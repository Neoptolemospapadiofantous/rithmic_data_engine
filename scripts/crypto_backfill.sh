#!/usr/bin/env bash
# crypto_backfill.sh — load Binance USDⓈ-M futures trade history into `ticks` so the paper fleet can be
# replayed on crypto (founder 2026-10-10: "test all strategies for crypto").
#
#   scripts/crypto_backfill.sh <FROM YYYY-MM-DD> <TO YYYY-MM-DD> [SYMBOL ...]   (default BTCUSDT ETHUSDT)
#   BARS_ONLY=1 scripts/crypto_backfill.sh …   1-minute bars only (ATR warm-up), no ticks
#
# Source: https://data.binance.vision daily aggTrades (free, no key). Rows go in exactly the shape the live
# crypto_collector writes: symbol BTCUSDT/ETHUSDT, exchange BINANCE, size in 0.001 lots (llround(qty*1000)),
# is_buy = the aggressor bought (NOT is_buyer_maker), side 'B'/'A', seq = agg_trade_id % 2e9 so trades sharing a
# millisecond keep exchange order. source = 'binance_hist' (live rows say 'binance_fut').
# Binance stopped publishing futures bookTicker history in 2024, so backfilled days have NO quotes: book-gated
# variants are judged on the forward feed only.
#
# Idempotent per (symbol, UTC day): the day's binance_hist rows are deleted before the load. Refuses while CME is
# open (Sun 18:00 → Fri 17:00 ET): a bulk COPY competes with the NQ collector's writer (the 2026-09-30 feed-drop
# trap). FORCE=1 overrides. Days already loaded are skipped unless RELOAD=1. Log: data/logs/crypto_backfill.log.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
FROM="${1:?from YYYY-MM-DD}"; TO="${2:?to YYYY-MM-DD}"; shift 2
SYMS=("$@"); [[ ${#SYMS[@]} -eq 0 ]] && SYMS=(BTCUSDT ETHUSDT)
LOG=data/logs/crypto_backfill.log; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
say() { echo "$(date -Is) $*" | tee -a "$LOG"; }

cme_open() {   # Sun 18:00 ET → Fri 17:00 ET, minus the daily 17:00–18:00 break
  local dow hm; dow=$(TZ=America/New_York date +%u); hm=$((10#$(TZ=America/New_York date +%H%M)))
  (( hm >= 1700 && hm < 1800 )) && return 1
  case $dow in 6) return 1;; 7) (( hm >= 1800 ));; 5) (( hm < 1700 ));; *) return 0;; esac; }
if cme_open && [[ "${FORCE:-0}" != 1 ]]; then
  say "refused: CME is open — a bulk load competes with the NQ collector (FORCE=1 to override)"; exit 1; fi

# BARS_ONLY=1: 1-minute klines straight into bars_1m, no ticks (~60 KB a day instead of ~0.4 GB). Used for the
# 14 sessions before the tick window so session_stats has an ATR14 and the regime gates are not blind on day one.
if [[ "${BARS_ONLY:-0}" == 1 ]]; then
  d="$FROM"
  while [[ ! "$d" > "$TO" ]]; do
    for s in "${SYMS[@]}"; do
      url="https://data.binance.vision/data/futures/um/daily/klines/$s/1m/$s-1m-$d.zip"
      curl -sf -m 120 -o "$TMP/k.zip" "$url" || { say "$s $d: kline download failed"; continue; }
      # open_time,open,high,low,close,volume,close_time,quote_volume,count,taker_buy_volume,…  (volume in coins)
      unzip -p "$TMP/k.zip" | gawk -F, -v sym="$s" '$1 ~ /^[0-9]/ {
          printf "%s\t%s+00\t%s\t%s\t%s\t%s\t%d\t%d\t%d\n", sym, strftime("%Y-%m-%d %H:%M:%S", int($1/1000), 1),
            $2, $3, $4, $5, int($6*1000+0.5), int($10*1000+0.5), $9 }' > "$TMP/bars.tsv"
      "${PSQL[@]}" <<SQL || { say "$s $d: bars load FAILED"; continue; }
CREATE TEMP TABLE k (symbol text, minute timestamptz, open float8, high float8, low float8, close float8,
                     volume bigint, buy_volume bigint, ticks int);
