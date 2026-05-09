#!/usr/bin/env bash
# Write a Hermes run note to the Obsidian vault.
# Usage: ./scripts/obsidian_note.sh [--action "what was fixed"]
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
VAULT="$HOME/obsidian/rithmic"
FINDINGS="$REPO/data/hermes_findings.json"
AUDIT="$REPO/data/audit_status.json"

ACTION="${1:-}"

if [[ ! -f "$FINDINGS" ]]; then
  echo "[obsidian_note] No findings file — run make hermes first." >&2
  exit 1
fi

# Parse findings JSON
OVERALL=$(python3 -c "import json; d=json.load(open('$FINDINGS')); print(d['overall'])")
PASS=$(python3 -c "import json; d=json.load(open('$FINDINGS')); print(d['pass'])")
FAIL=$(python3 -c "import json; d=json.load(open('$FINDINGS')); print(d['fail'])")
WARN=$(python3 -c "import json; d=json.load(open('$FINDINGS')); print(d['warn'])")
TS=$(python3 -c "import json; d=json.load(open('$FINDINGS')); print(d['ts'])")

DATE=$(echo "$TS" | cut -c1-10)
TIME=$(echo "$TS" | cut -c12-16)
NOTE_FILE="$VAULT/hermes/${DATE} ${TIME}.md"

# Status emoji
case "$OVERALL" in
  PASS) STATUS="✅ PASS" ;;
  WARN) STATUS="⚠️ WARN" ;;
  FAIL) STATUS="❌ FAIL" ;;
  *)    STATUS="$OVERALL" ;;
esac

# Build per-check lines
CHECKS=$(python3 -c "
import json
d = json.load(open('$FINDINGS'))
for c in d['findings']:
    icon = '✅' if c['status']=='PASS' else ('❌' if c['status']=='FAIL' else '⚠️')
    print(f'- {icon} **{c[\"check\"]}**: {c[\"detail\"]}')
")

# Build audit warnings
AUDIT_WARNS=""
if [[ -f "$AUDIT" ]]; then
  AUDIT_WARNS=$(python3 -c "
import json
try:
  d = json.load(open('$AUDIT'))
  warns = [c for c in d.get('checks',[]) if c['status'] in ('WARN','FAIL')]
  for w in warns:
    icon = '❌' if w['status']=='FAIL' else '⚠️'
    print(f'- {icon} **{w[\"name\"]}**: {w[\"message\"]}')
except: pass
")
fi

# Previous note link
PREV=$(ls "$VAULT/hermes/"*.md 2>/dev/null | tail -1 | xargs basename 2>/dev/null | sed 's/\.md$//' || echo "")

cat > "$NOTE_FILE" << MDEOF
---
date: ${DATE}
time: ${TIME}
overall: ${OVERALL}
pass: ${PASS}
fail: ${FAIL}
warn: ${WARN}
tags: [hermes, $(echo "$OVERALL" | tr '[:upper:]' '[:lower:]')]
---

# Hermes — ${DATE} ${TIME} UTC

## Status: ${STATUS} (${PASS} pass / ${FAIL} fail / ${WARN} warn)

### Checks
${CHECKS}

MDEOF

if [[ -n "$AUDIT_WARNS" ]]; then
  cat >> "$NOTE_FILE" << MDEOF
### Audit warnings
${AUDIT_WARNS}

MDEOF
fi

if [[ -n "$ACTION" ]]; then
  cat >> "$NOTE_FILE" << MDEOF
### Actions taken
${ACTION}

MDEOF
else
  cat >> "$NOTE_FILE" << MDEOF
### Actions taken
None — all clear.

MDEOF
fi

if [[ -n "$PREV" ]]; then
  echo "← [[hermes/${PREV}]]" >> "$NOTE_FILE"
fi

echo "[obsidian_note] Written: $NOTE_FILE"
