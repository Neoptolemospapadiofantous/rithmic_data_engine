#!/bin/bash
# Continuous 24/7 session runner.
# Runs nq_executor for each RTH session, then sleeps until the next trading day open.
#
# Usage: run_continuous.sh <account>
# Example: run_continuous.sh tradeify
#
# Managed by nq_executor-24x7@<account>.service
# Stop via: sudo systemctl stop nq_executor-24x7@<account>

set -euo pipefail

ACCOUNT="${1:-tradeify}"
BASE_DIR="/home/opc/rithmic_engine"
CONFIG_FILE="${BASE_DIR}/config/${ACCOUNT}_config.json"
BINARY="${BASE_DIR}/build/nq_executor"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] [24x7/${ACCOUNT}] $*"; }

patch_rth() {
    # Ensure session timing is RTH 9:30 ET and dry_run is off.
    # Preserves all other config keys (orb_minutes, sl_points, etc.)
    python3 - <<'PYEOF'
import json, sys, os

cfg = os.environ["_CFG"]
with open(cfg) as f:
    c = json.load(f)
c["session_open_hour"] = 9
c["session_open_min"]  = 30
c["dry_run"]           = False
with open(cfg, "w") as f:
    json.dump(c, f, indent=2)
print(f"  orb_minutes={c.get('orb_minutes',5)}  sl={c.get('sl_points','?')}  be_trigger={c.get('trail_be_trigger','?')}")
PYEOF
}

secs_until_next_open() {
    # Returns seconds until 9:25 ET on the next weekday
    python3 - <<'PYEOF'
import datetime, zoneinfo
tz = zoneinfo.ZoneInfo("America/New_York")
now = datetime.datetime.now(tz)
target = now.replace(hour=9, minute=25, second=0, microsecond=0)
if now >= target:
    target += datetime.timedelta(days=1)
# Skip Saturday (5) and Sunday (6)
while target.weekday() >= 5:
    target += datetime.timedelta(days=1)
secs = max(30, int((target - now).total_seconds()))
print(secs)
PYEOF
}

log "Continuous mode starting for account=${ACCOUNT}"

while true; do
    log "Patching config for RTH 9:30 ET..."
    export _CFG="$CONFIG_FILE"
    patch_rth || { log "Config patch failed — will retry next cycle"; }

    log "Starting nq_executor..."
    "$BINARY" --config "$CONFIG_FILE" || true
    log "Executor exited (exit=$?)"

    WAIT=$(secs_until_next_open)
    WAIT_H=$(( WAIT / 3600 ))
    WAIT_M=$(( (WAIT % 3600) / 60 ))
    log "Session complete. Next RTH open in ${WAIT_H}h${WAIT_M}m — sleeping..."
    sleep "$WAIT"
done
