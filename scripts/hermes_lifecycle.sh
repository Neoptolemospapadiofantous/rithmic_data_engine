#!/usr/bin/env bash
# Hermes Lifecycle — time-aware CI/deploy/monitor loop.
#
# Phases (all times Eastern):
#   PRE_MARKET   before 09:20  → hermes-fleet + deploy if green
#   PRE_SESSION  09:20–09:30   → hermes-fast + Oracle health (no deploy)
#   SESSION      09:30–16:05   → hermes-fast every 30 min, no deploy
#   POST_SESSION 16:05–17:00   → hermes-fleet + session post-mortem + push
#   OVERNIGHT    17:00–next    → hermes-fast every 90 min
#
# Usage:
#   bash scripts/hermes_lifecycle.sh [--once] [--no-deploy]
#     --once       run one cycle and exit (for testing)
#     --no-deploy  skip all git push / make deploy steps

set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
VAULT="$HOME/obsidian/rithmic"
ORACLE="opc@170.9.233.177"
SSH_KEY="$HOME/.ssh/id_ed25519"
ONCE=0
NO_DEPLOY=0

for arg in "$@"; do
  [[ "$arg" == "--once"      ]] && ONCE=1
  [[ "$arg" == "--no-deploy" ]] && NO_DEPLOY=1
done

log()  { echo "[lifecycle] $(date -u +%H:%M:%S) $*"; }
warn() { echo "[lifecycle] WARN: $*" >&2; }

# ── Time helpers (Eastern = UTC-4 during DST, UTC-5 otherwise) ────────────────
et_hour_min() {
  # Returns ET time as HHMM integer (handles DST via TZ env)
  TZ="America/New_York" date +"%H%M"
}

phase() {
  local hm
  hm=$(et_hour_min)
  local n=$((10#$hm))  # force base-10 — leading zeros would be parsed as octal otherwise
  if   [[ $n -lt 920  ]]; then echo "PRE_MARKET"
  elif [[ $n -lt 930  ]]; then echo "PRE_SESSION"
  elif [[ $n -lt 1605 ]]; then echo "SESSION"
  elif [[ $n -lt 1700 ]]; then echo "POST_SESSION"
  else                          echo "OVERNIGHT"
  fi
}

sleep_seconds() {
  case "$(phase)" in
    PRE_MARKET)   echo 900  ;;   # 15 min — want to catch issues before open
    PRE_SESSION)  echo 300  ;;   #  5 min — tight window
    SESSION)      echo 1800 ;;   # 30 min — lighter during live trading
    POST_SESSION) echo 600  ;;   # 10 min — wrap up quickly
    OVERNIGHT)    echo 5400 ;;   # 90 min — slow, audit-focused
  esac
}

# ── Oracle health check ───────────────────────────────────────────────────────
oracle_health() {
  local service="${1:-nq_executor@RTH}"
  log "Checking Oracle: $ORACLE ($service)"
  local status
  status=$(ssh -i "$SSH_KEY" -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
    "$ORACLE" "systemctl is-active $service 2>/dev/null || echo inactive" 2>/dev/null || echo "unreachable")

  if [[ "$status" == "active" ]]; then
    log "Oracle $service: RUNNING ✓"
    return 0
  else
    warn "Oracle $service: $status"
    # Write alert to Obsidian
    local note="$VAULT/daily/$(date -u +%Y-%m-%d).md"
    echo "" >> "$note"
    echo "### Oracle alert — $(date -u +%H:%M) UTC" >> "$note"
    echo "- **$service**: $status" >> "$note"
    return 1
  fi
}

# ── Deploy gate ───────────────────────────────────────────────────────────────
deploy_if_green() {
  if [[ $NO_DEPLOY -eq 1 ]]; then
    log "Deploy skipped (--no-deploy)"
    return 0
  fi

  local overall
  overall=$(python3 -c "import json; print(json.load(open('$REPO/data/hermes_findings.json'))['overall'])" 2>/dev/null || echo "UNKNOWN")

  if [[ "$overall" == "PASS" ]]; then
    log "Hermes PASS — deploying to Oracle"
    cd "$REPO" && make deploy 2>&1 | tail -5
    log "Deploy complete"
    sleep 15  # give service time to restart
    oracle_health || warn "Service not active after deploy"
  else
    log "Deploy skipped — hermes overall=$overall"
  fi
}

# ── Push to git ───────────────────────────────────────────────────────────────
push_if_green() {
  if [[ $NO_DEPLOY -eq 1 ]]; then
    log "Push skipped (--no-deploy)"
    return 0
  fi

  local overall
  overall=$(python3 -c "import json; print(json.load(open('$REPO/data/hermes_findings.json'))['overall'])" 2>/dev/null || echo "UNKNOWN")

  if [[ "$overall" == "PASS" ]]; then
    local changes
    changes=$(git -C "$REPO" status --porcelain 2>/dev/null | wc -l)
    if [[ "$changes" -gt 0 ]]; then
      log "Uncommitted changes — not pushing (fleet may have made fixes)"
    else
      log "Pushing to remote"
      git -C "$REPO" push origin main 2>&1 | tail -3
    fi
  fi
}

# ── Session post-mortem ───────────────────────────────────────────────────────
session_postmortem() {
  log "Generating session post-mortem"
  bash "$REPO/scripts/obsidian_session.sh" "$(date -u +%Y-%m-%d)" 2>/dev/null || \
    warn "Session post-mortem failed (PostgreSQL may be unavailable)"
}

