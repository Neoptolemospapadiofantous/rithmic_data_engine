#!/usr/bin/env bash
# recorder_watch.sh — one-minute health check of the DEX market-data recorders (lighter_recorder, hl_recorder,
# dex_recorder ×3). Meant for deploy/recorder-watch.timer (every minute); safe to run by hand.
#
#   scripts/recorder_watch.sh            check, alert on changes via grid-notify
#   scripts/recorder_watch.sh --dry-run  print what it sees and what it would send; no alert, no state change
#
# A recorder is a PROBLEM when its unit is enabled but not active, or its status file
# (data/recorder_status/<name>.json, written every ~4 s — src/dex/recorder_status.hpp) is missing, older than
# MAX_AGE, says no book/trade message arrived in its BUSIEST market for more than MAX_AGE seconds, or reports
# lines it could not archive. A disabled or uninstalled unit is skipped (switched off on purpose).
#
# Alerts once: a problem is sent when it appears and a "recovered" line when it clears; nothing repeats while it
# lasts. Rate-limited: at most one message per MIN_GAP seconds (a held-back change goes out on a later run).
# FAIL-OPEN: grid-notify missing or failing never fails this script; it always exits 0.
#
# Env (defaults): RECORDER_WATCH_UNITS "unit=status-name …" (the five recorders below), RECORDER_STATUS_DIR
# (data/recorder_status), RECORDER_WATCH_MAX_AGE (60), RECORDER_WATCH_MIN_GAP (600),
# RECORDER_WATCH_STATE (data/recorder_watch/state.json), GRID_NOTIFY (~/.local/bin/grid-notify).
# A backup recorder (docs/market_making_system.md §2) is added as e.g. "lighter-recorder-backup=lighter_backup".

set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

UNITS="${RECORDER_WATCH_UNITS:-lighter-recorder-local=lighter hl-recorder-local=hyperliquid extended-recorder-local=extended aster-recorder-local=aster paradex-recorder-local=paradex}"
STATUS_DIR="${RECORDER_STATUS_DIR:-$ROOT/data/recorder_status}"
MAX_AGE="${RECORDER_WATCH_MAX_AGE:-60}"
MIN_GAP="${RECORDER_WATCH_MIN_GAP:-600}"
STATE="${RECORDER_WATCH_STATE:-$ROOT/data/recorder_watch/state.json}"
NOTIFY="${GRID_NOTIFY:-$HOME/.local/bin/grid-notify}"
SYSTEMCTL="${RECORDER_WATCH_SYSTEMCTL:-systemctl --user}"   # tests point this at a stub

command -v jq >/dev/null 2>&1 || { echo "recorder_watch: jq missing — nothing checked"; exit 0; }

now=$(date +%s)
declare -A problems=()   # key → text

for pair in $UNITS; do
  unit="${pair%%=*}"; name="${pair#*=}"
  enabled=$($SYSTEMCTL is-enabled "$unit" 2>/dev/null | head -1)
  [ "$enabled" = "enabled" ] || continue
  active=$($SYSTEMCTL is-active "$unit" 2>/dev/null | head -1)
  if [ "$active" != "active" ]; then
    problems["$unit:inactive"]="$unit is ${active:-unknown} (enabled but not running)"
    continue
  fi
  f="$STATUS_DIR/$name.json"
  if [ ! -s "$f" ]; then
    problems["$unit:nostatus"]="$unit: no status file $f (a binary from before 2026-10-26? restart the unit)"
    continue
  fi
  # one jq call: file age from updated_us, busiest market + its age, unarchived lines
  read -r upd busiest bage dropped werr < <(jq -r '[(.updated_us // 0), (.busiest // "" | if . == "" then "-" else . end),
      (.busiest_age_s // 1e9), (.dropped_lines // 0), (.write_errors // 0)] | map(tostring) | join(" ")' "$f" 2>/dev/null) \
    || { problems["$unit:badstatus"]="$unit: status file $f unreadable"; continue; }
  [ -n "${upd:-}" ] || { problems["$unit:badstatus"]="$unit: status file $f unreadable"; continue; }
  fage=$(( now - ${upd%.*} / 1000000 ))
  if [ "$fage" -gt "$MAX_AGE" ]; then
    problems["$unit:stale"]="$unit: status not updated for ${fage}s (process hung?)"
    continue
  fi
  silent=$(awk -v a="$bage" -v f="$fage" 'BEGIN { printf "%d", a + f }')
  if [ "$silent" -gt "$MAX_AGE" ]; then
    problems["$unit:silent"]="$unit: no book/trade message for ${silent}s on its busiest market ${busiest}"
  fi
  if [ "${dropped%.*}" -gt 0 ] 2>/dev/null; then
    problems["$unit:dropped"]="$unit: ${dropped%.*} lines not archived, ${werr%.*} write errors since start (disk full / drive unmounted?)"
  fi
done

# ── state: which problems were already reported, and when the last message went out ─────────────────────────
prev_keys=""; last_sent=0
if [ -s "$STATE" ]; then
  prev_keys=$(jq -r '.keys[]?' "$STATE" 2>/dev/null)
  last_sent=$(jq -r '.last_sent // 0' "$STATE" 2>/dev/null); last_sent=${last_sent:-0}
fi
declare -A prev=()
for k in $prev_keys; do prev["$k"]=1; done

new_lines=(); cleared_lines=()
for k in "${!problems[@]}"; do [ -n "${prev[$k]:-}" ] || new_lines+=("${problems[$k]}"); done
for k in "${!prev[@]}"; do [ -n "${problems[$k]:-}" ] || cleared_lines+=("recovered: ${k%%:*} (${k#*:})"); done

if [ "$DRY" = 1 ]; then
  echo "recorder_watch (dry run): ${#problems[@]} problem(s)"
  for k in "${!problems[@]}"; do echo "  [$k] ${problems[$k]}"; done
  for l in "${new_lines[@]}"; do echo "  would alert: $l"; done
  for l in "${cleared_lines[@]}"; do echo "  would send:  $l"; done
  exit 0
fi

if [ ${#new_lines[@]} -eq 0 ] && [ ${#cleared_lines[@]} -eq 0 ]; then exit 0; fi
if [ $(( now - last_sent )) -lt "$MIN_GAP" ]; then
  echo "$(date -u +%FT%TZ) recorder_watch: ${#new_lines[@]} new / ${#cleared_lines[@]} cleared — held back (last alert $(( now - last_sent ))s ago)"
  exit 0
fi

msg="📼 recorder watch"
for l in "${new_lines[@]}"; do msg+=$'\n'"🔴 $l"; done
for l in "${cleared_lines[@]}"; do msg+=$'\n'"🟢 $l"; done
echo "$(date -u +%FT%TZ) $msg"
if [ -x "$NOTIFY" ]; then
  printf '%s' "$msg" | timeout 30 "$NOTIFY" >/dev/null 2>&1 || true
else
  echo "recorder_watch: $NOTIFY not found — alert only logged"
fi

mkdir -p "$(dirname "$STATE")" 2>/dev/null || exit 0
keys_json=$(printf '%s\n' "${!problems[@]}" | jq -R . | jq -sc 'map(select(length > 0))')
jq -nc --argjson keys "$keys_json" --argjson t "$now" '{keys: $keys, last_sent: $t}' > "$STATE.tmp" 2>/dev/null \
  && mv "$STATE.tmp" "$STATE"
exit 0
