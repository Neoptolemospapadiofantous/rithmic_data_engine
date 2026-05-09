#!/usr/bin/env bash
# Session Analyst agent — post-session PostgreSQL pull → Obsidian post-mortem.
# Called by coordinator after 16:05 ET.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$REPO/data/agents/session-analyst"
TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
DATE="${1:-$(date -u +"%Y-%m-%d")}"

update_status() {
  python3 "$REPO/scripts/agents/agent_status.py" \
    "$REPO/data/agents" "session-analyst" "$1" "$TS" "$2" 2>/dev/null || true
}

update_status "running" "started"

# Generate session post-mortem
RESULT=$(bash "$REPO/scripts/obsidian_session.sh" "$DATE" 2>&1 || echo "session-analyst: no data")
SUMMARY=$(echo "$RESULT" | grep "\[obsidian_session\]" | head -1 || echo "$RESULT")

cat > "$OUT/last_run.json" << EOF
{"ts":"$TS","date":"$DATE","summary":"$SUMMARY"}
EOF

update_status "idle" "$SUMMARY"
echo "SESSION_ANALYST: $SUMMARY"
