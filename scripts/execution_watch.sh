#!/usr/bin/env bash
# execution_watch.sh — intraday EXECUTION watch for a live executor (execution-watch.timer, every 2 min).
# Outside weekday RTH (09:25–16:05 ET) it exits 0 silently. Inside it, it checks the things that make a
# session silently produce nothing: unit down, dry-run mode, broker sessions missing since the last
# start, executor log not written, feed stale, a halt that never lifted, an exchange stop lagging the
# internal stop. Each condition alerts ONCE on transition (state file) and once more when it clears.
#   scripts/execution_watch.sh [account]
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
ACC="${1:-tradeify}"; U="nq-executor-local@$ACC"; L="data/logs/nq_executor_${ACC}.log"; CFG="config/${ACC}_config.json"
STATE="data/.execution_watch_${ACC}.state"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -X -Atq -h "${PG_HOST:-127.0.0.1}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}")
notify() { for b in "$HOME/.local/bin/grid-notify" "$HOME/.config/ecosystem/bin/grid-notify"; do
             [[ -x "$b" ]] && { "$b" "$1" >/dev/null 2>&1 || true; return; }; done; }
j() { python3 -c "import json; c=json.load(open('$CFG')); v=c.get('$1'); print('' if v is None else v)" 2>/dev/null; }

dow=$(TZ=America/New_York date +%u); hm=$(TZ=America/New_York date +%H%M)
(( dow <= 5 )) && [[ "$hm" > "0924" && "$hm" < "1606" ]] || exit 0

problems=()
[[ $(systemctl --user is-active "$U" 2>/dev/null) == active ]] || problems+=("unit $U not active")
[[ "$(j dry_run)" == "False" ]] || problems+=("config dry_run is not false — nothing reaches Rithmic")
if [[ -f "$L" ]]; then
  start_line=$(grep -an 'Instance lock acquired' "$L" | tail -1 | cut -d: -f1); start_line=${start_line:-1}
  blk=$(tail -n +"$start_line" "$L")
  grep -aq 'ORDER_PLANT login OK' <<<"$blk" || problems+=("no ORDER_PLANT login since the last start")
  grep -aq 'PNL_PLANT position subscription OK' <<<"$blk" || problems+=("no PNL_PLANT subscription since the last start")
  grep -aq 'dry_run=TRUE' <<<"$blk" && problems+=("executor started in DRY RUN")
  age=$(( $(date +%s) - $(stat -c %Y "$L") )); (( age <= 90 )) || problems+=("executor log silent for ${age}s")
  lasthalt=$(grep -aE 'Trading (un)?halted' <<<"$blk" | tail -1)
  [[ "$lasthalt" == *"Trading halted"* && "$lasthalt" != *shutdown* ]] && problems+=("halted: ${lasthalt##*Trading halted: }")
  tc=$(grep -a 'TRAIL-CHECK' <<<"$blk" | tail -1)
  if [[ -n "$tc" ]]; then sl=$(grep -oE 'sl=[-0-9.]+' <<<"$tc" | head -1 | cut -d= -f2); ex=$(grep -oE 'exch_sl=[-0-9.]+' <<<"$tc" | cut -d= -f2)
    ts=$(sed -E 's/^\[([0-9.]+)\].*/\1/' <<<"$tc"); now=$(date +%s)
    if [[ "$sl" != "$ex" ]] && ! python3 -c "import sys; sys.exit(0 if $now-float('$ts') < 20 else 1)"; then
      problems+=("exchange stop $ex != internal stop $sl for >20s"); fi
  fi
else problems+=("executor log $L missing"); fi
sym=$(j md_feed_symbol); sym=${sym:-NQ}
tick_age=$("${PSQL[@]}" -c "select coalesce(round(extract(epoch from now()-max(ts_event))),9999) from ticks where symbol='$sym' and ts_event > now()-interval '10 minutes'" 2>/dev/null)
(( ${tick_age:-9999} <= 45 )) || problems+=("$sym feed stale ${tick_age:-?}s")

cur=$(IFS='; '; echo "${problems[*]:-}"); prev=$(cat "$STATE" 2>/dev/null || true)
if [[ "$cur" != "$prev" ]]; then
  if [[ -n "$cur" ]]; then notify "🔴 execution watch ($ACC, $(TZ=America/New_York date +%H:%M) ET): $cur"
  else notify "🟢 execution watch ($ACC, $(TZ=America/New_York date +%H:%M) ET): recovered"; fi
  printf '%s' "$cur" > "$STATE"
fi
echo "$(date -Is) $ACC: ${cur:-ok}" >> data/logs/execution_watch.log
[[ -z "$cur" ]]
