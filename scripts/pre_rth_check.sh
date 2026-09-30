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
# ── execution-config checks (founder 2026-09-30: validate the execution path every day)
CFGF="config/${ACC}_config.json"
cj() { python3 -c "import json; c=json.load(open('$CFGF')); v=c.get('$1'); print('' if v is None else v)" 2>/dev/null; }
[[ "$(cj dry_run)" == "False" ]] || problems+=("config dry_run is not false — nothing will reach Rithmic")
[[ "$(cj account_label)" != *_dry* ]] || problems+=("account_label $(cj account_label) is a dry label")
sym=$(cj symbol); pv=$(cj point_value); ct=$(cj trade_contract); qty=$(cj qty); comm=$(cj commission_rt)
case "$sym" in NQ) [[ "$pv" == 20.0 || "$pv" == 20 ]] || problems+=("point_value $pv for NQ (expect 20)");;
               MNQ) [[ "$pv" == 2.0 || "$pv" == 2 ]] || problems+=("point_value $pv for MNQ (expect 2)");; esac
[[ "$ct" == ${sym}* ]] || problems+=("trade_contract $ct does not match symbol $sym")
python3 -c "import sys; sys.exit(0 if float('${qty:-0}')>=1 else 1)" || problems+=("qty $qty")
python3 -c "import sys; sys.exit(0 if float('${comm:-0}')>0 else 1)" || problems+=("commission_rt is 0 — recorded P&L would be gross")
set -a; . ./.env 2>/dev/null; set +a
exp=$(PGPASSWORD="${PG_PASSWORD:-}" psql -X -Atq -h "${PG_HOST:-127.0.0.1}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -c "select expiry_date||'|'||coalesce(roll_date::text,'') from contracts where contract='$ct'" 2>/dev/null)
today=$(TZ=America/New_York date +%F)
if [[ -n "$exp" ]]; then e=${exp%%|*}; r=${exp##*|}; [[ "$e" > "$today" ]] || problems+=("contract $ct expired $e"); [[ -z "$r" || "$r" > "$today" ]] || problems+=("contract $ct past roll date $r"); fi
grep -aq 'ORDER_PLANT login OK' <(tail -n +"${start_line:-1}" "$L" 2>/dev/null) || problems+=("no ORDER_PLANT login since the executor started")
room=$(tail -n +"${start_line:-1}" "$L" 2>/dev/null | grep -a 'BROKER-HWM\] balance=' | tail -1 | grep -oE 'room=[-0-9.]+' | cut -d= -f2)
stop_usd=$(python3 -c "print(round(float('$(cj sl_points)')*float('${pv:-0}')*float('${qty:-0}'),2))" 2>/dev/null)
[[ -n "$room" ]] && python3 -c "import sys; sys.exit(0 if float('$room') >= float('${stop_usd:-0}') else 1)" || { [[ -n "$room" ]] && problems+=("drawdown room \$$room < one stop \$$stop_usd"); }
summary="$sym $ct qty=$qty stop=\$$stop_usd room=\$${room:-?} trades<=$(cj max_daily_trades) entries<$(cj last_entry_hour):$(printf %02d "$(cj last_entry_min 2>/dev/null || echo 0)") ET"
read -r avail < <(awk '/^MemAvailable:/{print int($2/1024)}' /proc/meminfo)
(( avail < 3072 )) && problems+=("only ${avail} MB RAM available — close browser tabs")
if (( ${#problems[@]} )); then
  notify "🔴 pre-RTH check ($ACC): $(IFS='; '; echo "${problems[*]}") — fix before 09:30 ET"; exit 1
fi
notify "🟢 pre-RTH check ($ACC): executor up, flat-checked, ${broker}, ${avail} MB free · $summary — ready for 09:30 ET"
