#!/usr/bin/env bash
# stack_status.sh — one index of every trading process on this box (make stack-status).
#
# Lists each systemd user unit of the local stack with its pid, memory and OOM exposure, the feed
# and executor health, memory headroom, and — the part that matters most — STRAY processes: any
# nq_executor / rithmic_engine / paper_engine that is NOT the unit's own MainPID. A stray executor
# trades the same account behind the unit's back; a stray collector fights the unit for the one
# Rithmic market-data session (force-logout loop). Exit 1 when anything needs attention, so this
# doubles as the pre-flight gate before a live session.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
ACCOUNT="${1:-tradeify}"
bad=0; warn() { printf '  \033[31m✗ %s\033[0m\n' "$1"; bad=1; }
ok()   { printf '  \033[32m✓\033[0m %s\n' "$1"; }

mb() { awk '/^VmRSS:/{printf "%d", $2/1024}' "/proc/$1/status" 2>/dev/null || echo 0; }

echo "== units"
declare -A MAINPID
# every extra paper fleet config/paper_fleet_<code>.json runs in unit paper-engine-local-<code> (2026-10-01)
PAPER_UNITS=(paper-engine-local)
for f in config/paper_fleet_*.json; do [[ -f "$f" ]] || continue; c=${f#config/paper_fleet_}; PAPER_UNITS+=("paper-engine-local-${c%.json}"); done
for u in rithmic-collector-local "${PAPER_UNITS[@]}" "nq-executor-local@$ACCOUNT" dashboard-api-local; do
  st=$(systemctl --user is-active "$u" 2>/dev/null); pid=$(systemctl --user show -p MainPID --value "$u" 2>/dev/null)
  MAINPID[$u]=${pid:-0}
  if [[ "$st" == active && "${pid:-0}" != 0 ]]; then
    printf '  %-32s %-9s pid=%-8s rss=%5sMB oom_adj=%s oom_score=%s\n' "$u" "$st" "$pid" "$(mb "$pid")" \
      "$(cat /proc/$pid/oom_score_adj)" "$(cat /proc/$pid/oom_score)"
    [[ $(cat /proc/$pid/oom_score_adj) -gt 100 && "$u" != dashboard-api-local ]] && warn "$u oom_score_adj>100 — reinstall deploy/ units and restart it"
  else
    printf '  %-32s %s\n' "$u" "${st:-not-installed}"
  fi
done
[[ $(systemctl --user is-active rithmic-collector-local) == active ]] || warn "collector not active — no market data"

echo "== strays (processes not owned by their unit)"
stray=0
chk() { # name unit... — a process is owned if it is the MainPID of ANY of the listed units
  local name=$1 p u owned; shift
  for p in $(pgrep -x "$name"); do
    owned=0; for u in "$@"; do [[ "$p" == "${MAINPID[$u]:-0}" ]] && owned=1; done
    (( owned )) && continue
    warn "stray $name pid=$p: $(tr '\0' ' ' < /proc/$p/cmdline | cut -c1-120)"; stray=1
  done; }
chk rithmic_engine rithmic-collector-local
chk paper_engine "${PAPER_UNITS[@]}"
# every executor that is not the account unit's MainPID is a second writer candidate
for p in $(pgrep -x nq_executor); do
  [[ "$p" == "${MAINPID[nq-executor-local@$ACCOUNT]:-0}" ]] && continue
  u=$(grep -o 'nq-executor-local@[^/]*\.service' /proc/$p/cgroup 2>/dev/null)
  if [[ -n "$u" ]]; then printf '  · %s pid=%s (unit %s)\n' nq_executor "$p" "$u"
  else warn "stray nq_executor pid=$p: $(tr '\0' ' ' < /proc/$p/cmdline | cut -c1-120)"; stray=1; fi
done
[[ $stray == 0 ]] && ok "none"

echo "== feed + executor"
age=$(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -c \
  "SELECT COALESCE(EXTRACT(EPOCH FROM now()-max(ts_event)),-1)::int FROM ticks WHERE symbol='NQ' AND ts_event > now()-interval '1 day'" 2>/dev/null)
echo "  newest NQ tick: ${age:-?}s ago (Globex halts 17:00–18:00 ET daily)"
log="data/logs/nq_executor_${ACCOUNT}.log"
if [[ -f "$log" ]]; then
  echo "  executor log last write: $(( $(date +%s) - $(stat -c %Y "$log") ))s ago"
  b=$(grep -a '\[BROKER\]' "$log" | tail -1 | cut -c1-140); echo "  last broker line: ${b:-none}"
fi
[[ -f NO_DEPLOY ]] && echo "  NO_DEPLOY present — the executor unit refuses to start"
grep -hE '"session_open_(hour|min)"|"qty"|"max_daily_trades"|"orb_minutes"' "config/${ACCOUNT}_config.json" | tr -d ' \n'; echo

echo "== memory"
read -r avail swapt swapf < <(awk '/^MemAvailable:/{a=$2}/^SwapTotal:/{t=$2}/^SwapFree:/{f=$2}END{print a,t,f}' /proc/meminfo)
avail_mb=$((avail/1024)); swap_pct=$(( swapt > 0 ? (swapt-swapf)*100/swapt : 0 ))
echo "  available ${avail_mb} MB, swap ${swap_pct}%"
(( avail_mb < 2048 )) && warn "under 2 GB available — close browser tabs before trading"
chrome_in=0; chrome_out=0
for p in $(pgrep chrome); do r=$(mb "$p"); if grep -q browsers.slice "/proc/$p/cgroup" 2>/dev/null; then chrome_in=$((chrome_in+r)); else chrome_out=$((chrome_out+r)); fi; done
echo "  chrome: ${chrome_in} MB inside browsers.slice (capped), ${chrome_out} MB outside (uncapped)"
(( chrome_out > 4096 )) && echo "  ! quit Chrome fully and relaunch it from the dock to move it under the 10 GB cap"

exit $bad
