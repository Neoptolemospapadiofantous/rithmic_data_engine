#!/usr/bin/env bash
# mm_live_watch.sh — is the LIVE Lighter market maker (mm_live) still running? Meant for deploy/mm-live-watch.timer
# (every minute); safe to run by hand.
#
#   scripts/mm_live_watch.sh            check, alert on changes via grid-notify
#   scripts/mm_live_watch.sh --dry-run  print what it sees and what it would send; no alert, no state change
#
# Why (2026-10-28): the box reset at 12:24 UTC, mm-live@avax was started by hand (unit disabled) so it did not come
# back, and it sat down ~9 h holding 1.23 AVAX with nothing reporting it — recorder_watch skips disabled units, and
# a disabled unit is exactly how the live maker runs.
#
# An instance is DOWN when its status file (engine.status_file of config/mm_live_<name>.json, written every 5 s)
# is older than MAX_AGE seconds. The alert names the unit state and the last position/collateral the file shows.
# Switched off on purpose = the config's no_deploy_file (data/mm_live/NO_DEPLOY_MM) exists: then nothing is checked.
# An instance whose status file never existed is skipped (never run).
#
# Alerts once: DOWN when it appears, "back up" when it clears; nothing repeats while it lasts. FAIL-OPEN: a missing
# jq / grid-notify never fails this script; it always exits 0.
#
# Env (defaults): MM_LIVE_WATCH_INSTANCES ("avax"), MM_LIVE_WATCH_MAX_AGE (60),
# MM_LIVE_WATCH_STATE (data/mm_live_watch/state.json), GRID_NOTIFY (~/.local/bin/grid-notify).

set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

INSTANCES="${MM_LIVE_WATCH_INSTANCES:-avax}"
MAX_AGE="${MM_LIVE_WATCH_MAX_AGE:-60}"
STATE="${MM_LIVE_WATCH_STATE:-$ROOT/data/mm_live_watch/state.json}"
NOTIFY="${GRID_NOTIFY:-$HOME/.local/bin/grid-notify}"

command -v jq >/dev/null 2>&1 || { echo "mm_live_watch: jq missing — nothing checked"; exit 0; }
mkdir -p "$(dirname "$STATE")" 2>/dev/null
[ -s "$STATE" ] || echo '{}' > "$STATE" 2>/dev/null
now=$(date +%s)

send() {
    if [ "$DRY" = 1 ]; then echo "WOULD SEND: $1"; return; fi
    [ -x "$NOTIFY" ] && "$NOTIFY" "$1" >/dev/null 2>&1
    return 0
}

for name in $INSTANCES; do
    cfg="$ROOT/config/mm_live_${name}.json"
    [ -f "$cfg" ] || { echo "$name: no config $cfg — skipped"; continue; }
    status=$(jq -r '.engine.status_file // empty' "$cfg")
    nodeploy=$(jq -r '.no_deploy_file // empty' "$cfg")
    if [ -n "$nodeploy" ] && [ -e "$nodeploy" ]; then echo "$name: NO_DEPLOY_MM present — not checked"; continue; fi
    [ -n "$status" ] && [ -f "$status" ] || { echo "$name: no status file yet — skipped"; continue; }

    age=$(( now - $(stat -c %Y "$status") ))
    unit_state=$(systemctl --user is-active "mm-live@${name}" 2>/dev/null || true)
    was=$(jq -r --arg n "$name" '.[$n] // "up"' "$STATE")
    if [ "$age" -gt "$MAX_AGE" ]; then
        pos=$(jq -r '.position // "?"' "$status"); mkt=$(jq -r '.market // "?"' "$status")
        coll=$(jq -r '.collateral_usd // "?"' "$status")
        msg="🔴 mm_live $name DOWN: status file ${age}s old, unit mm-live@${name} ${unit_state:-unknown}; last position ${pos} ${mkt}, collateral \$${coll} — resting orders are cancelled by the dead-man, the position is NOT"
        echo "$name: $msg"
        if [ "$was" != "down" ]; then
            send "$msg"
            [ "$DRY" = 1 ] || { jq --arg n "$name" '.[$n]="down"' "$STATE" > "$STATE.tmp" && mv "$STATE.tmp" "$STATE"; }
        fi
    else
        echo "$name: up (status ${age}s old, unit ${unit_state:-unknown})"
        if [ "$was" = "down" ]; then
            send "✅ mm_live $name back up (status ${age}s old, unit ${unit_state:-unknown})"
            [ "$DRY" = 1 ] || { jq --arg n "$name" '.[$n]="up"' "$STATE" > "$STATE.tmp" && mv "$STATE.tmp" "$STATE"; }
        fi
    fi
done
exit 0
