#!/usr/bin/env bash
# overnight_live_test.sh — unattended live test right after Tradeify's daily reset
# (17:00 CT = 18:00 ET = 01:00 Cyprus in summer), then hand back to production.
#
#   1. pre-flight   : stack index clean, no executor running, NO_DEPLOY present, feed fresh
#   2. drill        : scripts/drill_orphan.sh <acct> recover — the orphan guards must PASS
#   3. ORB test     : nq-executor-local@<acct>_orbtest  (18:05 ET range, 2 MNQ, flat by 18:50)
#   4. trend test   : nq-executor-local@<acct>_trend    (supertrend 1m, 19:00–20:00 ET, 2 MNQ)
#   5. production   : nq-executor-local@<acct>          (09:30 ET ORB for the next RTH session)
#
# Every step must prove the executor sees the real account (PNL plant + [BROKER] line) and that
# the exchange is flat before the next one starts. Any failure: stop everything, touch NO_DEPLOY,
# alert through grid-notify, exit 1 — production is NOT started after a failure.
# Scheduled with systemd-run (see RUNBOOK); log: data/logs/overnight_live_test.log.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
ACC="${1:-tradeify}"
OUT="data/logs/overnight_live_test.log"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq)

say()    { echo "[$(date '+%F %T') ET $(TZ=America/New_York date +%H:%M)] $*" | tee -a "$OUT"; }
notify() { for b in "$HOME/.local/bin/grid-notify" "$HOME/.config/ecosystem/bin/grid-notify"; do
             [[ -x "$b" ]] && { "$b" "$1" >/dev/null 2>&1 || true; return; }; done; }
