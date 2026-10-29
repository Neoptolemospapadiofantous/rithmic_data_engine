#!/usr/bin/env bash
# mm_guardian.sh — flattens the live Lighter market maker's position when the main engine has gone silent.
# Meant for deploy/mm-guardian.timer (every minute) on a DIFFERENT machine from the main engine; safe to run by hand.
#
#   scripts/mm_guardian.sh              check; act when the rule below holds
#   scripts/mm_guardian.sh --dry-run    print what it sees and what it would do; no flatten, no alert, no state change
#
# Why (2026-10-28): a box reset killed mm_live with 1.23 AVAX open. The exchange's dead-man cancelled the resting
# orders but nothing closed the position; it rode a −4 % move for ~20 h (−$0.36 on $5). mm_live_watch.sh now tells
# a human; this closes the position when no human does.
#
# Rule — act only when ALL hold, on GUARD_STRIKES consecutive runs (default 3 ≈ 3 min):
#   1. the main engine's status file is older than GUARD_MAX_AGE s (default 120), or cannot be read at all;
#   2. Lighter's public account endpoint shows a non-zero position in the market (no position → nothing to do);
#   3. it also shows 0 open orders in that market (a live main engine that is merely unreachable from here would
#      normally have quotes resting; the dead-man has cancelled a dead engine's within its deadline);
#   4. no GUARD_PAUSE file (data/mm_guardian/PAUSE) — touch it when a position is held on purpose.
# Action: start mm_live with the GUARDIAN config (its own state dir with a KILL file already in it, its own API key
# index and key file) — the engine's tested KILL path: cancel-all, adopt the exchange position, reduce-only IOC
# flatten capped 0.5 % through the touch, sticky halt; it never rests a quote (test
# guardian_start_with_kill_file_flattens_and_never_quotes). Waits until the public endpoint shows the position at 0
# (≤ GUARD_FLATTEN_WAIT s, default 90), then SIGTERMs it. One Telegram line before, one after.
# After acting it stays latched (state "acted") and does nothing more until a human runs it with --reset.
#
# Setup on the guardian machine (once):
#   1. create a SECOND Lighter API key for this account (a different key index from the main engine's), on this
#      machine, saved 0600 outside git (e.g. ~/.config/mm_live/lighter_guardian_key.json) — never copy the main key;
#   2. cp config/mm_live_avax_guardian.example.json config/mm_live_avax_guardian.json, set api_key_index/key_file;
#   3. on the SAME box as the main engine: GUARD_MAIN_SSH empty (status read locally). On another box:
#      GUARD_MAIN_SSH=user@host and GUARD_MAIN_STATUS=/path/to/status.json on that host (key-based ssh).
#   4. build/mm_live --config config/mm_live_avax_guardian.json --mode live --check-config   (opens no socket)
#   5. install deploy/mm-guardian.{service,timer}.
#
# Env (defaults): GUARD_INSTANCE (avax), GUARD_MAX_AGE (120), GUARD_STRIKES (3), GUARD_FLATTEN_WAIT (90),
# GUARD_MAIN_SSH (""), GUARD_MAIN_STATUS (main config's engine.status_file), GUARD_STATE (data/mm_guardian/state.json),
# GRID_NOTIFY (~/.local/bin/grid-notify), GUARD_API (https://mainnet.zklighter.elliot.ai). Tests: GUARD_ACCOUNT_FILE = read
# the account JSON from a file instead of the exchange (heartbeat check only; use with --dry-run). Always exits 0.

set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 0
DRY=0; RESET=0
case "${1:-}" in --dry-run) DRY=1 ;; --reset) RESET=1 ;; esac

NAME="${GUARD_INSTANCE:-avax}"
MAIN_CFG="$ROOT/config/mm_live_${NAME}.json"
GUARD_CFG="$ROOT/config/mm_live_${NAME}_guardian.json"
MAX_AGE="${GUARD_MAX_AGE:-120}"
STRIKES="${GUARD_STRIKES:-3}"
WAIT="${GUARD_FLATTEN_WAIT:-90}"
SSH_TARGET="${GUARD_MAIN_SSH:-}"
STATE="${GUARD_STATE:-$ROOT/data/mm_guardian/state.json}"
PAUSE="$(dirname "$STATE")/PAUSE"
NOTIFY="${GRID_NOTIFY:-$HOME/.local/bin/grid-notify}"
API="${GUARD_API:-https://mainnet.zklighter.elliot.ai}"

log() { echo "[$(date -u +%FT%TZ)] mm_guardian $NAME: $*"; }
send() { if [ "$DRY" = 1 ]; then log "WOULD SEND: $1"; return; fi; [ -x "$NOTIFY" ] && "$NOTIFY" "$1" >/dev/null 2>&1; return 0; }
command -v jq >/dev/null 2>&1 || { log "jq missing — nothing checked"; exit 0; }
command -v curl >/dev/null 2>&1 || { log "curl missing — nothing checked"; exit 0; }
mkdir -p "$(dirname "$STATE")" 2>/dev/null
[ -s "$STATE" ] || echo '{"strikes":0,"acted":false}' > "$STATE"
setstate() { [ "$DRY" = 1 ] && return; jq "$1" "$STATE" > "$STATE.tmp" && mv "$STATE.tmp" "$STATE"; }

if [ "$RESET" = 1 ]; then echo '{"strikes":0,"acted":false}' > "$STATE"; log "reset"; exit 0; fi
if [ "$(jq -r '.acted' "$STATE")" = "true" ]; then log "latched after acting at $(jq -r '.acted_at // "?"' "$STATE") — run with --reset after checking"; exit 0; fi
[ -e "$PAUSE" ] && { log "PAUSE present — not checking"; setstate '.strikes=0'; exit 0; }
[ -f "$MAIN_CFG" ] || { log "no main config $MAIN_CFG"; exit 0; }

