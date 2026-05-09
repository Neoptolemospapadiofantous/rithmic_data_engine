#!/usr/bin/env bash
# Hermes Coordinator — orchestrates all specialist agents.
#
# Agents managed:
#   ci-watchdog     builds + tests + fixes
#   deploy-manager  Oracle deploy + health
#   doc-keeper      docs + CHANGES.md + Obsidian decisions
#   session-analyst post-session PostgreSQL → Obsidian
#   audit-sentinel  17-check audit monitoring
#
# Usage:
#   make coordinator-start          persistent loop
#   make coordinator-once           one cycle, exit
#   make coordinator-status         show agent statuses
#   make agent-run AGENT=<name>     run one agent manually

set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
VAULT="$HOME/obsidian/rithmic"
DATA="$REPO/data/agents"
LOCK="$DATA/hermes.lock"
ONCE=0
[[ "${1:-}" == "--once" ]] && ONCE=1

log()  { echo "[coordinator] $(date -u +%H:%M:%S) $*"; }
warn() { echo "[coordinator] WARN: $*" >&2; }

# ── Phase detection (force base-10) ──────────────────────────────────────────
et_hhmm() { TZ="America/New_York" date +"%H%M"; }
phase() {
  local n=$((10#$(et_hhmm)))
  if   [[ $n -lt 920  ]]; then echo "PRE_MARKET"
  elif [[ $n -lt 930  ]]; then echo "PRE_SESSION"
  elif [[ $n -lt 1605 ]]; then echo "SESSION"
  elif [[ $n -lt 1700 ]]; then echo "POST_SESSION"
  else                          echo "OVERNIGHT"
  fi
}

sleep_for_phase() {
  case "$(phase)" in
    PRE_MARKET)   echo 900  ;;
    PRE_SESSION)  echo 300  ;;
    SESSION)      echo 1800 ;;
    POST_SESSION) echo 600  ;;
    OVERNIGHT)    echo 5400 ;;
  esac
}

# ── Exclusive lock for hermes runs (prevent parallel builds) ──────────────────
acquire_lock() {
  local timeout=120 elapsed=0
  while [[ -f "$LOCK" ]]; do
    [[ $elapsed -ge $timeout ]] && { warn "lock timeout — removing stale lock"; rm -f "$LOCK"; break; }
    sleep 5; elapsed=$((elapsed+5))
  done
  echo "$$" > "$LOCK"
}
release_lock() { rm -f "$LOCK"; }

# ── Agent status helpers ──────────────────────────────────────────────────────
agent_last_run() {
  python3 -c "
import json, pathlib, sys
f = pathlib.Path('$DATA/status.json')
d = json.loads(f.read_text())
print(d.get('$1', {}).get('last_run') or 'never')
" 2>/dev/null || echo "never"
}

minutes_since() {
  # Minutes since ISO timestamp, or 9999 if never
  python3 -c "
from datetime import datetime, timezone
ts = '$1'
if ts == 'never': print(9999); exit()
try:
  dt = datetime.fromisoformat(ts.replace('Z','+00:00'))
  delta = datetime.now(timezone.utc) - dt
  print(int(delta.total_seconds() / 60))
except: print(9999)
" 2>/dev/null || echo "9999"
}

update_coordinator_status() {
  python3 -c "
import json, pathlib
f = pathlib.Path('$DATA/coordinator/status.json')
f.parent.mkdir(parents=True, exist_ok=True)
f.write_text(json.dumps({'status': '$1', 'last_run': '$(date -u +%Y-%m-%dT%H:%M:%SZ)', 'phase': '$(phase)'}))
" 2>/dev/null || true
}

# ── Run a single agent ────────────────────────────────────────────────────────
run_agent() {
  local name="$1"; shift
  local script="$REPO/scripts/agents/${name//-/_}.sh"
  if [[ ! -x "$script" ]]; then warn "agent script not found: $script"; return 1; fi
  log "→ $name starting"
  bash "$script" "$@" 2>&1 | sed "s/^/  [$name] /"
  log "→ $name done"
}

