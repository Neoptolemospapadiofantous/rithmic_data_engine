#!/usr/bin/env bash
# collector_watchdog.sh — external belt-and-braces for the 24/7 tick collector.
# Runs from the rithmic-collector-watchdog.timer (every 2 min). Independent of the
# collector's own in-process link watchdog: if the unit is dead, or the newest tick
# in Postgres is older than STALE_S while the CME Globex market is open, it restarts
# the unit and alerts once through grid-notify (and once more on recovery).
# Also rotates the append-only logs daily so a runaway log can never fill the disk.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
UNIT="rithmic-collector-local"; STALE_S="${COLLECTOR_STALE_S:-120}"; STATE="data/validation/collector_watchdog.json"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="$PG_PASSWORD"
mkdir -p data/validation
notify() { for b in "$HOME/.local/bin/grid-notify" "$HOME/.config/ecosystem/bin/grid-notify"; do [[ -x "$b" ]] && { "$b" "$1" >/dev/null 2>&1 || true; return; }; done; }

# CME Globex equity futures: open Sun 18:00 ET → Fri 17:00 ET, daily halt 17:00–18:00 ET.
market_open() {
  local dow hm; dow=$(TZ=America/New_York date +%u); hm=$((10#$(TZ=America/New_York date +%H%M)))  # 10#: "0718" is not octal
  [[ $dow -eq 6 ]] && return 1                                   # Saturday
  [[ $dow -eq 7 && $hm -lt 1800 ]] && return 1                   # Sunday before reopen
  [[ $dow -eq 5 && $hm -ge 1700 ]] && return 1                   # Friday after close
  [[ $hm -ge 1700 && $hm -lt 1800 ]] && return 1                 # daily halt (give it until 18:02)
  [[ $hm -ge 1800 && $hm -lt 1803 ]] && return 1
  return 0
}

prev=$(cat "$STATE" 2>/dev/null | grep -o '"failing": *[a-z]*' | grep -o '[a-z]*$'); prev=${prev:-false}
active=$(systemctl --user is-active "$UNIT" 2>/dev/null)
age=$(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -c \
      "SELECT COALESCE(EXTRACT(EPOCH FROM now()-max(ts_event)),1e9)::int FROM ticks WHERE symbol='NQ' AND ts_event > now()-interval '1 day'" 2>/dev/null || echo 1000000000)
open=false; market_open && open=true
failing=false; reason=""
if [[ "$active" != "active" ]]; then failing=true; reason="unit $active"
elif [[ "$open" == true && "$age" -gt "$STALE_S" ]]; then failing=true; reason="newest NQ tick ${age}s old while the market is open"; fi
action="none"
if [[ "$failing" == true ]]; then
  systemctl --user restart "$UNIT" && action="restarted $UNIT"
  [[ "$prev" != true ]] && notify "🔴 collector watchdog: $reason — $action"
elif [[ "$prev" == true ]]; then
  notify "🟢 collector watchdog: recovered (tick age ${age}s)"
fi
# daily log rotation (append-only files: copy + truncate keeps the writer's fd valid)
rot="data/validation/.last_logrotate"; today=$(date +%F)
if [[ "$(cat "$rot" 2>/dev/null)" != "$today" ]]; then
  for f in data/logs/collector.log data/logs/paper_engine.log data/logs/nq_executor_tradeify.log data/logs/paper_replay.log; do
    [[ -f "$f" ]] || continue
    if [[ $(stat -c %s "$f") -gt $((50*1024*1024)) ]]; then cp "$f" "$f.1" && : > "$f"; gzip -f "$f.1" 2>/dev/null || true; fi
  done
  echo "$today" > "$rot"
fi
# Memory-pressure alert (2026-09-23: a browser-filled box hit the kernel OOM killer while the
# live executor was up). Once per episode, same as the feed alert above.
mem_flag="data/validation/.mem_low"
read -r avail swapt swapf < <(awk '/^MemAvailable:/{a=$2}/^SwapTotal:/{t=$2}/^SwapFree:/{f=$2}END{print a,t,f}' /proc/meminfo)
avail_mb=$((avail/1024)); swap_pct=$(( swapt > 0 ? (swapt-swapf)*100/swapt : 0 ))
mem_low=false
if (( avail_mb < ${MEM_MIN_AVAIL_MB:-2048} )) || (( swap_pct >= 95 && avail_mb < 4096 )); then mem_low=true; fi
if [[ "$mem_low" == true && ! -f "$mem_flag" ]]; then
  notify "🟠 trading box memory low: ${avail_mb} MB available, swap ${swap_pct}% — close browser tabs (executor $(systemctl --user is-active nq-executor-local@tradeify 2>/dev/null))"
  touch "$mem_flag"
elif [[ "$mem_low" == false && -f "$mem_flag" ]]; then
  notify "🟢 trading box memory recovered: ${avail_mb} MB available, swap ${swap_pct}%"; rm -f "$mem_flag"
fi
printf '{"ts":"%s","unit":"%s","market_open":%s,"tick_age_s":%s,"failing":%s,"reason":"%s","action":"%s","mem_avail_mb":%s,"swap_pct":%s,"mem_low":%s}\n' \
  "$(date -Is)" "$active" "$open" "$age" "$failing" "$reason" "$action" "$avail_mb" "$swap_pct" "$mem_low" > "$STATE"
