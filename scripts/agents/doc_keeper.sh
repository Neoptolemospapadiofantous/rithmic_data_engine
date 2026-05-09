#!/usr/bin/env bash
# Doc Keeper agent — keeps docs, CHANGES.md, and Obsidian decisions in sync.
# Called by coordinator. Uses claude --print with the doc-syncer agent definition.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
VAULT="$HOME/obsidian/rithmic"
OUT="$REPO/data/agents/doc-keeper"
TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

update_status() {
  python3 "$REPO/scripts/agents/agent_status.py" \
    "$REPO/data/agents" "doc-keeper" "$1" "$TS" "$2" 2>/dev/null || true
}

update_status "running" "started"

RECENT_COMMITS=$(git -C "$REPO" log --oneline -5 2>/dev/null || echo "unavailable")
DOC_AGENT=$(python3 -c "import json; print(json.load(open('$REPO/scripts/fleet_agents.json'))['doc-syncer']['prompt'])")

RESULT=$(claude --print \
  --dangerously-skip-permissions \
  --allowedTools "Bash,Read,Edit,Write" \
  --add-dir "$REPO" \
  --add-dir "$VAULT" \
  --max-budget-usd 0.50 \
  "You are the Doc Keeper for the rithmic_engine project.

Recent commits:
$RECENT_COMMITS

$DOC_AGENT

After making any changes, output a one-line summary: FIXED: X items | OK: Y items" 2>/dev/null || echo "doc-keeper failed")

SUMMARY=$(echo "$RESULT" | grep -E "^FIXED:|^OK:" | tail -1 || echo "completed")
echo "$RESULT" > "$OUT/last_run.log"
cat > "$OUT/last_run.json" << EOF
{"ts":"$TS","summary":"$SUMMARY"}
EOF

update_status "idle" "$SUMMARY"
echo "DOC_KEEPER: $SUMMARY"
