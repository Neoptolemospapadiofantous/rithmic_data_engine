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
print(f"  cycle start {now_et.strftime('%H:%M:%S')} ET  epoch={now_unix}"
      f"  orb={c.get('orb_minutes',5)}m  max_trades={c.get('max_daily_trades',3)}")
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
        patch_cycle || { log "Config patch failed — retrying in 10s"; sleep 10; continue; }

        log "Starting executor (cycle)..."
        "$BINARY" --config "$CONFIG_FILE" || true
        EXIT_CODE=$?
        log "Executor exited (exit=${EXIT_CODE}) — cycle complete"

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
