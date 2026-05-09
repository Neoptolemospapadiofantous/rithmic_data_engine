#!/usr/bin/env bash
# Generate or update today's daily note in the Obsidian vault.
# Aggregates hermes runs, session summaries, and open warnings for the day.
set -euo pipefail

VAULT="$HOME/obsidian/rithmic"
DATE=$(date -u +"%Y-%m-%d")
NOTE="$VAULT/daily/${DATE}.md"

# Count today's hermes runs
HERMES_COUNT=$(ls "$VAULT/hermes/${DATE} "*.md 2>/dev/null | wc -l || echo 0)
LAST_STATUS=$(ls "$VAULT/hermes/${DATE} "*.md 2>/dev/null | tail -1 | \
  xargs -I{} python3 -c "
import re
content = open('{}').read()
m = re.search(r'overall: (\w+)', content)
print(m.group(1) if m else 'UNKNOWN')
" 2>/dev/null || echo "NONE")

case "$LAST_STATUS" in
  PASS) ICON="✅" ;;
  WARN) ICON="⚠️" ;;
  FAIL) ICON="❌" ;;
  *)    ICON="—"  ;;
esac

# Check for open warnings from latest hermes run
AUDIT="$(dirname "$0")/../data/audit_status.json"
WARNS=""
if [[ -f "$AUDIT" ]]; then
  WARNS=$(python3 -c "
import json
try:
  d = json.load(open('$AUDIT'))
  ws = [c for c in d.get('checks',[]) if c['status'] in ('WARN','FAIL')]
  for w in ws:
    icon = '❌' if w['status']=='FAIL' else '⚠️'
    print(f'- {icon} **{w[\"name\"]}**: {w[\"message\"]}')
except: pass
" 2>/dev/null)
fi

if [[ ! -f "$NOTE" ]]; then
  cat > "$NOTE" << MDEOF
---
date: ${DATE}
tags: [daily]
---

# ${DATE}

## Hermes runs today
| Time | Status |
|------|--------|
MDEOF
fi

# Append today's run summary rows from hermes notes
python3 - "$VAULT/hermes" "$DATE" "$NOTE" << 'PYEOF'
import sys, os, re, pathlib

hermes_dir, date, note_path = sys.argv[1], sys.argv[2], sys.argv[3]
notes = sorted(pathlib.Path(hermes_dir).glob(f"{date} *.md"))
rows = []
for n in notes:
  content = n.read_text()
  time_m   = re.search(r'time: (\d{2}:\d{2})', content)
  overall_m = re.search(r'overall: (\w+)', content)
  pass_m   = re.search(r'pass: (\d+)', content)
  fail_m   = re.search(r'fail: (\d+)', content)
  warn_m   = re.search(r'warn: (\d+)', content)
  if time_m and overall_m:
    icon = {'PASS': '✅', 'WARN': '⚠️', 'FAIL': '❌'}.get(overall_m.group(1), '—')
    p = pass_m.group(1) if pass_m else '?'
    f = fail_m.group(1) if fail_m else '?'
    w = warn_m.group(1) if warn_m else '?'
    rows.append(f"| {time_m.group(1)} UTC | {icon} {overall_m.group(1)} | {p}p {f}f {w}w |")

if rows:
  content = pathlib.Path(note_path).read_text()
  table_header = "| Time | Status |\n|------|--------|"
  new_table = "| Time | Status | Counts |\n|------|--------|--------|\n" + "\n".join(rows)
  if "| Time | Status |" in content:
    content = content.replace(table_header, new_table)
  pathlib.Path(note_path).write_text(content)
PYEOF

# Append open warnings section if not present
if [[ -n "$WARNS" ]] && ! grep -q "## Open audit warnings" "$NOTE" 2>/dev/null; then
  cat >> "$NOTE" << MDEOF

## Open audit warnings
${WARNS}
MDEOF
fi

# Ensure Notes section exists
if ! grep -q "^## Notes" "$NOTE" 2>/dev/null; then
  echo -e "\n## Notes\n" >> "$NOTE"
fi

echo "[obsidian_daily] Updated: $NOTE  (hermes runs: $HERMES_COUNT, last: $ICON$LAST_STATUS)"
