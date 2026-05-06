#!/bin/bash
# Continuous / cycling session runner.
#
# Two modes (selected by cycle_mode in the config JSON):
#
#   RTH mode (cycle_mode=false, default):
#     Patches session_open=9:30 ET, runs executor through the RTH session,
#     then sleeps until 9:25 ET the next weekday.
#
#   Cycling mode (cycle_mode=true):
#     Sets session_open=NOW() ET and cycle_start_epoch=<unix_now> each loop.
#     Executor exits cleanly after max_daily_trades trades (position flat).
#     Wrapper immediately restarts — no RTH wait, runs 24/7.
#     Trade history is preserved; the cycle_start_epoch timestamp scopes the
#     trade count so each cycle starts from 0.
#
# Usage: run_continuous.sh <account>
# Managed by nq_executor-24x7@<account>.service
# Stop via: sudo systemctl stop nq_executor-24x7@<account>

set -euo pipefail

ACCOUNT="${1:-tradeify}"
BASE_DIR="/home/opc/rithmic_engine"
CONFIG_FILE="${BASE_DIR}/config/${ACCOUNT}_config.json"
BINARY="${BASE_DIR}/build/nq_executor"
CYCLE_RESTART_DELAY=30  # seconds between cycle end and next start

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] [24x7/${ACCOUNT}] $*"; }

# ── Config readers ────────────────────────────────────────────────────────────
is_cycle_mode() {
    python3 -c "
import json, sys
with open('${CONFIG_FILE}') as f:
    c = json.load(f)
print('true' if c.get('cycle_mode', False) else 'false')
" 2>/dev/null || echo 'false'
}

# ── Config patchers ───────────────────────────────────────────────────────────
patch_rth() {
    python3 - <<'PYEOF'
import json, os
cfg = os.environ["_CFG"]
with open(cfg) as f:
    c = json.load(f)
c["session_open_hour"] = 9
c["session_open_min"]  = 30
c["dry_run"]           = False
c["cycle_mode"]        = False
with open(cfg, "w") as f:
    json.dump(c, f, indent=2)
print(f"  RTH 9:30 ET  orb_minutes={c.get('orb_minutes',5)}  sl={c.get('sl_points','?')}")
PYEOF
}

patch_cycle() {
    # Set session_open = NOW() ET and stamp cycle_start_epoch = now Unix.
    # Prints: cycle_timeout_secs on the last line (read by caller).
    python3 - <<'PYEOF'
import json, os, time
from datetime import datetime
try:
    from zoneinfo import ZoneInfo
    tz = ZoneInfo("America/New_York")
except ImportError:
    import pytz
    tz = pytz.timezone("America/New_York")

cfg = os.environ["_CFG"]
now_unix = int(time.time())
now_et   = datetime.now(tz)

with open(cfg) as f:
    c = json.load(f)
c["session_open_hour"]  = now_et.hour
c["session_open_min"]   = now_et.minute
c["cycle_start_epoch"]  = now_unix
c["dry_run"]            = False
c["cycle_mode"]         = True
with open(cfg, "w") as f:
    json.dump(c, f, indent=2)

orb = c.get("orb_minutes", 5)
max_trades = c.get("max_daily_trades", 3)
# cycle_timeout_mins: explicit config key, or default = orb window + 30 min buffer
timeout_mins = int(c.get("cycle_timeout_mins", orb + 15))
timeout_secs = timeout_mins * 60

print(f"  cycle start {now_et.strftime('%H:%M:%S')} ET  epoch={now_unix}"
      f"  orb={orb}m  max_trades={max_trades}  timeout={timeout_mins}m")
print(f"TIMEOUT_SECS={timeout_secs}")
PYEOF
}

# ── RTH sleep helper ──────────────────────────────────────────────────────────
secs_until_next_open() {
    python3 - <<'PYEOF'
import datetime
try:
    from zoneinfo import ZoneInfo
    tz = ZoneInfo("America/New_York")
except ImportError:
    import pytz
    tz = pytz.timezone("America/New_York")
now = datetime.datetime.now(tz)
target = now.replace(hour=9, minute=25, second=0, microsecond=0)
if now >= target:
    target += datetime.timedelta(days=1)
while target.weekday() >= 5:
    target += datetime.timedelta(days=1)
secs = max(30, int((target - now).total_seconds()))
print(secs)
PYEOF
}

