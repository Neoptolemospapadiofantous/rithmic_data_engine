#!/usr/bin/env bash
# order_probe.sh — prove the live account can place an order, WITHOUT a fill.
#
# Founder 2026-10-07: "test that we can place a trade". Stops the live executor, runs
# `nq_executor --drill probe` as a transient unit: once the exchange confirms the account FLAT it
# sends ONE BUY LIMIT 100 pts below the last price (cannot fill in the seconds it rests), waits for
# the exchange to accept it (status "open"), cancels it and waits for the cancel to complete. Then it
# restarts the live executor — always, pass or fail — and checks it came back.
#
#   scripts/order_probe.sh [account]            (default tradeify)
#   scripts/order_probe.sh tradeify --force     allow inside 09:25-10:35 ET (the ORB entry window)
#
# Proves: login + account permission + the configured contract (trade_contract) + trade route +
# the cancel path. Costs nothing unless the market moves 100 pts in a few seconds (then the
# executor's net reconciler unwinds it, like the orphan drill). Exit 0 = PASS.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
ACC="${1:-tradeify}"; FORCE=0; [[ "${2:-}" == "--force" ]] && FORCE=1
LOG="data/logs/nq_executor_${ACC}.log"; UNIT="nq-executor-local@${ACC}"; PROBE="nq-probe-${ACC}"
CFG="config/${ACC}_config.json"

[[ -f "$CFG" ]] || { echo "no $CFG"; exit 2; }
hm=$((10#$(TZ=America/New_York date +%H%M))); dow=$(TZ=America/New_York date +%u)
if (( !FORCE && dow <= 5 && hm >= 925 && hm < 1035 )); then
  echo "refusing: $(TZ=America/New_York date +%H:%M) ET is inside the ORB entry window (09:25-10:35) — rerun later or pass --force"; exit 2
fi
[[ -f NO_DEPLOY ]] && { echo "refusing: NO_DEPLOY already present — someone else holds the account"; exit 2; }
last_pos=$(grep -a 'pos=' "$LOG" 2>/dev/null | tail -1)
[[ "$last_pos" == *"pos=FLAT"* ]] || { echo "refusing: the live executor is not FLAT: ${last_pos:0:160}"; exit 2; }
last_net=$(grep -a '\[BROKER\] balance' "$LOG" | tail -1 | grep -oE 'exchange_net=-?[0-9]+' | cut -d= -f2)
[[ "${last_net:-0}" == 0 ]] || { echo "refusing: broker reports exchange_net=$last_net"; exit 2; }

restore() {
  systemctl --user stop "$PROBE" >/dev/null 2>&1 || true
  rm -f NO_DEPLOY
  systemctl --user start "$UNIT"
  sleep 20
  if systemctl --user is-active --quiet "$UNIT" && tail -n 200 "$LOG" | grep -aq 'BROKER-HWM\] balance='; then
    echo "live executor restored: $(tail -n 200 "$LOG" | grep -a 'BROKER-HWM\] balance=' | tail -1 | cut -c16-150)"
  else
    echo "WARNING: $UNIT did not come back cleanly — check: systemctl --user status $UNIT"
  fi
}
trap restore EXIT

echo "probe account=$ACC contract=$(jq -r '.trade_contract // .symbol' "$CFG") — stopping $UNIT"
systemctl --user stop "$UNIT"; touch NO_DEPLOY
START_LINE=$(wc -l < "$LOG")
since() { tail -n +"$((START_LINE + 1))" "$LOG" 2>/dev/null; }
wait_for() { local t=0; while (( t < $2 )); do since | grep -aqE "$1" && return 0; sleep 1; ((t++)); done; return 1; }

systemd-run --user --unit="$PROBE" --collect -p WorkingDirectory="$REPO" \
  -p EnvironmentFile="$REPO/.env" -p EnvironmentFile="-$REPO/.env.$ACC" \
  -p StandardOutput="append:$REPO/$LOG" -p StandardError="append:$REPO/$LOG" \
  "$REPO/build/nq_executor" --config "$CFG" --drill probe >/dev/null || { echo "systemd-run failed"; exit 2; }

RC=2
if ! wait_for '\[DRILL\] probe order sent|\[DRILL\] probe FAIL' 90; then
  echo "FAIL: the probe order was never sent (login / flat confirmation?) — see $LOG"
elif wait_for '\[DRILL\] PASS|\[DRILL\] probe FAIL' 75 && since | grep -aq '\[DRILL\] PASS'; then
  echo "PASS: $(since | grep -ao '\[DRILL\] PASS.*' | tail -1)"; RC=0
else
  echo "FAIL: $(since | grep -ao '\[DRILL\] probe FAIL.*' | tail -1)"
fi
echo "--- probe log ---"
since | grep -aE '\[DRILL\]|ExchangeOrderNotification|login OK|exchange confirmed FLAT|RequestCancelOrder|BROKER\] balance' | cut -c16-200 | tail -20
exit $RC