et_hm()  { echo $((10#$(TZ=America/New_York date +%H%M))); }
wait_until_et() { say "waiting until $1 ET"; while (( $(et_hm) < $1 )); do sleep 15; done; }
unit()   { echo "nq-executor-local@$1"; }
logf()   { echo "data/logs/nq_executor_$1.log"; }

stop_inst() { # instance — SIGTERM flattens; verify the process is really gone
  local u; u=$(unit "$1")
  systemctl --user stop "$u" 2>/dev/null
  for _ in $(seq 60); do
    [[ "$(systemctl --user show -p MainPID --value "$u" 2>/dev/null)" == 0 ]] && ! pgrep -x nq_executor >/dev/null && return 0
    sleep 1
  done
  say "WARN: nq_executor still running 60 s after stopping $1"; return 1; }

abort() {
  say "ABORT: $*"
  for i in "${ACC}_orbtest" "${ACC}_trend" "$ACC"; do stop_inst "$i"; done
  touch NO_DEPLOY
  notify "🔴 overnight live test ABORTED ($ACC): $* — NO_DEPLOY set, production NOT started. Check RTrader + $OUT"
  exit 1; }

# start an instance and prove it sees the account: PNL plant up, a [BROKER] line, day P&L reset
start_inst() { # instance
  local u l start t line dpnl; u=$(unit "$1"); l=$(logf "$1")
  start=$(wc -l < "$l" 2>/dev/null || echo 0)
  rm -f NO_DEPLOY
  systemctl --user start "$u" || abort "systemctl start $u failed"
  for t in $(seq 90); do
    line=$(tail -n +"$((start + 1))" "$l" 2>/dev/null | grep -a '\[BROKER\]' | tail -1)
    [[ -n "$line" ]] && break
    tail -n +"$((start + 1))" "$l" 2>/dev/null | grep -aq 'Instance lock REFUSED' && abort "$1: instance lock refused (another executor on $ACC?)"
    sleep 1
  done
  [[ -n "$line" ]] || abort "$1: no [BROKER] line within 90 s — no position truth"
  tail -n +"$((start + 1))" "$l" | grep -aq 'PNL_PLANT position subscription OK' || abort "$1: PNL plant not subscribed"
  dpnl=$(grep -o 'day_pnl=[-0-9.]*' <<<"$line" | cut -d= -f2)
  awk -v d="$dpnl" 'BEGIN{exit !(d > -450)}' || abort "$1: broker day_pnl=$dpnl — Tradeify reset not seen yet"
  grep -q 'exchange_net=0' <<<"$line" || abort "$1: exchange not flat at start: $line"
  say "$1 up: $line"; }

# after a test window: log the last broker line. Informational only — the stop that follows
# flattens (SIGTERM), and the NEXT start_inst takes a fresh exchange snapshot and refuses to
# continue unless the account is flat, so a stale line here can never hide an open position.
check_flat() { # instance
  local line; line=$(grep -a '\[BROKER\]' "$(logf "$1")" | tail -1)
  say "$1 last broker line: $line"
  grep -q 'exchange_net=0' <<<"$line" || say "WARN: $1 last broker line not flat — relying on the stop's flatten + next snapshot"; }

trades_summary() {
  "${PSQL[@]}" -c "SELECT strategy||' '||count(*)||' trades '||round(coalesce(sum(pnl_usd),0)::numeric,2)||' USD'
                   FROM live_trades WHERE account_label='$ACC' AND trade_date='$1'::date GROUP BY strategy" 2>/dev/null | paste -sd';'; }

say "=== overnight live test start (account $ACC) ==="
# ── 1. pre-flight ─────────────────────────────────────────────────────────────
[[ -f "config/${ACC}_orbtest_config.json" ]] || { say "config/${ACC}_orbtest_config.json missing — it is archived when not in use: git mv config/archived/${ACC}_orbtest_config.json config/ && ln -sf .env.${ACC} .env.${ACC}_orbtest"; exit 1; }
[[ -f "config/${ACC}_trend_config.json" ]] || { say "config/${ACC}_trend_config.json missing — archived 2026-09-25 (TREND_ST off the live board): git mv config/archived/${ACC}_trend_config.json config/ && ln -sf .env.${ACC} .env.${ACC}_trend"; exit 1; }
h=$(et_hm); (( h >= 1700 && h < 1830 )) || { say "not started in the 17:00–18:30 ET slot (now $h) — refusing"; \
  notify "🔴 overnight live test ($ACC) did not run: started at $h ET, outside its slot"; exit 1; }
wait_until_et 1802
pgrep -x nq_executor >/dev/null && abort "an nq_executor is already running: $(pgrep -ax nq_executor)"
[[ -f NO_DEPLOY ]] || touch NO_DEPLOY                 # the drill requires the lock
bash scripts/stack_status.sh "$ACC" >> "$OUT" 2>&1 || abort "stack-status reported a problem (see log)"
for _ in $(seq 20); do
  age=$("${PSQL[@]}" -c "SELECT COALESCE(EXTRACT(EPOCH FROM now()-max(ts_event)),1e9)::int FROM ticks WHERE symbol='NQ' AND ts_event > now()-interval '1 hour'")
  (( ${age:-999999} < 60 )) && break; sleep 15
done
(( ${age:-999999} < 60 )) || abort "no fresh NQ ticks after the Globex reopen (age ${age}s)"
say "pre-flight OK (tick age ${age}s)"
TD=$(TZ=America/New_York date -d tomorrow +%F)       # after 18:00 ET the trading date is tomorrow

# ── 2. orphan drill ──────────────────────────────────────────────────────────
say "drill: recover"
if bash scripts/drill_orphan.sh "$ACC" recover >> "$OUT" 2>&1; then say "drill PASS"
else abort "orphan drill FAILED"; fi
notify "🟢 overnight test ($ACC): orphan drill PASS — starting live ORB test (18:05 ET, 2 MNQ)"

# ── 3. ORB test ──────────────────────────────────────────────────────────────
start_inst "${ACC}_orbtest"
wait_until_et 1852
check_flat "${ACC}_orbtest"
stop_inst "${ACC}_orbtest" || abort "ORB test executor would not stop"
notify "🟢 overnight test ($ACC): ORB test done — $(trades_summary "$TD"). Starting trend test (19:00–20:00 ET)"

# ── 4. trend test ────────────────────────────────────────────────────────────
start_inst "${ACC}_trend"
wait_until_et 2004
check_flat "${ACC}_trend"
stop_inst "${ACC}_trend" || abort "trend test executor would not stop"

# ── 5. hand back to production (09:30 ET ORB) ────────────────────────────────
grep -q '"session_open_hour": 9,' "config/${ACC}_config.json" && grep -q '"session_open_min": 30,' "config/${ACC}_config.json" \
  || abort "config/${ACC}_config.json no longer opens at 09:30 — production not started"
start_inst "$ACC"
say "=== done: production running for the $TD RTH session ==="
notify "🟢 overnight live test ($ACC) COMPLETE — $(trades_summary "$TD"). Production ORB running for $TD 09:30 ET, flat, broker feed OK."
exit 0
