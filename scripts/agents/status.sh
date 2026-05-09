#!/usr/bin/env bash
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
python3 - "$REPO/data/agents" << 'PYEOF'
import json, sys, pathlib

base = pathlib.Path(sys.argv[1])
agents = ["coordinator", "ci-watchdog", "deploy-manager", "doc-keeper", "session-analyst", "audit-sentinel"]

print("\n  Hermes Coordinator — Agent Status")
print("  ──────────────────────────────────")
for name in agents:
    f = base / name / "status.json"
    if f.exists():
        d = json.loads(f.read_text())
    else:
        # fallback to top-level status.json for coordinator
        top = base / "status.json"
        if top.exists():
            d = json.loads(top.read_text()).get(name, {})
        else:
            d = {}
    status = d.get("status", "unknown")
    last   = (d.get("last_run") or "never")[11:16]
    result = d.get("last_result") or "—"
    icon   = "🟢" if status == "idle" else "🔵" if status == "running" else "⚪"
    print(f"  {icon} {name:<22} last={last}  {result}")
print()
PYEOF
