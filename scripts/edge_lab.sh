#!/usr/bin/env bash
# edge_lab.sh — the strategy research pipeline (research/README.md).
#
#   scripts/edge_lab.sh export [SYM …]        bars_1m (default NQ ES; <X>LOG = <X>USDT on a log scale, 1 pt = 1 bp) + NQ order-book minutes + FOMC dates → data/edge_lab/cache/
#   scripts/edge_lab.sh run [--quick] SPEC…   build/edge_lab on research/ideas/*.json → data/edge_lab/reports/, then load
#   scripts/edge_lab.sh meta SPEC…            take/skip model on a passed idea's trades → reports/<id>__meta.md, then load
#   scripts/edge_lab.sh exits SPEC…           thesis exits on a passed idea's trades   → reports/<id>__exits.md, then load
#   scripts/edge_lab.sh macro                 free FRED yields / dollar + CBOE VVIX / SKEW → data/edge_lab/cache/macro.csv
#   scripts/edge_lab.sh load                  data/edge_lab/results.csv → research_ideas / research_runs
#   scripts/edge_lab.sh list                  every idea and its verdict, newest first
#
# Read-only on market data. EDGE_LAB_BIN overrides the binary (build-exp/edge_lab during RTH — never rebuild build/
# 09:25–16:05 ET).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
OUT=data/edge_lab; CACHE=$OUT/cache; mkdir -p "$CACHE" "$OUT/reports"
BIN="${EDGE_LAB_BIN:-build/edge_lab}"

cmd="${1:-}"; shift || true
case "$cmd" in
export)
    syms=("$@"); [[ ${#syms[@]} -eq 0 ]] && syms=(NQ ES)
    for s in "${syms[@]}"; do
        [[ "$s" =~ ^[A-Z0-9]+$ ]] || { echo "bad symbol $s"; exit 2; }
        t0=$(date +%s)
        if [[ "$s" == *LOG ]]; then   # <X>LOG = <X>USDT on a log scale, 1 point = 1 basis point (crypto, cost in bp)
            src="${s%LOG}USDT"
            "${PSQL[@]}" -c "\copy (SELECT extract(epoch FROM (minute AT TIME ZONE 'America/New_York'))::bigint, ln(open)*10000, ln(high)*10000, ln(low)*10000, ln(close)*10000, volume, coalesce(buy_volume, 0) FROM bars_1m WHERE symbol = '$src' AND open > 0 AND low > 0 ORDER BY minute) TO '$CACHE/$s.csv.tmp' CSV" || exit 1
        else
        "${PSQL[@]}" -c "\copy (SELECT extract(epoch FROM (minute AT TIME ZONE 'America/New_York'))::bigint, open, high, low, close, volume, coalesce(buy_volume, 0) FROM bars_1m WHERE symbol = '$s' ORDER BY minute) TO '$CACHE/$s.csv.tmp' CSV" || exit 1
        fi
        mv "$CACHE/$s.csv.tmp" "$CACHE/$s.csv"
        echo "$s: $(wc -l <"$CACHE/$s.csv") bars in $(( $(date +%s) - t0 )) s"
    done
    # order-book minutes (mbo_1m, NQ GLBX 2023-03 … 2026-03) for bk(); edge_lab runs without it
    "${PSQL[@]}" -c "\copy (SELECT extract(epoch FROM (ts_minute AT TIME ZONE 'America/New_York'))::bigint, qty_imbalance, cancel_to_add_ratio, bid_qty_top10bps, ask_qty_top10bps FROM mbo_1m WHERE symbol = 'NQ' AND venue = 'GLBX' AND complete ORDER BY ts_minute) TO '$CACHE/NQ.book.csv.tmp' CSV" \
        && mv "$CACHE/NQ.book.csv.tmp" "$CACHE/NQ.book.csv" && echo "NQ book: $(wc -l <"$CACHE/NQ.book.csv") minutes"
    "${PSQL[@]}" -c "\copy (SELECT day FROM calendar WHERE kind = 'fomc' ORDER BY day) TO '$CACHE/fomc.txt'" || exit 1
    echo "fomc: $(wc -l <"$CACHE/fomc.txt") days"
    # macro release days (calendar kinds cpi / ppi / nfp, migrations/20261017_calendar_bls_releases.sql) → YYYYMMDD,kind for load_events
    "${PSQL[@]}" -c "\\copy (SELECT to_char(day, 'YYYYMMDD'), kind FROM calendar WHERE kind IN ('cpi', 'ppi', 'nfp') AND day <= current_date ORDER BY day, kind) TO '$CACHE/releases.csv' CSV" || exit 1
    echo "releases: $(wc -l <"$CACHE/releases.csv") cpi/ppi/nfp days"
    ;;
macro)
    # free daily macro / options data for the day facts vvix skew y10 y10c y2 usdc (edge_lab: data/edge_lab/cache/macro.csv)
    tmpd=$(mktemp -d)
    for id in DGS10 DGS2 DTWEXBGS; do curl -sfL --max-time 60 -o "$tmpd/$id.csv" "https://fred.stlouisfed.org/graph/fredgraph.csv?id=$id" || { echo "FRED $id failed"; exit 1; }; done
    for n in VVIX SKEW; do curl -sfL --max-time 60 -A "Mozilla/5.0" -o "$tmpd/$n.csv" "https://cdn.cboe.com/api/global/us_indices/daily_prices/${n}_History.csv" || { echo "CBOE $n failed"; exit 1; }; done
    # one row per date: YYYYMMDD,dgs10,dgs2,usd,vvix,skew (empty = no observation that day)
    awk -F, 'FNR == 1 { f++; next }
        f <= 3 { d = $1; gsub(/-/, "", d); if ($2 != "." && $2 != "") v[d, f] = $2; seen[d] = 1; next }
        { split($1, m, "/"); if (length(m[3]) != 4) next; d = sprintf("%04d%02d%02d", m[3], m[1], m[2]); v[d, f] = $NF + 0; seen[d] = 1 }
        END { for (d in seen) print d "," v[d,1] "," v[d,2] "," v[d,3] "," v[d,4] "," v[d,5] }' \
        "$tmpd/DGS10.csv" "$tmpd/DGS2.csv" "$tmpd/DTWEXBGS.csv" "$tmpd/VVIX.csv" "$tmpd/SKEW.csv" | sort > "$CACHE/macro.csv.tmp" \
        && mv "$CACHE/macro.csv.tmp" "$CACHE/macro.csv"
    rm -rf "$tmpd"
    echo "macro: $(wc -l <"$CACHE/macro.csv") dates ($(head -1 "$CACHE/macro.csv" | cut -d, -f1) … $(tail -1 "$CACHE/macro.csv" | cut -d, -f1))"
    ;;
run|meta|exits)
    [[ -x "$BIN" ]] || { echo "$BIN missing — cmake --build build --target edge_lab (build-exp in RTH)"; exit 1; }
    nice -n 10 "$BIN" "$cmd" "$@"; rc=$?
    "$0" load || rc=1
    exit $rc
    ;;
load)
    [[ -s $OUT/results.csv ]] || { echo "nothing to load"; exit 0; }
    "${PSQL[@]}" <<SQL || exit 1
