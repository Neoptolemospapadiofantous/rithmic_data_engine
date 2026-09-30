#!/usr/bin/env bash
# Live fire drill — reproduce the 2026-09-23 failure shape on the REAL account with
# ONE contract and prove the executor closes it. Two modes:
#   recover : executor places an untracked BUY 1, its guards + PNL-plant reconciliation
#             must unwind it within the grace window (default 5 s). Exit 0 = PASS.
#   crash   : same, but the process is SIGKILLed 2 s after the orphan order (before the
#             unwind) and the normal unit is started: the startup snapshot must find the
#             ghost position and unwind it. Exit 0 = PASS.
# Cost: one MNQ round trip (~$1–3 slippage + commission). Run only when the account is
# tradable and FLAT, the unit is STOPPED and NO_DEPLOY is present.
#   scripts/drill_orphan.sh tradeify recover
#   scripts/drill_orphan.sh tradeify crash
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
ACC="${1:-tradeify}"; MODE="${2:-recover}"
LOG="data/logs/nq_executor_${ACC}.log"; UNIT="nq-executor-local@${ACC}"; DRILL="nq-drill-${ACC}"
[[ "$MODE" == recover || "$MODE" == crash ]] || { echo "mode must be recover|crash"; exit 2; }
[[ -f NO_DEPLOY ]] || { echo "refusing: NO_DEPLOY absent — stop the unit and 'touch NO_DEPLOY' first"; exit 2; }
systemctl --user is-active "$UNIT" >/dev/null 2>&1 && { echo "refusing: $UNIT is active"; exit 2; }
systemctl --user stop "$DRILL" >/dev/null 2>&1 || true
START_LINE=$(wc -l < "$LOG" 2>/dev/null || echo 0)
since() { tail -n +"$((START_LINE + 1))" "$LOG" 2>/dev/null; }
wait_for() { # pattern timeout_s
  local t=0; while (( t < $2 )); do since | grep -q -E "$1" && return 0; sleep 1; ((t++)); done; return 1; }
echo "drill[$MODE] account=$ACC — launching executor with --drill orphan (transient unit $DRILL)"
systemd-run --user --unit="$DRILL" --collect -p WorkingDirectory="$REPO" \
  -p EnvironmentFile="$REPO/.env" -p EnvironmentFile="-$REPO/.env.$ACC" \
  -p StandardOutput="append:$REPO/$LOG" -p StandardError="append:$REPO/$LOG" \
  "$REPO/build/nq_executor" --config "config/${ACC}_config.json" --drill orphan >/dev/null || { echo "systemd-run failed"; exit 2; }
wait_for 'PNL_PLANT position subscription OK' 60 || { echo "FAIL: PNL plant never subscribed"; systemctl --user stop "$DRILL"; exit 2; }
wait_for '\[DRILL\] orphan order sent' 60       || { echo "FAIL: orphan order never sent (exchange not confirmed flat? see $LOG)"; systemctl --user stop "$DRILL"; exit 2; }
echo "orphan order sent"
if [[ "$MODE" == crash ]]; then
  sleep 2
  echo "SIGKILL the executor before it can unwind (simulated crash)"
  systemctl --user kill -s KILL "$DRILL"; sleep 1; systemctl --user stop "$DRILL" >/dev/null 2>&1 || true
  rm -f NO_DEPLOY; systemctl --user start "$UNIT"
  if wait_for '\[STARTUP-RECON\] unwind sent|\[NET-RECON\] unwind sent' 90 && wait_for 'consistent again|net_qty=0 — exchange confirmed FLAT' 90; then
    echo "PASS[crash]: ghost position found on restart and closed"; RC=0
  else
    echo "FAIL[crash]: no unwind after restart — CLOSE THE POSITION IN RTRADER"; RC=2
  fi
  systemctl --user stop "$UNIT"; touch NO_DEPLOY
else
  if wait_for '\[DRILL\] PASS' 120; then echo "PASS[recover]: $(since | grep -o '\[DRILL\] PASS.*' | tail -1)"; RC=0
  elif since | grep -q '\[DRILL\] FAIL'; then echo "FAIL[recover]: $(since | grep -o '\[DRILL\] FAIL.*' | tail -1)"; RC=2
  else echo "FAIL[recover]: no verdict within 120 s — check $LOG and RTrader"; RC=2; fi
  systemctl --user stop "$DRILL" >/dev/null 2>&1 || true
fi
echo "--- drill log excerpt ---"; since | grep -E 'DRILL|unowned|GHOST|NET-RECON|STARTUP-RECON|POS-UPDATE|BROKER' | cut -c1-170 | tail -20
exit $RC