# ── Weekly digest (Fridays post-session) ─────────────────────────────────────
weekly_digest_if_friday() {
  local dow
  dow=$(TZ="America/New_York" date +%u)  # 5 = Friday
  [[ "$dow" != "5" ]] && return 0

  log "Friday — writing weekly digest"
  local week_note="$VAULT/daily/week-$(date -u +%Y-%W).md"

  python3 - "$VAULT" "$week_note" << 'PYEOF'
import sys, pathlib, re
vault, out_path = sys.argv[1], sys.argv[2]

hermes_dir = pathlib.Path(vault) / "hermes"
sessions_dir = pathlib.Path(vault) / "sessions"

# Last 7 days of hermes notes
notes = sorted(hermes_dir.glob("*.md"))[-35:]  # ~5/day × 7 days
pass_c = warn_c = fail_c = 0
for n in notes:
  content = n.read_text()
  m = re.search(r'overall: (\w+)', content)
  if m:
    s = m.group(1)
    if s == 'PASS': pass_c += 1
    elif s == 'WARN': warn_c += 1
    elif s == 'FAIL': fail_c += 1

# Last 5 session notes
sessions = sorted(sessions_dir.glob("*.md"))[-5:]
pnl_total = 0.0
trade_total = 0
for s in sessions:
  content = s.read_text()
  pnl_m = re.search(r'pnl: ([-\d.]+)', content)
  tr_m = re.search(r'trades: (\d+)', content)
  if pnl_m: pnl_total += float(pnl_m.group(1))
  if tr_m: trade_total += int(tr_m.group(1))

week = pathlib.Path(out_path)
week.write_text(f"""---
type: weekly
tags: [weekly]
---

# Weekly Digest — {pathlib.Path(out_path).stem}

## Hermes health
- Runs: {pass_c + warn_c + fail_c} total
- ✅ PASS: {pass_c} | ⚠️ WARN: {warn_c} | ❌ FAIL: {fail_c}

## Trading sessions
- Total trades: {trade_total}
- Net P&L: ${pnl_total:.2f}

## Sessions this week
""" + "\n".join(f"- [[sessions/{s.stem}]]" for s in sessions))
print(f"Weekly digest written: {out_path}")
PYEOF
}

# ── One lifecycle cycle ───────────────────────────────────────────────────────
run_cycle() {
  local p
  p=$(phase)
  log "=== CYCLE: $p ==="

  case "$p" in

    PRE_MARKET)
      # Full fleet analysis + deploy if green
      log "Running hermes-fleet (pre-market deep check)"
      cd "$REPO" && bash scripts/hermes_fleet.sh --fast 2>&1
      bash "$REPO/scripts/obsidian_daily.sh" 2>/dev/null || true
      deploy_if_green
      ;;

    PRE_SESSION)
      # Fast check only — window is too short for fleet
      log "Running hermes-fast (pre-session)"
      cd "$REPO" && make hermes-fast 2>&1 | tail -5
      bash "$REPO/scripts/obsidian_note.sh" 2>/dev/null || true
      bash "$REPO/scripts/obsidian_daily.sh" 2>/dev/null || true
      oracle_health || true
      ;;

    SESSION)
      # Fast check + Oracle health — never deploy during live session
      log "Running hermes-fast (session monitoring — no deploy)"
      cd "$REPO" && make hermes-fast 2>&1 | tail -5
      bash "$REPO/scripts/obsidian_note.sh" 2>/dev/null || true
      bash "$REPO/scripts/obsidian_daily.sh" 2>/dev/null || true
      oracle_health || true
      ;;

    POST_SESSION)
      # Full fleet + session post-mortem + push
      log "Running hermes-fleet (post-session)"
      cd "$REPO" && bash scripts/hermes_fleet.sh 2>&1
      session_postmortem
      bash "$REPO/scripts/obsidian_daily.sh" 2>/dev/null || true
      weekly_digest_if_friday
      push_if_green
      ;;

    OVERNIGHT)
      # Fast check — focus is audit daemon (disk, WAL, logs)
      log "Running hermes-fast (overnight)"
      cd "$REPO" && make hermes-fast 2>&1 | tail -5
      # Only write Obsidian note if something changed
      local overall
      overall=$(python3 -c "import json; print(json.load(open('$REPO/data/hermes_findings.json'))['overall'])" 2>/dev/null || echo "UNKNOWN")
      if [[ "$overall" != "PASS" ]]; then
        bash "$REPO/scripts/obsidian_note.sh" "Overnight check: $overall" 2>/dev/null || true
        bash "$REPO/scripts/obsidian_daily.sh" 2>/dev/null || true
      fi
      ;;
  esac

  log "=== CYCLE DONE ==="
}

# ── Main loop ─────────────────────────────────────────────────────────────────
log "Hermes Lifecycle starting. ONCE=$ONCE NO_DEPLOY=$NO_DEPLOY"

while true; do
  run_cycle

  [[ $ONCE -eq 1 ]] && { log "--once: exiting"; exit 0; }

  local secs
  secs=$(sleep_seconds)
  log "Sleeping ${secs}s until next cycle (phase=$(phase))"
  sleep "$secs"
done
