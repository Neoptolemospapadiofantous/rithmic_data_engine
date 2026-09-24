#!/usr/bin/env bash
# strategy_handoff.sh — hand the account from one live executor to another during RTH,
# ONE executor at a time (the instance lock is account-wide; two executors on one account
# would each unwind the other's position as a mismatch).
#
#   scripts/strategy_handoff.sh <acct> <from_inst> <to_inst> [switch_et=1000] [hard_et=1030] [back_et=1600]
#   e.g. scripts/strategy_handoff.sh tradeify tradeify tradeify_trend
#
#   1. wait until <from_inst> is DONE: it logged "Daily trade limit reached", or it is
#      FLAT at/after switch_et (ET HHMM). A trade still open at switch_et is allowed to
#      finish until hard_et; then the stop's SIGTERM flattens it.
#   2. stop <from_inst> (verified gone), start <to_inst>; it must show PNL plant + a
#      [BROKER] line with exchange_net=0 within 90 s, else stop it and alert.
#   3. at back_et, stop <to_inst> and restart <from_inst> so the next session starts as
#      configured (production ORB for tomorrow's 09:30).
# Log: data/logs/strategy_handoff.log; every step goes to Telegram via grid-notify.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
ACC="${1:?account}"; FROM="${2:?from instance}"; TO="${3:?to instance}"
SWITCH_ET="${4:-1000}"; HARD_ET="${5:-1030}"; BACK_ET="${6:-1600}"
OUT="data/logs/strategy_handoff.log"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq)

say()    { echo "[$(date '+%F %T') ET $(TZ=America/New_York date +%H:%M)] $*" | tee -a "$OUT"; }
notify() { for b in "$HOME/.local/bin/grid-notify" "$HOME/.config/ecosystem/bin/grid-notify"; do
             [[ -x "$b" ]] && { "$b" "$1" >/dev/null 2>&1 || true; return; }; done; }
et_hm()  { echo $((10#$(TZ=America/New_York date +%H%M))); }
unit()   { echo "nq-executor-local@$1"; }
logf()   { echo "data/logs/nq_executor_$1.log"; }
tag_of() { local t; t=$(grep -o '"strategy": *"[^"]*"' "config/$1_config.json" | sed 's/.*"\([^"]*\)"$/\1/' | head -1)
           echo "${t:-ORB}"; }   # OrbConfig default tag

stop_inst() {
  local u; u=$(unit "$1"); systemctl --user stop "$u" 2>/dev/null
  for _ in $(seq 60); do
    [[ "$(systemctl --user show -p MainPID --value "$u")" == 0 ]] && ! pgrep -x nq_executor >/dev/null && return 0
    sleep 1
  done; return 1; }

start_inst() { # prove position truth + flat, else stop it again
  local u l s line; u=$(unit "$1"); l=$(logf "$1"); s=$(wc -l < "$l" 2>/dev/null || echo 0)
  systemctl --user start "$u" || return 1
  for _ in $(seq 90); do
    line=$(tail -n +"$((s + 1))" "$l" 2>/dev/null | grep -a '\[BROKER\]' | tail -1)
    [[ -n "$line" ]] && break; sleep 1
  done
  if [[ -z "$line" ]] || ! tail -n +"$((s + 1))" "$l" | grep -aq 'PNL_PLANT position subscription OK' \
     || ! grep -q 'exchange_net=0' <<<"$line"; then
    say "ERROR: $1 did not come up clean (${line:-no [BROKER] line}) — stopping it"; stop_inst "$1"; return 1
  fi
  say "$1 up: $line"; return 0; }

# is <inst> done and clean? its live_position row (by its own strategy tag) AND the last broker line agree
is_flat() {
  local st net pend; st=$("${PSQL[@]}" -c "SELECT state FROM live_position WHERE account_label='$ACC'
          AND strategy='$(tag_of "$1")' ORDER BY last_updated DESC LIMIT 1")
  net=$(grep -a '\[BROKER\]' "$(logf "$1")" | tail -1 | grep -o 'exchange_net=[-0-9]*' | cut -d= -f2)
  # …and no stop cancel still unconfirmed (a stopped process no longer reads its ACK)
  pend=$("${PSQL[@]}" -c "SELECT count(*) FROM pending_stop_cancels WHERE account_label='$ACC'")
  [[ "$st" == FLAT && "${net:-1}" == 0 && "${pend:-1}" == 0 ]]; }

say "=== handoff armed: $FROM → $TO (switch ${SWITCH_ET} ET, hard ${HARD_ET} ET, back ${BACK_ET} ET) ==="
START_LINE=$(wc -l < "$(logf "$FROM")" 2>/dev/null || echo 0)
reason=""
while :; do
  now=$(et_hm)
  if tail -n +"$((START_LINE + 1))" "$(logf "$FROM")" 2>/dev/null | grep -aq 'Daily trade limit reached'; then
    reason="daily trade limit reached"; break; fi
  if (( now >= SWITCH_ET )) && is_flat "$FROM"; then reason="${SWITCH_ET} ET and flat"; break; fi
  if (( now >= HARD_ET )); then reason="hard deadline ${HARD_ET} ET (stop flattens any open trade)"; break; fi
  sleep 10
done
say "handoff trigger: $reason"
stop_inst "$FROM" || { say "ABORT: $FROM would not stop"; notify "🔴 handoff ($ACC): $FROM would not stop — check RTrader"; exit 1; }
say "$FROM stopped"
if start_inst "$TO"; then
  notify "🟢 handoff ($ACC): $FROM done ($reason) → $TO LIVE (its window: config/${TO}_config.json; supertrend flattens 12:00 ET = 19:00 CY)"
else
  touch NO_DEPLOY
  notify "🔴 handoff ($ACC): $TO failed to start clean — NO executor on the account, NO_DEPLOY set. Check $OUT"
  exit 1
fi

# hand back to the from-instance for the next session
while (( $(et_hm) < BACK_ET )); do sleep 30; done
stop_inst "$TO" || { notify "🔴 handoff ($ACC): $TO would not stop at ${BACK_ET} ET"; exit 1; }
if start_inst "$FROM"; then
  notify "🟢 handoff ($ACC): $TO stopped after RTH; $FROM restored for the next session"
else
  notify "🔴 handoff ($ACC): $FROM failed to restart after RTH — no executor running. Check $OUT"; exit 1
fi
say "=== handoff complete ==="
