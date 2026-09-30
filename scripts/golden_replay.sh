#!/usr/bin/env bash
# golden_replay.sh — regression net for the paper fleet.
#   Replays a FROZEN window over recorded ticks + quotes under account label
#   'golden', exports the trades, and diffs them against tests/golden/<name>.csv.
#   Any difference is a behaviour change: either intended (re-freeze with a reason
#   in CHANGES.md) or a bug.
#
#   usage: scripts/golden_replay.sh [--freeze] [--twice] [--name NAME] [--from 'YYYY-MM-DD HH:MM'] [--to '…']
#     --freeze   write the current result as the new golden file
#     --twice    run the replay twice and require byte-identical output (determinism)
#   exit 0 = match, 1 = mismatch/missing golden, 2 = replay failed
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
NAME="2026-09-22_rth_open"; FROM="2026-09-22 09:25"; TO="2026-09-22 11:00"; FREEZE=0; TWICE=0
while [[ $# -gt 0 ]]; do case "$1" in
  --freeze) FREEZE=1;; --twice) TWICE=1;; --name) NAME="$2"; shift;; --from) FROM="$2"; shift;; --to) TO="$2"; shift;;
  *) echo "unknown arg $1"; exit 2;; esac; shift; done
GOLD="tests/golden/$NAME.csv"; OUT="data/validation/golden_$NAME.csv"; STATUS="data/validation/golden_last.json"
set -a; . ./.env 2>/dev/null; set +a
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq)
export PGPASSWORD="$PG_PASSWORD"
export_trades() {  # deterministic order, stable columns, no ids/timestamps of insertion
  "${PSQL[@]}" -c "SELECT strategy_id, direction, qty, to_char(entry_time AT TIME ZONE 'UTC','YYYY-MM-DD HH24:MI:SS.US'), entry_price,
                          to_char(exit_time AT TIME ZONE 'UTC','YYYY-MM-DD HH24:MI:SS.US'), exit_price, pnl_pts, pnl_usd, exit_reason,
                          COALESCE(entry_bbo::text,''), COALESCE(exit_bbo::text,''), COALESCE(pnl_bbo_usd::text,'')
                   FROM paper_trades WHERE account_label='golden' ORDER BY strategy_id, entry_time, direction" -F ',' > "$1"
}
run_once() {
  "${PSQL[@]}" -c "DELETE FROM paper_trades WHERE account_label='golden'; DELETE FROM paper_signals WHERE account_label='golden'" >/dev/null
  if ! ./build/paper_engine --config config/paper_fleet.json --account-label golden --replay-from "$FROM" --replay-to "$TO" > data/logs/paper_golden.log 2>&1; then
    echo "golden: replay FAILED (see data/logs/paper_golden.log)"; return 2; fi
  export_trades "$1"
}
t0=$(date +%s)
run_once "$OUT" || { echo "{\"ok\":false,\"name\":\"$NAME\",\"error\":\"replay failed\",\"ts\":\"$(date -Is)\"}" > "$STATUS"; exit 2; }
N=$(wc -l < "$OUT"); DET="skipped"
if [[ $TWICE -eq 1 ]]; then
  run_once "$OUT.2" || exit 2
  if cmp -s "$OUT" "$OUT.2"; then DET="identical"; else DET="DIFFERENT"; fi
fi
"${PSQL[@]}" -c "DELETE FROM paper_trades WHERE account_label='golden'; DELETE FROM paper_signals WHERE account_label='golden'" >/dev/null
if [[ $FREEZE -eq 1 ]]; then cp "$OUT" "$GOLD"; echo "golden: froze $N trades → $GOLD"; fi
if [[ ! -f "$GOLD" ]]; then
  echo "golden: no golden file $GOLD (run with --freeze)"; echo "{\"ok\":false,\"name\":\"$NAME\",\"error\":\"no golden file\",\"trades\":$N,\"ts\":\"$(date -Is)\"}" > "$STATUS"; exit 1; fi
ADDED=$(comm -13 <(sort "$GOLD") <(sort "$OUT") | wc -l); REMOVED=$(comm -23 <(sort "$GOLD") <(sort "$OUT") | wc -l)
comm -3 <(sort "$GOLD") <(sort "$OUT") | head -20 > "data/validation/golden_${NAME}.diff"
OK=$([[ $ADDED -eq 0 && $REMOVED -eq 0 && "$DET" != "DIFFERENT" ]] && echo true || echo false)
echo "{\"ok\":$OK,\"name\":\"$NAME\",\"window\":\"$FROM → $TO\",\"trades\":$N,\"golden_trades\":$(wc -l < "$GOLD"),\"added\":$ADDED,\"removed\":$REMOVED,\"determinism\":\"$DET\",\"secs\":$(( $(date +%s) - t0 )),\"ts\":\"$(date -Is)\"}" > "$STATUS"
cat "$STATUS"; echo
[[ "$OK" == true ]] && exit 0 || exit 1
