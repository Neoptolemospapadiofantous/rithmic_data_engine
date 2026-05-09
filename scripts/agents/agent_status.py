#!/usr/bin/env python3
"""Write agent status to its per-agent status.json file.
Usage: python3 agent_status.py <data_dir> <agent_name> <status> <last_run> <last_result>
"""
import json, sys, pathlib

data_dir, agent_name, status, last_run, last_result = sys.argv[1:6]
f = pathlib.Path(data_dir) / agent_name / "status.json"
f.parent.mkdir(parents=True, exist_ok=True)
f.write_text(json.dumps({
    "status": status,
    "last_run": last_run,
    "last_result": last_result
}))
