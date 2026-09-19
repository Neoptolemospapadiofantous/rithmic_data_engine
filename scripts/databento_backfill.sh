#!/usr/bin/env bash
# databento_backfill.sh — rebuild the NQ tick history from Databento (CME Globex GLBX.MDP3).
#
# Replaces the lost 2023-03 → 2026-03 tick dataset (269.8M rows, see
# data/migrate_progress.json) that lived only on the terminated Oracle VM.
#
# Usage (from repo root, DATABENTO_API_KEY in .env for cost/download):
#   bash scripts/databento_backfill.sh cost       ← price the full download BEFORE spending credit
#   bash scripts/databento_backfill.sh download   ← pull monthly CSV chunks → data/backfill/ (resumable)
#   bash scripts/databento_backfill.sh load       ← COPY chunks into Postgres ticks (dedup, resumable)
#   bash scripts/databento_backfill.sh bars       ← build/refresh 1-min bars from ticks (plain PG only)
#   bash scripts/databento_backfill.sh status     ← row counts + date coverage
#
# Notes:
#   - Continuous front month by volume (NQ.v.0), trades schema, CSV+zstd.
#   - `load`/`bars`/`status` need no API key — only Postgres (.env PG_*).
#   - ticks DDL matches src/db.cpp exactly; the collector and this script can
#     each run first, in any order, on both plain PG (local) and TimescaleDB
#     (Oracle — where bars_1min is the collector's continuous aggregate and
#     the `bars` step skips itself).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

# .env may contain secrets with shell metacharacters — grep keys out, never source it
# (same pattern as scripts/hermes.sh).
env_get() { grep -E "^$1=" .env 2>/dev/null | head -1 | cut -d= -f2- | tr -d "'\"" || true; }

