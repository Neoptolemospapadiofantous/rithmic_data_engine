#!/usr/bin/env bash
# payout_sim.sh — which strategy gets a Tradeify funded account paid every 1–2 weeks?
#
#   usage: scripts/payout_sim.sh [--label nq_hist] [--min-trades 40] [--horizon 60] [--step 2]
#
# Exports every paper strategy's trades (net per MNQ contract — the fleet trades MNQ) from the history label, alone and filtered to
# the regimes known at entry (gap vs ATR14, the last closed session's day type, close vs its 50-session average),
# keeps the series with ≥ min-trades and net > 0, and runs them through build/payout_sim (plans + model in
# src/paper/payout_sim.hpp): daily rules chosen on the first 60% of the days, scored on the rest; single series
# at 1/2/3/5 MNQ and 1 NQ, plus greedy portfolios. Output: data/payout/report.md + results.csv.
# Read-only on Postgres; never touches a fleet or live config. The regime filters are chosen on the same
# history they are scored on — treat a hit as a candidate for the bars history / forward test, not a result.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
LABEL=nq_hist; MIN=40; HORIZON=60; STEP=2
while [[ $# -gt 0 ]]; do case "$1" in
  --label) LABEL="$2"; shift;; --min-trades) MIN="$2"; shift;;
  --horizon) HORIZON="$2"; shift;; --step) STEP="$2"; shift;;
  *) echo "unknown arg $1"; exit 2;; esac; shift; done
[[ "$LABEL" =~ ^[a-z0-9_]+$ ]] || { echo "bad label"; exit 2; }
BIN="${PAYOUT_SIM_BIN:-build/payout_sim}"
[[ -x "$BIN" ]] || { echo "$BIN missing — cmake --build build --target payout_sim"; exit 1; }
OUT=data/payout; mkdir -p "$OUT"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)

T0=$(date +%s)
scripts/history_done_sync.sh >/dev/null || { echo "history_done_sync failed"; exit 1; }   # finished months only
"${PSQL[@]}" <<SQL || exit 1
SET work_mem = '512MB';
-- hist_trades_regime (scripts/sql/history_done.sql): finished months only, regime known at entry, prop trading day.
CREATE TEMP TABLE t AS
SELECT strategy_id, entry_time, trade_day AS d, pnl_usd / GREATEST(qty, 1) AS pnl, mae_pts, mfe_pts, gap_b, prev_b, trend_b
FROM hist_trades_regime WHERE account_label = '$LABEL'
  AND (account_label <> 'nq_bars' OR strategy_id IN (SELECT strategy_id FROM history_bar_reliability WHERE reliable));   -- bars: only strategies that pass bars_vs_ticks.sh
\copy (SELECT DISTINCT d FROM t ORDER BY 1) TO '$OUT/calendar.csv' CSV
CREATE TEMP TABLE c AS
          SELECT strategy_id AS series, entry_time, d, pnl, mae_pts, mfe_pts FROM t
UNION ALL SELECT strategy_id || ' · ' || gap_b,  entry_time, d, pnl, mae_pts, mfe_pts FROM t WHERE gap_b IS NOT NULL
UNION ALL SELECT strategy_id || ' · ' || prev_b, entry_time, d, pnl, mae_pts, mfe_pts FROM t WHERE prev_b IS NOT NULL
UNION ALL SELECT strategy_id || ' · ' || trend_b, entry_time, d, pnl, mae_pts, mfe_pts FROM t WHERE trend_b IS NOT NULL;
CREATE TEMP TABLE keep AS SELECT series FROM c GROUP BY 1 HAVING count(*) >= $MIN AND sum(pnl) > 0;
\copy (SELECT c.series, c.d, round(c.pnl::numeric, 2), round(COALESCE(c.mae_pts, 0)::numeric, 2), round(COALESCE(c.mfe_pts, 0)::numeric, 2), extract(epoch FROM c.entry_time)::bigint FROM c JOIN keep USING (series) ORDER BY c.series, c.entry_time) TO '$OUT/trades.csv' CSV
SQL
echo "[payout_sim] export: $(wc -l < "$OUT/trades.csv") trades, $(cut -d, -f1 "$OUT/trades.csv" | sort -u | wc -l) series, $(wc -l < "$OUT/calendar.csv") days ($(( $(date +%s) - T0 ))s)"
"$BIN" --trades "$OUT/trades.csv" --calendar "$OUT/calendar.csv" --horizon "$HORIZON" --step "$STEP" \
       --csv "$OUT/results.csv" --report "$OUT/report.md" || exit 1
sed -i "2i _Generated $(date -Iseconds) from label \`$LABEL\` by scripts/payout_sim.sh (series kept: ≥ $MIN trades and net > 0)._" "$OUT/report.md"
echo "[payout_sim] done in $(( $(date +%s) - T0 ))s → $OUT/report.md"