CREATE TEMP TABLE r (run_at timestamptz, id text, name text, why text, verdict text, stage_reached int, quick boolean, metrics jsonb, spec jsonb);
\copy r FROM '$OUT/results.csv' CSV HEADER
INSERT INTO research_runs (idea_id, run_at, verdict, stage_reached, quick, metrics, spec)
  SELECT id, run_at, verdict, stage_reached, quick, metrics, spec FROM r ON CONFLICT DO NOTHING;
INSERT INTO research_ideas AS t (id, name, why, verdict, stage_reached, metrics, spec, last_run)
  SELECT DISTINCT ON (id) id, name, why, verdict, stage_reached, metrics, spec, run_at FROM r ORDER BY id, run_at DESC
  ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name, why = EXCLUDED.why, verdict = EXCLUDED.verdict,
    stage_reached = EXCLUDED.stage_reached, metrics = EXCLUDED.metrics, spec = EXCLUDED.spec, last_run = EXCLUDED.last_run,
    runs = t.runs + 1
  WHERE EXCLUDED.last_run > t.last_run;
SQL
    cat "$OUT/results.csv" >>"$OUT/results.loaded.csv" && rm -f "$OUT/results.csv"
    ;;
list)
    "${PSQL[@]}" -F $'\t' -c "SELECT to_char(last_run AT TIME ZONE 'America/New_York', 'MM-DD HH24:MI'), verdict, id,
        round((metrics->'oos'->>'avg_pts')::numeric, 2), round((metrics->'oos'->>'pf')::numeric, 2), (metrics->'oos'->>'n')
        FROM research_ideas ORDER BY last_run DESC" | column -t -s $'\t'
    ;;
*) sed -n 2,13p "$0"; exit 2;;
esac
