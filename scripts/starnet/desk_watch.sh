#!/usr/bin/env bash
# desk_watch.sh — health watchdog + housekeeping for the StarNet strategy desk.
#
#   desk_watch.sh check     (cron hourly :20) alert through grid-notify (Telegram) ONCE when a condition
#                           turns bad, and once more when it recovers. Silent while everything is fine.
#   desk_watch.sh cleanup   (cron nightly 03:40) prune desk output older than KEEP_DAYS.
#
# Conditions checked:
#   starnet   — the sidecar answers /api/health            adapter  — the adapter answers /health
#   cron      — StarNet's scheduler is armed and ticked in the last 5 min
#   stale     — no new desk file for 5 h (a 2-hour loop; allows one missed cycle)
#   taint     — a scheduled run in the last 2 h lost its terminal (taintedBy set): the lockout bug class
#   limit     — the adapter is backing off after a Claude subscription-limit refusal
#   pace      — the adapter's rolling-hour pace cap is spent
#   nightshift— StarNet's autonomy posture acts unattended again (the page can push the old setting back)
#   roster / goal — a stale StarNet tab overwrote the crew (HERMES missing) or the Tradeify goal
# Fail-open: a watchdog that cannot read something reports it, it never throws or blocks.
set -uo pipefail
MODE="${1:-check}"
WORK="${DESK_WORK:-$HOME/starnet-work}"
STATE="${DESK_WATCH_STATE:-$HOME/.local/share/rithmic-desk/watch}"
KEEP_DAYS="${DESK_KEEP_DAYS:-14}"
STARNET="${STARNET_URL:-http://127.0.0.1:8787}"
ADAPTER="${STARNET_ADAPTER_URL:-http://127.0.0.1:8099}"
mkdir -p "$STATE"
notify() { command -v grid-notify >/dev/null 2>&1 && grid-notify "$1" >/dev/null 2>&1; echo "[desk-watch] $(date -Is) $1"; }

# edge-triggered: alert on ok->bad, and on bad->ok; nothing while the state is unchanged
flag() {   # flag <name> <bad:0|1> <message when bad>
  local f="$STATE/$1.bad"
  if [[ "$2" == 1 ]]; then
    [[ -f "$f" ]] || { echo "$3" > "$f"; notify "⚠️ rithmic desk: $3"; }
  elif [[ -f "$f" ]]; then
    rm -f "$f"; notify "✅ rithmic desk: $1 recovered"
  fi
}

if [[ "$MODE" == cleanup ]]; then
  n=$(find "$WORK/desk" "$WORK/findings" "$WORK/holdout" -maxdepth 1 -name '20*.md' -mtime +"$KEEP_DAYS" -print -delete 2>/dev/null | wc -l)
  m=$(find "$HOME/.local/share/rithmic-desk" -maxdepth 1 -type d -name '20*' -mtime +7 -print -exec rm -rf {} + 2>/dev/null | wc -l)
  echo "[desk-watch] $(date -Is) cleanup: $n desk files older than ${KEEP_DAYS}d, $m replay work dirs older than 7d"
  exit 0
fi

# a deliberate pause (e.g. waiting out the weekly Claude limit) silences the loop checks; it is cleared by
# whatever resumes the desk. The file's content is the reason, shown once.
PAUSE="$HOME/.local/share/rithmic-desk/PAUSED"
if [[ -f "$PAUSE" ]]; then
  flag paused 1 "desk paused on purpose: $(cat "$PAUSE")"
  exit 0
fi
flag paused 0 "desk paused"

# starnet + its per-launch token (injected into the served page)
page=$(curl -s -m 10 "$STARNET/" || true)
tok=$(grep -oE '__STARNET_API_TOKEN__="[0-9a-f]+' <<<"$page" | cut -d'"' -f2)
flag starnet "$([[ -n "$tok" ]] && echo 0 || echo 1)" "StarNet (:8787) is not answering — the desk is stopped (systemctl --user status starnet)"

ah=$(curl -s -m 10 "$ADAPTER/health" || true)
flag adapter "$([[ "$ah" == *'"ok": true'* ]] && echo 0 || echo 1)" "starnet-adapter (:8099) is not answering (systemctl --user status starnet-adapter)"

