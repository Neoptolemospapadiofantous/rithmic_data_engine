#!/usr/bin/env bash
# Hermes Fleet — multi-agent analysis and fix pipeline.
#
# Architecture:
#   Coordinator (this session) spawns 4 specialist agents in parallel:
#     regression-watcher  — detects new/recurring failures vs Obsidian history
#     code-scanner        — quality issues in recently changed C++ files
#     audit-analyst       — deep 17-check audit analysis + threshold proximity
#     test-sentinel       — test coverage gaps in execution layer
#
#   Coordinator synthesizes, fixes confirmed issues, re-runs hermes, writes
#   an enriched Obsidian note documenting everything the fleet found and did.
#
# Usage:
#   make hermes-fleet           — full fleet run
#   bash scripts/hermes_fleet.sh [--fast]  — direct invocation

set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
FAST=0
[[ "${1:-}" == "--fast" ]] && FAST=1

log() { echo "[fleet] $*"; }

# ── 1. Baseline hermes run ─────────────────────────────────────────────────────
log "=== BASELINE HERMES ==="
if [[ $FAST -eq 1 ]]; then
  bash "$REPO/scripts/hermes.sh" --fast || true
else
  bash "$REPO/scripts/hermes.sh" || true
fi

FINDINGS=$(cat "$REPO/data/hermes_findings.json")
AUDIT=$(cat "$REPO/data/audit_status.json")
OVERALL=$(python3 -c "import json; print(json.loads('$FINDINGS'.replace(\"'\",\"'\"))['overall'])" 2>/dev/null || \
          python3 -c "import json; d=json.load(open('$REPO/data/hermes_findings.json')); print(d['overall'])")

log "Baseline: $OVERALL"

# ── 2. Collect context for coordinator ────────────────────────────────────────
RECENT_COMMITS=$(git -C "$REPO" log --oneline -10 2>/dev/null || echo "unavailable")
CHANGED_FILES=$(git -C "$REPO" diff --name-only HEAD~5 2>/dev/null | head -30 || echo "unavailable")
OBSIDIAN_RECENT=$(ls "$HOME/obsidian/rithmic/hermes/"*.md 2>/dev/null | tail -3 | \
  xargs -I{} sh -c 'echo "=== {} ==="; head -20 {}' 2>/dev/null || echo "no prior notes")

AGENTS_JSON=$(cat "$REPO/scripts/fleet_agents.json")

# ── 3. Build coordinator prompt ────────────────────────────────────────────────
COORDINATOR_PROMPT="You are the Hermes Fleet Coordinator for the rithmic_engine C++ algorithmic trading system (NQ/MNQ futures, Legends prop account).

## Current state
Hermes overall: ${OVERALL}

Hermes findings:
$(python3 -c "import json; d=json.load(open('$REPO/data/hermes_findings.json')); [print(f'  {c[\"status\"]} {c[\"check\"]}: {c[\"detail\"]}') for c in d['findings']]" 2>/dev/null)

Audit warnings:
$(python3 -c "import json; d=json.load(open('$REPO/data/audit_status.json')); [print(f'  {c[\"status\"]} {c[\"name\"]}: {c[\"message\"]}') for c in d['checks'] if c['status'] in ('WARN','FAIL')]" 2>/dev/null || echo "  none")

Recent commits (last 10):
${RECENT_COMMITS}

Files changed in last 5 commits:
${CHANGED_FILES}

Recent Obsidian notes (for regression context):
${OBSIDIAN_RECENT}

## Your mission
1. Spawn all 5 specialist agents IN PARALLEL using the Agent tool:
   - regression-watcher
   - code-scanner
   - audit-analyst
   - test-sentinel
   - doc-syncer
   Pass each agent the repo path: /home/theone/Desktop/rithmic_engine

2. Collect all 5 reports. Synthesize into a priority list:
   CRITICAL → fix now
   HIGH     → fix now if < 30 min
   MEDIUM   → create Obsidian task note
   LOW      → note only

3. For each CRITICAL or HIGH confirmed issue (code OR docs):
   - Fix it (edit the relevant file)
   - Do NOT touch: src/execution/orb_strategy.hpp, config/live_config.json
   - After ALL fixes: run make hermes to verify still green

4. Write an enriched Obsidian note to:
   ~/obsidian/rithmic/hermes/FLEET-$(date -u +%Y-%m-%d\ %H:%M).md

   Note format:
   ---
   date: <today>
   type: fleet
   overall: <final hermes status>
   agents: [regression-watcher, code-scanner, audit-analyst, test-sentinel, doc-syncer]
   tags: [hermes, fleet, <pass/warn/fail>]
   ---
   # Fleet Run — <datetime>
   ## Agent Reports (summary)
   ### Regression Watcher
   <findings>
   ### Code Scanner
   <findings>
   ### Audit Analyst
   <findings>
   ### Test Sentinel
   <findings>
   ## Actions taken
   <list of fixes applied or 'None — all clear'>
   ## Final hermes status
   <result after fixes>

5. Also run: bash scripts/obsidian_daily.sh

6. Report a final one-line summary to stdout."

# ── 4. Launch the fleet ────────────────────────────────────────────────────────
log "=== LAUNCHING FLEET ==="
log "Agents: regression-watcher | code-scanner | audit-analyst | test-sentinel | doc-syncer"

claude --print \
  --dangerously-skip-permissions \
  --allowedTools "Agent,Bash,Read,Edit,Write" \
  --add-dir "$REPO" \
  --add-dir "$HOME/obsidian/rithmic" \
  --agents "$AGENTS_JSON" \
  --max-budget-usd 2.00 \
  "$COORDINATOR_PROMPT"

log "=== FLEET COMPLETE ==="
