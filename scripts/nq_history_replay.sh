#!/usr/bin/env bash
# nq_history_replay.sh — replay the NQ paper fleet over history, month by month, label nq_hist.
#
#   usage: scripts/nq_history_replay.sh FROM TO [--redo] [--with-mbo-1m]     (FROM/TO = YYYY-MM, inclusive)
#
# Each month is one scripts/history_month.sh nq YYYY-MM run: the Databento MBO file of that month read
# directly by paper_engine --dbn-mbo (2023-03 … 2026-03; nothing is loaded into `ticks`), or — from
# 2026-09 — the collector's own Postgres ticks/bbo. Months with neither (2026-04 … 2026-08: the gap
# 2026-03-20 … 2026-09-18) are recorded as no-data. Idempotent per month (the label's rows of the
# month are deleted first), resumable (done markers in data/history/done/nq/), refuses to start in
# weekday 09:25–16:05 ET and the Asia window 23:55–02:35 ET (FORCE=1 overrides) and waits for the window
# to end before each further month. build-exp/paper_engine under nice 15 / ionice idle.
# --with-mbo-1m also writes the month's per-minute order flow (mbo_1m venue GLBX, book_1m, bars_1m
# buy_volume) with dbn_tool mbo-1m. For the whole NQ + crypto history use scripts/history_queue.sh.
# Log: data/logs/nq_history_replay.log (+ per month data/logs/history/nq_<YYYY-MM>.log)
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
FROM="${1:-}"; TO="${2:-}"; shift 2 2>/dev/null || true
EXTRA=("$@")
[[ "$FROM" =~ ^[0-9]{4}-[0-9]{2}$ && "$TO" =~ ^[0-9]{4}-[0-9]{2}$ ]] || { echo "usage: $0 FROM TO (YYYY-MM) [--redo] [--with-mbo-1m]"; exit 2; }
LOG="data/logs/nq_history_replay.log"; mkdir -p data/logs
say() { echo "$(date -Is) $*" | tee -a "$LOG"; }
busy_window() {   # same windows as crypto_fleet.sh / fleet_expand.sh
  local dow hm; dow=$(TZ=America/New_York date +%u); hm=$((10#$(TZ=America/New_York date +%H%M)))
  (( dow <= 5 && hm >= 925 && hm < 1605 )) && { echo "regular hours"; return 0; }
  (( hm >= 2355 || hm < 235 )) && (( dow != 6 )) && { echo "Asia session"; return 0; }
  return 1
}
if w=$(busy_window) && [[ "${FORCE:-0}" != 1 ]]; then echo "refused: $w (FORCE=1 to override)"; exit 1; fi
m="$FROM"
while [[ "$m" < "$TO" || "$m" == "$TO" ]]; do
  while w=$(busy_window) && [[ "${FORCE:-0}" != 1 ]]; do say "pausing before $m — $w"; sleep 300; done
  out=$(scripts/history_month.sh nq "$m" "${EXTRA[@]}"); rc=$?
  case $rc in 0) say "nq_hist $m: $out";; 4) say "nq_hist $m: no data (no MBO file, outside the collector period)";;
              *) say "nq_hist $m: FAILED rc=$rc (data/logs/history/nq_$m.log) — stopping"; exit 1;; esac
  m=$(date -u -d "$m-01 +1 month" +%Y-%m)
done
say "nq_hist $FROM..$TO: finished"
