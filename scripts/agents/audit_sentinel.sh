#!/usr/bin/env bash
# Audit Sentinel agent — monitors all 17 audit checks, flags thresholds.
# Called by coordinator. Reads audit_status.json, writes findings.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$REPO/data/agents/audit-sentinel"
TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

update_status() {
  python3 "$REPO/scripts/agents/agent_status.py" \
    "$REPO/data/agents" "audit-sentinel" "$1" "$TS" "$2" 2>/dev/null || true
}

update_status "running" "started"

# Parse audit_status.json and produce structured findings
python3 - "$REPO/data/audit_status.json" "$OUT/findings.json" << 'PYEOF'
import json, sys, pathlib

audit_path, out_path = sys.argv[1], sys.argv[2]
d = json.load(open(audit_path))

findings = {"ts": None, "critical": [], "high": [], "medium": [], "healthy": []}
findings["ts"] = d.get("timestamp", "")
findings["overall"] = d.get("overall", "UNKNOWN")
findings["failed"] = d.get("failed", 0)
findings["warned"] = d.get("warned", 0)

for c in d.get("checks", []):
  name = c["name"]
  status = c["status"]
  msg = c["message"]

  if status == "FAIL":
    findings["critical"].append({"check": name, "message": msg})
  elif status == "WARN":
    findings["high"].append({"check": name, "message": msg})
  elif status == "PASS":
    # Extract numeric values for threshold proximity
    import re
    nums = re.findall(r'[\d.]+', msg)
    findings["healthy"].append({"check": name, "message": msg})
  elif status == "INFO":
    if any(w in msg.lower() for w in ["none", "no ", "0 "]):
      findings["healthy"].append({"check": name, "message": msg})
    else:
      findings["medium"].append({"check": name, "message": msg})

pathlib.Path(out_path).write_text(json.dumps(findings, indent=2))

# Print summary
crit = len(findings["critical"])
high = len(findings["high"])
med  = len(findings["medium"])
print(f"AUDIT_SENTINEL: critical={crit} high={high} medium={med} overall={findings['overall']}")
PYEOF

SUMMARY=$(cat "$OUT/findings.json" | python3 -c "
import json,sys
d=json.load(sys.stdin)
print(f'critical={len(d[\"critical\"])} high={len(d[\"high\"])} overall={d[\"overall\"]}')
" 2>/dev/null || echo "parsed")

cat > "$OUT/last_run.json" << EOF
{"ts":"$TS","summary":"$SUMMARY"}
EOF

update_status "idle" "$SUMMARY"