ACCT=$(jq -r '.account_index' "$MAIN_CFG"); MKT=$(jq -r '.engine.market_spec.market_index' "$MAIN_CFG")
SYM=$(jq -r '.market' "$MAIN_CFG")
STATUS_PATH="${GUARD_MAIN_STATUS:-$(jq -r '.engine.status_file' "$MAIN_CFG")}"

# 1. main engine heartbeat (age of its status file, read locally or over ssh)
if [ -z "$SSH_TARGET" ]; then
    MT=$(stat -c %Y "$STATUS_PATH" 2>/dev/null || echo "")
else
    MT=$(ssh -o BatchMode=yes -o ConnectTimeout=10 "$SSH_TARGET" "stat -c %Y '$STATUS_PATH'" 2>/dev/null || echo "")
fi
NOW=$(date +%s)
if [ -n "$MT" ]; then AGE=$(( NOW - MT )); else AGE=-1; fi
if [ "$AGE" -ge 0 ] && [ "$AGE" -le "$MAX_AGE" ]; then
    [ "$(jq -r '.strikes' "$STATE")" != 0 ] && log "main healthy again (status ${AGE}s)"
    setstate '.strikes=0'; log "main up (status ${AGE}s old)"; exit 0
fi

# 2 + 3. exchange view (public, no key)
if [ -n "${GUARD_ACCOUNT_FILE:-}" ]; then ACC=$(cat "$GUARD_ACCOUNT_FILE"); else ACC=$(curl -s -m 10 "$API/api/v1/account?by=index&value=$ACCT"); fi
ROW=$(echo "$ACC" | jq -c --argjson m "$MKT" '.accounts[0].positions[]? | select(.market_id == $m)' 2>/dev/null)
if [ -z "$ROW" ] && ! echo "$ACC" | jq -e '.accounts[0]' >/dev/null 2>&1; then
    log "main silent (status ${AGE}s) but the exchange endpoint did not answer — no action this run"; exit 0
fi
[ -n "$ROW" ] || ROW='{}'
POS=$(echo "$ROW" | jq -r 'if .position then ((.position|tonumber) * (if .sign == -1 then -1 else 1 end)) else 0 end')
OPEN=$(echo "$ROW" | jq -r '.open_order_count // 0')
if [ "$(echo "$POS" | jq 'if . == 0 then 1 else 0 end')" = 1 ]; then
    log "main silent (status ${AGE}s) but no $SYM position on the exchange — nothing to do"; setstate '.strikes=0'; exit 0
fi
if [ "$OPEN" != 0 ]; then
    log "main silent from here (status ${AGE}s) but $OPEN order(s) resting — engine probably alive, not acting"; setstate '.strikes=0'; exit 0
fi

S=$(( $(jq -r '.strikes' "$STATE") + 1 ))
setstate ".strikes=$S"
log "STRIKE $S/$STRIKES: main silent (status ${AGE}s), $SYM position $POS, 0 open orders"
[ "$S" -lt "$STRIKES" ] && exit 0

# act
[ -f "$GUARD_CFG" ] || { log "would flatten but $GUARD_CFG is missing (setup step 2)"; send "⚠️ mm_guardian $NAME: main silent, $SYM position $POS stranded, but the guardian config is missing — close it by hand"; setstate '.strikes=0'; exit 0; }
GSTATE=$(jq -r '.state_dir' "$GUARD_CFG")
GSTATUS=$(jq -r '.engine.status_file' "$GUARD_CFG")
if [ "$DRY" = 1 ]; then log "WOULD FLATTEN $POS $SYM with $GUARD_CFG (state $GSTATE)"; exit 0; fi
send "🛡️ mm_guardian $NAME: main engine silent ${AGE}s with $SYM position $POS and no orders — flattening now"
mkdir -p "$GSTATE" && echo "guardian $(date -u +%FT%TZ)" > "$GSTATE/KILL"
INIT=""; [ -f "$GSTATE/risk_state.json" ] || INIT="--init-risk"
GODEBUG=asyncpreemptoff=1 "$ROOT/build/mm_live" --config "$GUARD_CFG" --mode live $INIT >> "$ROOT/data/logs/mm_guardian_${NAME}.log" 2>&1 &
PID=$!
FLAT=0
for _ in $(seq 1 $(( WAIT / 5 ))); do
    sleep 5
    kill -0 "$PID" 2>/dev/null || break
    R=$(curl -s -m 10 "$API/api/v1/account?by=index&value=$ACCT" | jq -r --argjson m "$MKT" '.accounts[0].positions[]? | select(.market_id == $m) | .position' 2>/dev/null)
    if [ -n "$R" ] && [ "$(echo "$R" | jq 'tonumber == 0')" = true ]; then FLAT=1; break; fi
done
kill -TERM "$PID" 2>/dev/null; wait "$PID" 2>/dev/null
setstate ".acted=true | .acted_at=\"$(date -u +%FT%TZ)\" | .flat=$( [ $FLAT = 1 ] && echo true || echo false) | .strikes=0"
if [ "$FLAT" = 1 ]; then
    send "✅ mm_guardian $NAME: $SYM flat. Guardian latched; the main engine stays as it is — restart it by hand, then scripts/mm_guardian.sh --reset"
else
    send "🔴 mm_guardian $NAME: flatten NOT confirmed within ${WAIT}s — check the account by hand (log data/logs/mm_guardian_${NAME}.log)"
fi
log "done (flat=$FLAT)"
exit 0
