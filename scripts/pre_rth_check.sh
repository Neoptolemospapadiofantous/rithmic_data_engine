#!/usr/bin/env bash
# pre_rth_check.sh — 09:00 ET weekday readiness report for the live executor (pre-rth-check.timer).
# Runs the stack index, confirms the production executor is up with position truth (a [BROKER]
# line since its last start) and opens at 09:30, and sends one Telegram line either way.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
ACC="${1:-tradeify}"; U="nq-executor-local@$ACC"; L="data/logs/nq_executor_$ACC.log"
OUT="data/logs/pre_rth_check.log"
notify() { for b in "$HOME/.local/bin/grid-notify" "$HOME/.config/ecosystem/bin/grid-notify"; do
             [[ -x "$b" ]] && { "$b" "$1" >/dev/null 2>&1 || true; return; }; done; }
{ echo "=== $(date -Is)"; bash scripts/stack_status.sh "$ACC"; } >> "$OUT" 2>&1; ss=$?
problems=()
(( ss == 0 )) || problems+=("stack-status failed")
[[ -f NO_DEPLOY ]] && problems+=("NO_DEPLOY present")
[[ $(systemctl --user is-active "$U") == active ]] || problems+=("$U not active")
start_line=$(grep -an 'Instance lock acquired' "$L" 2>/dev/null | tail -1 | cut -d: -f1)
broker=$(tail -n +"${start_line:-1}" "$L" 2>/dev/null | grep -a '\[BROKER\]' | tail -1 | grep -o 'balance=.*')
[[ -n "$broker" ]] || problems+=("no [BROKER] line since the executor started")
# startup halts are normally lifted once the snapshot confirms flat — only the LAST halt/unhalt counts
last_h=$(tail -n +"${start_line:-1}" "$L" 2>/dev/null | grep -aoE '(\[ORB\]|\[TREND [a-z_]+\]) Trading (un)?halted: .*|\[TREND [a-z_]+\] (un)?halted: .*' | tail -1)
[[ "$last_h" == *" halted: "* || "$last_h" == *"Trading halted"* ]] && problems+=("halted — $last_h")
grep -q '"session_open_hour": 9,' "config/${ACC}_config.json" && grep -q '"session_open_min": 30,' "config/${ACC}_config.json" \
  || problems+=("config does not open at 09:30")
read -r avail < <(awk '/^MemAvailable:/{print int($2/1024)}' /proc/meminfo)
(( avail < 3072 )) && problems+=("only ${avail} MB RAM available — close browser tabs")
if (( ${#problems[@]} )); then
  notify "🔴 pre-RTH check ($ACC): $(IFS='; '; echo "${problems[*]}") — fix before 09:30 ET"; exit 1
fi
notify "🟢 pre-RTH check ($ACC): executor up, flat-checked, ${broker}, ${avail} MB free — ready for 09:30 ET"