PG_HOST="${PG_HOST:-$(env_get PG_HOST)}";     PG_HOST="${PG_HOST:-127.0.0.1}"
PG_PORT="${PG_PORT:-$(env_get PG_PORT)}";     PG_PORT="${PG_PORT:-5432}"
PG_DB="${PG_DB:-$(env_get PG_DB)}";           PG_DB="${PG_DB:-rithmic}"
PG_USER="${PG_USER:-$(env_get PG_USER)}";     PG_USER="${PG_USER:-rithmic_user}"
export PGPASSWORD="${PG_PASSWORD:-$(env_get PG_PASSWORD)}"
DATABENTO_API_KEY="${DATABENTO_API_KEY:-$(env_get DATABENTO_API_KEY)}"
PSQL=(psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DB" -v ON_ERROR_STOP=1 -q)

API="https://hist.databento.com/v0"
DATASET="GLBX.MDP3"
DB_SCHEMA="trades"
SYMBOLS="${BACKFILL_SYMBOL:-NQ.v.0}"     # continuous front month by volume
STYPE="continuous"
START="${BACKFILL_START:-2023-03-01}"
END="${BACKFILL_END:-$(date -u +%Y-%m-%d)}"
OUT="data/backfill"

log() { echo "[backfill] $*"; }
die() { echo "[backfill] ERROR: $*" >&2; exit 1; }

need_key() {
  [[ -n "${DATABENTO_API_KEY:-}" ]] ||
    die "DATABENTO_API_KEY not set. Sign up at databento.com (free \$125 credit), create an API key, add DATABENTO_API_KEY=db-... to .env"
}

ensure_ticks_table() {
  "${PSQL[@]}" <<'SQL'
CREATE TABLE IF NOT EXISTS ticks (
    ts_event  TIMESTAMPTZ      NOT NULL,
    symbol    VARCHAR(32)      NOT NULL DEFAULT 'NQ',
    exchange  VARCHAR(32)      NOT NULL DEFAULT 'CME',
    price     DOUBLE PRECISION NOT NULL,
    size      BIGINT           NOT NULL,
    side      CHAR(1),
    is_buy    BOOLEAN,
    source    VARCHAR(32)      DEFAULT 'amp_rithmic'
);
CREATE UNIQUE INDEX IF NOT EXISTS ticks_dedup_idx
    ON ticks (symbol, exchange, ts_event, price, size);
CREATE INDEX IF NOT EXISTS idx_ticks_ts ON ticks (ts_event);
SQL
}

cmd_cost() {
  need_key
  log "Pricing $DB_SCHEMA for $SYMBOLS ($START → $END) — no credit is spent by this call."
  local usd
  usd=$(curl -sS -f -u "$DATABENTO_API_KEY:" -G "$API/metadata.get_cost" \
    --data-urlencode "dataset=$DATASET" \
    --data-urlencode "symbols=$SYMBOLS" \
    --data-urlencode "schema=$DB_SCHEMA" \
    --data-urlencode "stype_in=$STYPE" \
    --data-urlencode "start=$START" \
    --data-urlencode "end=$END")
  log "Estimated cost: \$${usd} USD (signup credit: \$125)"
}

cmd_download() {
  need_key
  command -v zstd >/dev/null || die "zstd not installed (sudo apt install zstd)"
  mkdir -p "$OUT"
  local m chunk_start chunk_end f
  m="${START:0:7}-01"
  while [[ "$m" < "$END" ]]; do
    chunk_start="$m"
    chunk_end=$(date -u -d "$m +1 month" +%Y-%m-%d)
    [[ "$chunk_end" > "$END" ]] && chunk_end="$END"
    f="$OUT/nq_trades_${m:0:7}.csv.zst"
    if [[ -f "$f" ]]; then
      log "skip ${m:0:7} (exists)"
    else
      log "download ${chunk_start} → ${chunk_end} ..."
      curl -sS -f -u "$DATABENTO_API_KEY:" -o "$f.part" "$API/timeseries.get_range" \
        -d "dataset=$DATASET" \
        -d "symbols=$SYMBOLS" \
        -d "schema=$DB_SCHEMA" \
        -d "stype_in=$STYPE" \
        -d "start=$chunk_start" \
        -d "end=$chunk_end" \
        -d "encoding=csv" \
        -d "compression=zstd" \
        -d "pretty_px=true" \
        -d "pretty_ts=true" \
        -d "map_symbols=true" || { rm -f "$f.part"; die "download failed for ${m:0:7}"; }
      mv "$f.part" "$f"
      log "  saved $f ($(du -h "$f" | cut -f1))"
    fi
    m=$(date -u -d "$m +1 month" +%Y-%m-01)
  done
  log "Download complete."
}

cmd_load() {
  command -v zstd >/dev/null || die "zstd not installed (sudo apt install zstd)"
  ensure_ticks_table
  "${PSQL[@]}" <<'SQL'
CREATE TABLE IF NOT EXISTS _databento_stage (
    ts_recv TEXT, ts_event TEXT, rtype TEXT, publisher_id TEXT,
    instrument_id TEXT, action TEXT, side TEXT, depth TEXT,
    price TEXT, size TEXT, flags TEXT, ts_in_delta TEXT,
    sequence TEXT, symbol TEXT
);
SQL
  local f loaded=0
  shopt -s nullglob
  for f in "$OUT"/nq_trades_*.csv.zst; do
    if [[ -f "$f.loaded" ]]; then
      log "skip $(basename "$f") (loaded)"
      continue
    fi
    log "load $(basename "$f") ..."
    "${PSQL[@]}" -c "TRUNCATE _databento_stage;"
    zstdcat "$f" | "${PSQL[@]}" -c "\\copy _databento_stage FROM STDIN WITH (FORMAT csv, HEADER true)"
    "${PSQL[@]}" <<'SQL'
INSERT INTO ticks (ts_event, symbol, exchange, price, size, side, is_buy, source)
SELECT regexp_replace(ts_event, '(\.\d{6})\d*Z$', '\1Z')::timestamptz,
       'NQ', 'CME',
       price::double precision,
       size::bigint,
       side::char(1),
       (side = 'B'),
       'databento'
FROM _databento_stage
WHERE action = 'T'
ON CONFLICT (symbol, exchange, ts_event, price, size) DO NOTHING;
SQL
    touch "$f.loaded"
    loaded=$((loaded + 1))
  done
  log "Loaded $loaded new chunk(s)."
  cmd_status
}

cmd_bars() {
  # On TimescaleDB (Oracle) bars_1min is the collector's continuous aggregate — skip.
  local kind
  kind=$("${PSQL[@]}" -tAc "SELECT COALESCE((SELECT relkind FROM pg_class WHERE oid = to_regclass('public.bars_1min')), '-')")
  if [[ "$kind" == "m" ]]; then
    log "bars_1min is a TimescaleDB continuous aggregate here — auto-refreshed, nothing to do."
    return 0
  fi
  if [[ "$kind" == "-" ]]; then
    log "Creating plain bars_1min table..."
    "${PSQL[@]}" <<'SQL'
CREATE TABLE bars_1min (
    ts     TIMESTAMPTZ PRIMARY KEY,
    open   DOUBLE PRECISION NOT NULL,
    high   DOUBLE PRECISION NOT NULL,
    low    DOUBLE PRECISION NOT NULL,
    close  DOUBLE PRECISION NOT NULL,
    volume BIGINT NOT NULL
);
SQL
  fi
  log "Aggregating ticks → bars_1min (upsert, may take a few minutes on a full history)..."
  "${PSQL[@]}" <<'SQL'
INSERT INTO bars_1min (ts, open, high, low, close, volume)
SELECT date_trunc('minute', ts_event)                          AS ts,
       (array_agg(price ORDER BY ts_event ASC))[1]             AS open,
       MAX(price)                                              AS high,
       MIN(price)                                              AS low,
       (array_agg(price ORDER BY ts_event DESC))[1]            AS close,
       SUM(size)                                               AS volume
FROM ticks
GROUP BY 1
ON CONFLICT (ts) DO UPDATE
    SET open = EXCLUDED.open, high = EXCLUDED.high, low = EXCLUDED.low,
        close = EXCLUDED.close, volume = EXCLUDED.volume;
SQL
  log "bars_1min ready: $("${PSQL[@]}" -tAc 'SELECT COUNT(*) FROM bars_1min') rows"
}

cmd_status() {
  local t
  t=$("${PSQL[@]}" -tAc "SELECT to_regclass('public.ticks')")
  if [[ -z "$t" ]]; then
    log "ticks table does not exist yet — run: $0 load"
    return 0
  fi
  "${PSQL[@]}" -c "
    SELECT COUNT(*)                        AS ticks,
           MIN(ts_event)::date             AS first_day,
           MAX(ts_event)::date             AS last_day,
           COUNT(DISTINCT ts_event::date)  AS days
    FROM ticks;"
  shopt -s nullglob
  local n_files=("$OUT"/nq_trades_*.csv.zst)
  log "chunks on disk: ${#n_files[@]} ($(ls "$OUT"/*.loaded 2>/dev/null | wc -l) loaded)"
}

case "${1:-}" in
  cost)     cmd_cost ;;
  download) cmd_download ;;
  load)     cmd_load ;;
  bars)     cmd_bars ;;
  status)   cmd_status ;;
  *) grep '^#' "$0" | head -25; exit 1 ;;
esac
