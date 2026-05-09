#!/usr/bin/env bash
# CI Watchdog agent — runs hermes, fixes failures, commits fixes.
# Called by coordinator with: bash scripts/agents/ci_watchdog.sh [--fast]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
FAST="${1:-}"
OUT="$REPO/data/agents/ci-watchdog"
TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

update_status() {
  python3 "$REPO/scripts/agents/agent_status.py" \
    "$REPO/data/agents" "ci-watchdog" "$1" "$TS" "$2" 2>/dev/null || true
}

update_status "running" "started"

# Run hermes
if [[ "$FAST" == "--fast" ]]; then
  bash "$REPO/scripts/hermes.sh" --fast > "$OUT/last_run.log" 2>&1 || true
else
  bash "$REPO/scripts/hermes.sh" > "$OUT/last_run.log" 2>&1 || true
fi

OVERALL=$(python3 -c "import json; print(json.load(open('$REPO/data/hermes_findings.json'))['overall'])" 2>/dev/null || echo "UNKNOWN")
cp "$REPO/data/hermes_findings.json" "$OUT/findings.json" 2>/dev/null || true

cat > "$OUT/last_run.json" << EOF
{"ts":"$TS","overall":"$OVERALL","fast":$([ "$FAST" = "--fast" ] && echo true || echo false)}
EOF

update_status "idle" "$OVERALL"
echo "CI_WATCHDOG: $OVERALL"