if [[ -n "$tok" ]]; then
  cron=$(curl -s -m 10 -H "x-starnet-token: $tok" "$STARNET/api/cron" || true)
  bad=$(printf '%s' "$cron" | python3 -c '
import json, sys, time
try:
    d = json.load(sys.stdin); t = (d.get("health") or {}).get("lastTickAt") or 0
    print(0 if d.get("enabled") and time.time() * 1000 - t < 300000 else 1)
except Exception:
    print(1)
')
  flag cron "$bad" "StarNet's scheduler is not armed or stopped ticking — no job will run (POST /api/cron/arm {\"enabled\":true})"

  post=$(curl -s -m 10 -H "x-starnet-token: $tok" "$STARNET/api/autonomy/posture" || true)
  flag nightshift "$([[ "$post" == *'"actsUnattended": true'* || "$post" == *'"actsUnattended":true'* ]] && echo 1 || echo 0)" \
       "Night Shift is acting unattended again (autonomy posture reverted) — set initiative back to 'propose'"
fi

# a STALE StarNet window can push its older roster/goal over the station's (seen 2026-09-30: HERMES vanished
# and StarNet's auto-made goal replaced the Tradeify goal) — reload open StarNet tabs, then re-add what was lost
WS="$HOME/.local/share/StarNet/workspaces"
roster=$(python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); print(len(r["agents"]), int("HERMES" in [a["name"] for a in r["agents"]]))' "$WS/agent.roster.json" 2>/dev/null || echo "0 0")
flag roster "$([[ "${roster%% *}" -ge 15 && "${roster##* }" == 1 ]] && echo 0 || echo 1)" \
     "the crew roster lost agents (${roster%% *} agents, HERMES present=${roster##* }) — a stale StarNet tab overwrote it; reload tabs and re-add"
goal=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["goal"]["text"])' "$WS/_commander.goals.json" 2>/dev/null || echo "")
flag goal "$([[ "$goal" == *Tradeify* ]] && echo 0 || echo 1)" "the station's active goal is no longer the Tradeify goal (now: ${goal:0:80}) — refocus it"

newest=$(find "$WORK/desk" -maxdepth 1 -name '20*.md' -printf '%T@\n' 2>/dev/null | sort -n | tail -1)
age=$(( $(date +%s) - ${newest%.*} ))
flag stale "$(( age > 18000 ? 1 : 0 ))" "no new desk file for $(( age / 3600 )) h — the loop is not producing"

# a scheduled run that lost its terminal in the last 2 h (runs.jsonl carries taintedBy)
tainted=$(python3 - <<'PY'
import json, os, time
p = os.path.expanduser("~/.local/share/StarNet/workspaces/runs.jsonl"); n = 0; src = set()
try:
    with open(p) as fh:
        lines = fh.readlines()[-400:]
    for l in lines:
        try: d = json.loads(l)
        except Exception: continue
        if d.get("surface") == "autonomous" and d.get("taintedBy") and time.time() * 1000 - (d.get("startedAt") or 0) < 7200000:
            n += 1; src.add(str(d.get("taintedBy")))
except OSError:
    pass
print(f"{n} {'; '.join(sorted(src))}")
PY
)
flag taint "$([[ "${tainted%% *}" -gt 0 ]] && echo 1 || echo 0)" \
     "${tainted%% *} scheduled run(s) lost their terminal in the last 2 h (taint: ${tainted#* }) — agents cannot write files"

if [[ "$ah" == *'"ok": true'* ]]; then
  backoff=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1]).get("limit_backoff_s", 0))' "$ah" 2>/dev/null || echo 0)
  flag limit "$(( backoff > 0 ? 1 : 0 ))" "Claude subscription limit reached — the desk is paused until it resets (adapter backing off ${backoff}s)"
  read -r used cap < <(python3 -c 'import json,sys; d=json.loads(sys.argv[1]); print(d.get("calls_last_hour",0), d.get("hourly_cap",0))' "$ah" 2>/dev/null || echo "0 0")
  flag pace "$(( cap > 0 && used >= cap ? 1 : 0 ))" "hourly pace cap spent ($used/$cap calls in the last hour) — runs are being refused until it rolls"
fi
exit 0