\copy k FROM '$TMP/bars.tsv'
INSERT INTO bars_1m (symbol, minute, open, high, low, close, volume, buy_volume, ticks) SELECT * FROM k
ON CONFLICT (symbol, minute) DO NOTHING;
SQL
      say "$s $d: $(wc -l < "$TMP/bars.tsv") kline bars (bars only)"
    done
    d=$(date -d "$d + 1 day" +%F)
  done
  "${PSQL[@]}" -c "SELECT refresh_session_stats(60)" >/dev/null && say "session_stats refreshed"
  exit 0
fi

d="$FROM"
while [[ ! "$d" > "$TO" ]]; do
  for s in "${SYMS[@]}"; do
    have=$("${PSQL[@]}" -c "SELECT count(*) FROM ticks WHERE symbol='$s' AND source='binance_hist'
            AND ts_event >= '$d 00:00+00' AND ts_event < ('$d'::date + 1)::timestamp AT TIME ZONE 'UTC'")
    if [[ "$have" -gt 0 && "${RELOAD:-0}" != 1 ]]; then say "$s $d: $have rows already loaded — skip"; continue; fi
    url="https://data.binance.vision/data/futures/um/daily/aggTrades/$s/$s-aggTrades-$d.zip"
    if ! curl -sf -m 300 -o "$TMP/a.zip" "$url"; then say "$s $d: download failed ($url)"; continue; fi
    unzip -o -q "$TMP/a.zip" -d "$TMP" || { say "$s $d: unzip failed"; continue; }
    csv="$TMP/$s-aggTrades-$d.csv"
    # agg_trade_id,price,quantity,first_trade_id,last_trade_id,transact_time,is_buyer_maker (header row)
    gawk -F, -v sym="$s" 'NR==1 && $1 !~ /^[0-9]/ {next}
      { ms=$6; sec=int(ms/1000); buy=($7=="false");
        printf "%s.%03d+00\t%s\tBINANCE\t%s\t%d\t%d\t%s\t%s\tbinance_hist\n",
          strftime("%Y-%m-%d %H:%M:%S", sec, 1), ms%1000, sym, $2, int($3*1000+0.5), $1%2000000000,
          (buy?"B":"A"), (buy?"t":"f") }' "$csv" > "$TMP/rows.tsv"
    n=$(wc -l < "$TMP/rows.tsv")
    "${PSQL[@]}" <<SQL || { say "$s $d: load FAILED"; continue; }
SET synchronous_commit = off;
BEGIN;
DELETE FROM ticks WHERE symbol='$s' AND source='binance_hist'
  AND ts_event >= '$d 00:00+00' AND ts_event < ('$d'::date + 1)::timestamp AT TIME ZONE 'UTC';
\copy ticks (ts_event, symbol, exchange, price, size, seq, side, is_buy, source) FROM '$TMP/rows.tsv'
COMMIT;
SQL
    say "$s $d: loaded $n rows"
    rm -f "$TMP"/*.csv "$TMP/a.zip" "$TMP/rows.tsv"
  done
  d=$(date -d "$d + 1 day" +%F)
done
# bars_1m over exactly the loaded range: refresh_bars_1m() only walks FORWARD from a symbol's newest bar, so once
# live crypto rows exist it never sees an older backfilled day.
say "backfill $FROM..$TO ${SYMS[*]} done; rebuilding bars_1m for the range"
for s in "${SYMS[@]}"; do
  "${PSQL[@]}" -c "INSERT INTO bars_1m (symbol, minute, open, high, low, close, volume, buy_volume, ticks)
     SELECT '$s', date_trunc('minute', t.ts_event), (array_agg(t.price ORDER BY t.ts_event, t.seq))[1], max(t.price), min(t.price),
            (array_agg(t.price ORDER BY t.ts_event DESC, t.seq DESC))[1], sum(t.size)::bigint,
            COALESCE(sum(t.size) FILTER (WHERE t.is_buy), 0)::bigint, count(*)::integer
     FROM ticks t WHERE t.symbol = '$s' AND t.ts_event >= '$FROM 00:00+00'
       AND t.ts_event < ('$TO'::date + 1)::timestamp AT TIME ZONE 'UTC' AND t.ts_event < date_trunc('minute', now())
     GROUP BY 2
     ON CONFLICT (symbol, minute) DO UPDATE SET open = EXCLUDED.open, high = EXCLUDED.high, low = EXCLUDED.low,
       close = EXCLUDED.close, volume = EXCLUDED.volume, buy_volume = EXCLUDED.buy_volume, ticks = EXCLUDED.ticks" >/dev/null \
    || say "bars rebuild $s failed"
done