# ── Main loop ─────────────────────────────────────────────────────────────────
log "Continuous mode starting for account=${ACCOUNT}"

while true; do
    export _CFG="$CONFIG_FILE"

    MODE=$(is_cycle_mode)

    if [[ "$MODE" == "true" ]]; then
        # ── Cycling mode ──────────────────────────────────────────
        log "Cycle mode: patching config with current time..."
        PATCH_OUT=$(patch_cycle 2>&1) || { log "Config patch failed — retrying in 10s"; sleep 10; continue; }
        echo "$PATCH_OUT"

        # Extract timeout from patch output (last line: TIMEOUT_SECS=N)
        CYCLE_TIMEOUT=$(echo "$PATCH_OUT" | grep "^TIMEOUT_SECS=" | cut -d= -f2)
        CYCLE_TIMEOUT="${CYCLE_TIMEOUT:-2100}"  # fallback 35 min

        log "Starting executor (cycle, timeout=${CYCLE_TIMEOUT}s)..."
        # timeout -k 60 sends SIGTERM at CYCLE_TIMEOUT, then SIGKILL after 60s
        # if executor didn't flatten and exit cleanly (handles stuck/no-signal cycles)
        timeout -k 60 "${CYCLE_TIMEOUT}" "$BINARY" --config "$CONFIG_FILE" &
        EXEC_PID=$!
        CYCLE_START_TS=$(date +%s)

        # No-trade timer: log elapsed/remaining every 60s while executor is alive
        while kill -0 "$EXEC_PID" 2>/dev/null; do
            sleep 60
            if kill -0 "$EXEC_PID" 2>/dev/null; then
                ELAPSED=$(( $(date +%s) - CYCLE_START_TS ))
                REMAINING=$(( CYCLE_TIMEOUT - ELAPSED ))
                log "Cycle alive: ${ELAPSED}s elapsed — ${REMAINING}s until forced restart (no trade yet)"
            fi
        done
        wait "$EXEC_PID" 2>/dev/null || true
        EXIT_CODE=$?

        if [[ $EXIT_CODE -eq 124 ]]; then
            log "Cycle timed out after ${CYCLE_TIMEOUT}s (no trades or stuck) — forcing next cycle"
        else
            log "Executor exited (exit=${EXIT_CODE}) — cycle complete"
        fi

        # Clear last cycle's ORB from both tables so the UI immediately shows
        # "Building range" and the next executor builds a fresh range.
        PGPASSWORD=testpass123 psql -h 127.0.0.1 -U rithmic_user -d rithmic -q \
            -c "UPDATE live_sessions SET orb_high=0, orb_low=0, orb_range=0
                    WHERE account_label='${ACCOUNT}' AND session_date=CURRENT_DATE;
                UPDATE live_position SET orb_set=false, orb_high=NULL, orb_low=NULL
                    WHERE account_label='${ACCOUNT}' AND session_date=CURRENT_DATE;" \
            2>/dev/null || true

        log "Waiting ${CYCLE_RESTART_DELAY}s before next cycle..."
        sleep "${CYCLE_RESTART_DELAY}"
    else
        # ── RTH mode (default) ────────────────────────────────────
        log "RTH mode: patching config for 9:30 ET..."
        patch_rth || { log "Config patch failed — will retry next cycle"; }

        log "Starting executor (RTH)..."
        "$BINARY" --config "$CONFIG_FILE" || true
        log "Executor exited"

        WAIT=$(secs_until_next_open)
        WAIT_H=$(( WAIT / 3600 ))
        WAIT_M=$(( (WAIT % 3600) / 60 ))
        log "RTH session complete. Next open in ${WAIT_H}h${WAIT_M}m — sleeping..."
        sleep "$WAIT"
    fi
done
