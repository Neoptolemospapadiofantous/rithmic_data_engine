#!/usr/bin/env bash
# walk_forward.sh — does choosing parameters on past months work on the next month? (per family × session)
#
#   usage: scripts/walk_forward.sh [--labels nq_hist,nq_bars] [--train 3,6,12] [--min-train-trades 20]
#
# Reads finished history months only (hist_trades, scripts/sql/history_done.sql), net per MNQ contract,
# buckets each trade by session window of its ET entry time, and runs build/walk_forward
# (src/paper/walk_forward.hpp). nq_bars rows count only for strategies marked reliable by bars_vs_ticks.sh. Output: data/walk_forward/report.md + results.csv. Read-only.
# Session windows (ET entry time): asia 18:00–02:59 · london 03:00–09:29 · ny_open 09:30–10:59 ·
# ny_mid 11:00–13:59 · ny_pm 14:00–17:59.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
LABELS="nq_hist,nq_bars"; TRAIN="3,6,12"; MINT=20
while [[ $# -gt 0 ]]; do case "$1" in
  --labels) LABELS="$2"; shift;; --train) TRAIN="$2"; shift;; --min-train-trades) MINT="$2"; shift;;
  *) echo "unknown arg $1"; exit 2;; esac; shift; done
[[ "$LABELS" =~ ^[a-z0-9_,]+$ && "$TRAIN" =~ ^[0-9,]+$ && "$MINT" =~ ^[0-9]+$ ]] || { echo "bad args"; exit 2; }
BIN="${WALK_FORWARD_BIN:-build/walk_forward}"
[[ -x "$BIN" ]] || { echo "$BIN missing — cmake --build build --target walk_forward"; exit 1; }
OUT=data/walk_forward; mkdir -p "$OUT"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
IN_LIST="'${LABELS//,/\',\'}'"

T0=$(date +%s)
scripts/history_done_sync.sh >/dev/null || { echo "history_done_sync failed"; exit 1; }
"${PSQL[@]}" <<SQL || exit 1
SET work_mem = '512MB';
\copy (WITH t AS (SELECT strategy_id, (entry_time AT TIME ZONE 'America/New_York') AS et, pnl_usd / GREATEST(qty, 1) AS p FROM hist_trades WHERE account_label IN ($IN_LIST) AND (account_label <> 'nq_bars' OR strategy_id IN (SELECT strategy_id FROM history_bar_reliability WHERE reliable))) SELECT strategy_id, split_part(strategy_id, '_', 1), CASE WHEN et::time >= '18:00' OR et::time < '03:00' THEN 'asia' WHEN et::time < '09:30' THEN 'london' WHEN et::time < '11:00' THEN 'ny_open' WHEN et::time < '14:00' THEN 'ny_mid' ELSE 'ny_pm' END, to_char(et + interval '6 hours', 'YYYY-MM'), count(*), round(sum(p)::numeric, 2) FROM t GROUP BY 1, 2, 3, 4 ORDER BY 4) TO '$OUT/cells.csv' CSV
SQL
echo "[walk_forward] export: $(wc -l < "$OUT/cells.csv") strategy×session×month cells ($(( $(date +%s) - T0 ))s)"
"$BIN" --cells "$OUT/cells.csv" --train "$TRAIN" --min-train-trades "$MINT" --report "$OUT/report.md" --csv "$OUT/results.csv" || exit 1
sed -i "2i _Generated $(date -Iseconds) from labels \`$LABELS\` by scripts/walk_forward.sh._" "$OUT/report.md"
echo "[walk_forward] done in $(( $(date +%s) - T0 ))s → $OUT/report.md"