# ── Write coordinator cycle note to Obsidian ──────────────────────────────────
write_cycle_note() {
  local p="$1"
  local note_dir="$VAULT/hermes"
  local dt
  dt=$(date -u +"%Y-%m-%d %H:%M")
  local note="$note_dir/COORD-${dt}.md"

  python3 - "$note" "$p" "$DATA" << 'PYEOF'
import json, sys, pathlib
from datetime import datetime, timezone

note_path, phase, data_dir = sys.argv[1], sys.argv[2], sys.argv[3]
base = pathlib.Path(data_dir)
dt = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M")
date = dt[:10]

agents = ["ci-watchdog", "deploy-manager", "doc-keeper", "session-analyst", "audit-sentinel"]
rows = []
for agent in agents:
  f = base / agent / "status.json"
  d = json.loads(f.read_text()) if f.exists() else {}
  last = (d.get("last_run") or "never")[11:16]
  result = d.get("last_result") or "—"
  rows.append(f"| {agent} | {last} | {result} |")

content = f"""---
date: {date}
type: coordinator
phase: {phase}
tags: [hermes, coordinator]
---

# Coordinator — {dt} UTC  ({phase})

## Agent activity
| Agent | Last run | Result |
|-------|----------|--------|
""" + "\n".join(rows)

pathlib.Path(note_path).write_text(content)
print(f"[coordinator] Obsidian note: {note_path}")
PYEOF
}

# ── Main coordinator cycle ────────────────────────────────────────────────────
run_cycle() {
  local p
  p=$(phase)
  update_coordinator_status "running"
  log "=== COORDINATOR CYCLE: $p ==="

  case "$p" in

    PRE_MARKET)
      # Full deep analysis before market open
      # 1. CI first (with lock)
      acquire_lock
      run_agent ci-watchdog
      release_lock

      # 2. Audit sentinel (reads audit output — no build conflict)
      run_agent audit-sentinel

      # 3. Doc keeper (no build conflict, runs claude)
      run_agent doc-keeper &
      local doc_pid=$!

      # 4. Deploy if green (after CI, before session)
      local overall
      overall=$(python3 -c "import json; print(json.load(open('$REPO/data/hermes_findings.json'))['overall'])" 2>/dev/null || echo "UNKNOWN")
      if [[ "$overall" == "PASS" ]]; then
        run_agent deploy-manager
      else
        log "Deploy skipped — CI overall=$overall"
      fi

      wait $doc_pid 2>/dev/null || true
      ;;

    PRE_SESSION)
      # Fast CI check + Oracle health — tight window, no doc work
      acquire_lock
      run_agent ci-watchdog --fast
      release_lock
      run_agent deploy-manager --health-only
      ;;

    SESSION)
      # Fast CI + Oracle health — NEVER deploy during live session
      acquire_lock
      run_agent ci-watchdog --fast
      release_lock
      run_agent audit-sentinel &
      run_agent deploy-manager --health-only
      wait 2>/dev/null || true
      ;;

    POST_SESSION)
      # Full analysis + session post-mortem + doc sync + push
      acquire_lock
      run_agent ci-watchdog
      release_lock

      # Run non-conflicting agents in parallel
      run_agent audit-sentinel &
      run_agent session-analyst &
      run_agent doc-keeper &
      wait 2>/dev/null || true

      # Push if green
      local overall
      overall=$(python3 -c "import json; print(json.load(open('$REPO/data/hermes_findings.json'))['overall'])" 2>/dev/null || echo "UNKNOWN")
      if [[ "$overall" == "PASS" ]]; then
        local changes
        changes=$(git -C "$REPO" status --porcelain 2>/dev/null | wc -l)
        if [[ "$changes" -eq 0 ]]; then
          log "Pushing to remote"
          git -C "$REPO" push origin main 2>&1 | tail -3
        else
          log "Uncommitted changes — not pushing (agents may have made edits)"
        fi
      fi
      ;;

    OVERNIGHT)
      # Minimal — audit sentinel + fast CI only on state change
      run_agent audit-sentinel
      local crit
      crit=$(python3 -c "import json; d=json.load(open('$DATA/audit-sentinel/findings.json')); print(len(d['critical']))" 2>/dev/null || echo 0)
      if [[ "$crit" -gt 0 ]]; then
        log "Audit critical findings — running CI"
        acquire_lock
        run_agent ci-watchdog --fast
        release_lock
      fi
      ;;
  esac

  # Always write Obsidian note + daily update
  bash "$REPO/scripts/obsidian_note.sh" 2>/dev/null || true
  bash "$REPO/scripts/obsidian_daily.sh" 2>/dev/null || true
  write_cycle_note "$p"
  update_coordinator_status "idle"
  log "=== CYCLE DONE ==="
}

# ── Status display ────────────────────────────────────────────────────────────
show_status() {
  bash "$REPO/scripts/agents/status.sh"
}

# ── Entry point ───────────────────────────────────────────────────────────────
# Write PID
echo "$$" > "$DATA/coordinator/coordinator.pid"
trap 'rm -f "$DATA/coordinator/coordinator.pid" "$LOCK"' EXIT

log "Starting. phase=$(phase) ONCE=$ONCE"

while true; do
  run_cycle
  [[ $ONCE -eq 1 ]] && { show_status; exit 0; }
  secs=$(sleep_for_phase)
  log "Sleeping ${secs}s (next phase: $(phase))"
  sleep "$secs"
done
