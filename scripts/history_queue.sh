#!/usr/bin/env bash
# history_queue.sh — the resumable history job queue (NQ + crypto), run over a weekend.
#
#   usage: scripts/history_queue.sh run [--parallel 2] [--parallel-closed 6]   (start in the background: nohup … &)
#          scripts/history_queue.sh status                  (prints data/history/queue_status.json)
#          scripts/history_queue.sh list                    (the job list in queue order)
#
# Jobs (one per market × month, see scripts/history_month.sh):
#   flow 2023-03 … 2026-03         NQ order flow per minute from the MBO files (dbn_tool mbo-1m → mbo_1m GLBX/NQ,
#                                  book_1m, bars_1m buy_volume), ~1 min each
#   nq  2026-10, 2026-09           collector ticks in Postgres (label nq_hist)
#   nq  2026-03 … 2023-03          Databento MBO files      (label nq_hist), newest first
#   nqx 2026-10 … 2023-03          night grid + staged winners/new modes on the same NQ data (label nq_hist)
#   nqbars 2023-02 … 2010-07       NQ 1-minute bars as 4 synthetic prints each (label nq_bars, approximate)
#   crypto_download                scripts/binance_history_download.sh BTCUSDT,ETHUSDT 2020-01 (network,
#                                  runs beside the replay slots from the start)
#   crypto_bars                    dbn_tool agg-bars for every downloaded month (both coins) + session_stats
#                                  refresh — the regime gate's ATR and the BTC↔ETH reference bars
#   btc/eth 2026-10 … 2020-01      Binance aggTrades files (label crypto_hist), newest first, alternating
# Done/no-data markers: data/history/done/<market>/<YYYY-MM>.{done,nodata}; a rerun skips them.
#
# Rules: at most --parallel (default 2) history processes on the WHOLE box at once (paper_engine --replay-from of
# anyone + dbn_tool mbo-1m/agg-bars), each under nice 15 + ionice idle. Guarded windows
# (weekday 09:25–16:05 ET, Asia 23:55–02:35 ET except Saturday — the live executor, collectors and the Asia
# rota share the box): no job STARTS inside one, and running replays are paused with SIGSTOP and resumed
# with SIGCONT when it ends. Crypto replays wait while another session's Postgres replay of crypto_hist
# (paper_engine … crypto_hist without --binance-agg) still runs.
# Progress + ETA: data/logs/history_queue.log; machine-readable: data/history/queue_status.json.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
CMD="${1:-status}"; shift || true
PAR=2; PAR_CLOSED="${HISTORY_PAR_CLOSED:-6}"   # more slots while CME is shut (Fri 17:00 → Sun 18:00 ET)
while [[ $# -gt 0 ]]; do case "$1" in --parallel) PAR="$2"; shift;; --parallel-closed) PAR_CLOSED="$2"; shift;; *) echo "unknown arg $1"; exit 2;; esac; shift; done
H="data/history"; LOG="data/logs/history_queue.log"; STATUS="$H/queue_status.json"; PIDS="$H/pids"
mkdir -p "$H/done" "$PIDS" data/logs
say() { echo "$(date -Is) $*" | tee -a "$LOG" >&2; }

cme_closed() {    # weekend: Fri 17:00 ET → Sun 18:00 ET (no live trading, NQ collector idle) — founder 2026-10-11 "trade on
                  # all the data we have": use the box then. Running jobs above the weekday cap finish; none start.
  local dow hm; dow=$(TZ=America/New_York date +%u); hm=$((10#$(TZ=America/New_York date +%H%M)))
  (( dow == 6 )) || (( dow == 5 && hm >= 1700 )) || (( dow == 7 && hm < 1800 ))
}
slots() { if cme_closed; then echo "$PAR_CLOSED"; else echo "$PAR"; fi; }

busy_window() {   # same windows as crypto_fleet.sh / fleet_expand.sh
  local dow hm; dow=$(TZ=America/New_York date +%u); hm=$((10#$(TZ=America/New_York date +%H%M)))
  (( dow <= 5 && hm >= 925 && hm < 1605 )) && { echo "regular hours"; return 0; }
  (( hm >= 2355 || hm < 235 )) && (( dow != 6 )) && { echo "Asia session"; return 0; }
  return 1
}

mbo_months() { ls /home/theone/Desktop/bot/data/raw/mbo/*.dbn.zst | xargs -n1 basename | cut -c1-7 | sort -u; }
jobs_list() {   # priority file first (data/history/priority.txt, read every loop — reorder without a restart), no duplicates
  { [[ -f "$H/priority.txt" ]] && grep -Ev "^\s*(#|$)" "$H/priority.txt"; base_jobs; } | awk "!seen[\$0]++"
}
base_jobs() {
  local f; for f in $(mbo_months); do echo "flow $f"; done   # NQ order flow per minute (dbn_tool mbo-1m, ~1 min each)
  echo "nq 2026-10"; echo "nq 2026-09"
  # crypto bars as soon as the download is complete, then one month per coin early (measures the crypto
  # per-month cost for the ETA), then the NQ MBO months, then the rest of crypto, newest first
  echo "crypto_bars -"; echo "btc 2026-09"; echo "eth 2026-09"
  local m="2026-03"; while [[ "$m" > "2023-02" ]]; do echo "nq $m"; m=$(date -u -d "$m-01 -1 month" +%Y-%m); done
  # nqx = NQ strategy sets outside the main fleet (night grid, staged winners + new modes), same months (2026-10-11)
  echo "nqx 2026-10"; echo "nqx 2026-09"
  m="2026-03"; while [[ "$m" > "2023-02" ]]; do echo "nqx $m"; m=$(date -u -d "$m-01 -1 month" +%Y-%m); done
  # nqbars = NQ 2023-02 … 2010-07 from 1-minute bars only (4 synthetic prints/bar, label nq_bars), newest first
  m="2023-02"; while [[ "$m" > "2010-06" ]]; do echo "nqbars $m"; m=$(date -u -d "$m-01 -1 month" +%Y-%m); done
  # 2026-10-11 founder "que all": bars-vs-ticks check on the MBO months, ES (ticks + bars), crypto level variants
  # for the month replayed before they existed — then the rest of crypto — then the 12,031 combos (largest, last)
  m="2026-03"; while [[ "$m" > "2023-02" ]]; do echo "nqbcheck $m"; m=$(date -u -d "$m-01 -1 month" +%Y-%m); done
  echo "es 2026-10"; echo "es 2026-09"
  m="2026-03"; while [[ "$m" > "2010-06" ]]; do echo "esbars $m"; m=$(date -u -d "$m-01 -1 month" +%Y-%m); done
  echo "btclv 2026-09"; echo "ethlv 2026-09"
  m=$(date -u +%Y-%m); while [[ "$m" > "2019-12" ]]; do [[ "$m" == 2026-09 ]] || { echo "btc $m"; echo "eth $m"; }; m=$(date -u -d "$m-01 -1 month" +%Y-%m); done
  echo "nqcombo 2026-10"; echo "nqcombo 2026-09"
  m="2026-03"; while [[ "$m" > "2023-02" ]]; do echo "nqcombo $m"; m=$(date -u -d "$m-01 -1 month" +%Y-%m); done
}
job_state() {   # done | nodata | pending
  local mk="$1" m="$2"
  if [[ "$mk" == crypto_bars ]]; then [[ -f "$H/done/crypto_bars.done" ]] && echo done || echo pending; return; fi
  if [[ "$mk" == flow ]]; then [[ -f "$H/done/flow/$m.done" ]] && echo done || echo pending; return; fi
  if [[ -f "$H/done/$mk/$m.done" || ( "$mk" == nq && -f "data/history_replay/nq_hist/$m.done" ) ]]; then echo done
  elif [[ -f "$H/done/$mk/$m.nodata" ]]; then echo nodata; else echo pending; fi
}

crypto_bars() {   # all downloaded months, both coins, then session_stats
  local s f mk; local n=0
  for s in BTCUSDT ETHUSDT; do
    mkdir -p "$H/done/bars_$s"
    for f in "$H/binance/$s"/*.zip; do
      [[ -f "$f.ok" ]] || continue
      mk="$H/done/bars_$s/$(basename "$f").done"
      [[ -f "$mk" ]] && continue
      nice -n 15 ionice -c3 build-exp/dbn_tool agg-bars --symbol "$s" "$f" >> data/logs/history/crypto_bars.log 2>&1 && touch "$mk" && ((n++))
    done
  done
  set -a; . ./.env 2>/dev/null; set +a
  PGPASSWORD="${PG_PASSWORD:-}" psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq \
    -c "SET work_mem='256MB'; SELECT refresh_session_stats(((current_date - date '2019-12-25'))::int);" >> data/logs/history/crypto_bars.log 2>&1
  say "crypto_bars: $n file(s) rolled into bars_1m, session_stats refreshed"
}

write_status() {
  local running_json="$1" now; now=$(date +%s)
  local total=0 done=0 nodata=0 pending=0 line mk m st
  local rows=""
  while read -r mk m; do
    st=$(job_state "$mk" "$m"); ((total++))
    case "$st" in done) ((done++));; nodata) ((nodata++));; *) ((pending++));; esac
    rows+="{\"job\":\"$mk $m\",\"state\":\"$st\"},"
  done < <(jobs_list)
  # mean seconds per market from done markers → ETA (pending replays / parallel slots)
  local avg_nq avg_nqx avg_nqb avg_btc avg_eth
  avg_nq=$(cat "$H"/done/nq/*.done data/history_replay/nq_hist/*.done 2>/dev/null | jq -s '[.[] | select(.source != "pg") | .secs] | if length>0 then add/length else 7200 end')
  avg_btc=$(cat "$H"/done/btc/*.done 2>/dev/null | jq -s '[.[].secs] | if length>0 then add/length else 36000 end')
  avg_nqb=$(cat "$H"/done/nqbars/*.done 2>/dev/null | jq -s '[.[].secs] | if length>0 then add/length else 930 end')   # ~8,300 strategies × a bar month
  avg_nqx=$(cat "$H"/done/nqx/*.done 2>/dev/null | jq -s --argjson d "$avg_nq" '[.[] | select(.source != "pg") | .secs] | if length>0 then add/length else ($d * 0.36) end')
  avg_eth=$(cat "$H"/done/eth/*.done 2>/dev/null | jq -s '[.[].secs] | if length>0 then add/length else 36000 end')
  local pn=0 px=0 pq=0 pb=0 pe=0 po=""
  while read -r mk m; do
    [[ "$(job_state "$mk" "$m")" == pending ]] || continue
    case "$mk" in nq) ((pn++));; nqx) ((px++));; nqbars) ((pq++));; btc) ((pb++));; eth) ((pe++));;
                   nqbcheck|es|esbars|nqcombo|btclv|ethlv) po+="$mk ";; esac
  done < <(jobs_list)
  # the other job types: measured mean when a month is done, else a size-scaled guess (strategies × feed)
  local osecs=0 oc="" mk2 n2 d2 a2
  for mk2 in nqbcheck es esbars nqcombo btclv ethlv; do
    n2=$(grep -ow "$mk2" <<< "$po" | wc -l); (( n2 )) || continue; oc+="\"$mk2\":$n2,"
    case $mk2 in nqbcheck) d2=$avg_nqb;; es) d2=1200;; esbars) d2=180;; nqcombo) d2=$(jq -n "$avg_nq * 2.48");; btclv) d2=$(jq -n "$avg_btc * 0.26");; ethlv) d2=$(jq -n "$avg_eth * 0.26");; esac
    a2=$(cat "$H"/done/$mk2/*.done 2>/dev/null | jq -s --argjson d "$d2" '[.[].secs] | if length>0 then add/length else $d end')
    osecs=$(jq -n "$osecs + $n2 * $a2 | floor")
  done
  # crypto: cost ∝ ticks (≈ 1,000 ticks/s for the 4,845-strategy fleet, measured on NQ MBO and the Postgres crypto
  # replay; replaced by the measured rate once crypto months are done). Ticks per month: crypto_month_ticks.txt
  # (from the agg-bars pass). Pending crypto months without a count use avg_btc/avg_eth.
  local rate; rate=$(cat "$H"/done/btc/*.done "$H"/done/eth/*.done 2>/dev/null | jq -s '[.[] | select(.ticks != null and .secs > 0)] | if length > 0 then (map(.ticks) | add) / (map(.secs) | add) else 1000 end')
  local csecs=0 ct
  if [[ -f "$H/crypto_month_ticks.txt" ]]; then
    csecs=$(while read -r mk m; do [[ ( "$mk" == btc || "$mk" == eth ) && "$(job_state "$mk" "$m")" == pending ]] && echo "$mk $m"; done < <(jobs_list) \
            | awk -v r="$rate" 'NR==FNR { t[$1" "$2]=$3; next } { s += ($0 in t) ? t[$0] / r : 36000 } END { printf "%d", s * 6573 / 4845 }' "$H/crypto_month_ticks.txt" -)
    pb=0; pe=0
  fi
  local eta; eta=$(jq -n "($pn * $avg_nq + $px * $avg_nqx + $pq * $avg_nqb + $pb * $avg_btc + $pe * $avg_eth + $csecs + $osecs) / $PAR | floor")
  local paused; paused=$(busy_window || true)
  printf '{"updated":"%s","parallel":%s,"paused_for":"%s","total":%d,"done":%d,"nodata":%d,"pending":%d,"pending_by_market":{"nq":'"$pn"',"nqx":'"$px"',"nqbars":'"$pq"',"other":{'"${oc%,}"'},"other_secs":'"$osecs"',"btc":'"$pb"',"eth":'"$pe"'},"avg_secs":{"nq_mbo":%s,"btc":%s,"eth":%s},"crypto_ticks_per_sec":'"$rate"',"crypto_pending_secs":'"$csecs"',"eta_hours":%s,"eta_finish":"%s","running":[%s],"jobs":[%s]}\n' \
    "$(date -Is)" "$PAR" "$paused" "$total" "$done" "$nodata" "$pending" "$avg_nq" "$avg_btc" "$avg_eth" \
    "$(jq -n "$eta/3600*10|floor/10")" "$(date -Is -d "@$(( now + eta ))")" "$running_json" "${rows%,}" > "$STATUS.tmp" && mv "$STATUS.tmp" "$STATUS"
}

# Global cap (coordinator 2026-10-10): every history replay process on the box counts — paper_engine replays
# (ours or anyone's, e.g. the weekday slot2/night evals) and dbn_tool mbo-1m / agg-bars.
# Matched on the real binaries (pgrep -x) + their argv, never on a shell's text (a waiting shell whose command
# line merely mentions "paper_engine --replay-from" must not count — that stalled the queue on its first run).
procs_matching() {   # $1 binary name, $2 ERE on its argv → PIDs
  local p; for p in $(pgrep -x "$1"); do tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null | grep -qE -- "$2" && echo "$p"; done
}
active_procs() { echo $(( $(procs_matching paper_engine "--replay-from" | wc -l) + $(procs_matching dbn_tool "mbo-1m|agg-bars" | wc -l) )); }
# a job just launched has not spawned its engine yet, so the process count lags by a few seconds: take the larger of
# live engines and jobs this queue is tracking (2026-10-11: two slots filled 5 s apart → 3 replays on parallel 2)
busy_slots() { local a; a=$(active_procs); (( a > $1 )) && echo "$a" || echo "$1"; }
flow_job() {   # NQ order flow of one MBO month → mbo_1m (GLBX/NQ), book_1m, bars_1m buy_volume
  local m="$1" files; files=$(ls /home/theone/Desktop/bot/data/raw/mbo/"$m"*.dbn.zst)
  mkdir -p "$H/done/flow" data/logs/history
  # shellcheck disable=SC2086
  nice -n 15 ionice -c3 build-exp/dbn_tool mbo-1m $files --symbol NQ > "data/logs/history/flow_$m.log" 2>&1 && touch "$H/done/flow/$m.done"
}

other_crypto_replay() { procs_matching paper_engine "--account-label crypto_hist" | while read -r p; do grep -qa -- "--binance-agg" "/proc/$p/cmdline" || echo "$p"; done | head -1; }

run_queue() {
  exec 9> "$H/queue.lock"
  flock -n 9 || { echo "another history_queue.sh run is active ($H/queue.lock)"; exit 1; }
  say "queue start (parallel $PAR, $PAR_CLOSED while CME is closed) — $(jobs_list | wc -l) jobs"
  # crypto download in the background (network, not a replay slot)
  local dl_pid=""
  if ! [[ -f "$H/done/crypto_download.done" ]]; then
    # (a downloader started by hand is waited for, then the rerun only re-verifies what exists)
    ( while pgrep -f "binance_history_download.sh" > /dev/null; do sleep 60; done
      scripts/binance_history_download.sh BTCUSDT,ETHUSDT 2020-01 > /dev/null 2>&1 && touch "$H/done/crypto_download.done" ) 9>&- &
    dl_pid=$!; say "crypto download started (pid $dl_pid, log data/logs/binance_history_download.log)"
  fi
  declare -A running=()      # pid -> "market month"
  declare -A started=()
  local paused=0
  while :; do
    # reap finished workers
    local p
    for p in "${!running[@]}"; do
      if ! kill -0 "$p" 2>/dev/null; then
        wait "$p"; local rc=$?
        local j="${running[$p]}"; local secs=$(( $(date +%s) - ${started[$p]} ))
        case $rc in 0) say "DONE $j in ${secs}s — $(tail -1 "data/logs/history/${j// /_}.log" 2>/dev/null | cut -c1-220)"
                       # refresh data/history/report.md after every replayed month (seconds; never blocks the queue)
                       [[ "$j" == nq* || "$j" == btc* || "$j" == eth* ]] && { scripts/history_report.sh > /dev/null 2>&1 9>&- & };;
                    4) say "NODATA $j";; *) say "FAILED $j rc=$rc after ${secs}s (data/logs/history/${j// /_}.log)"; touch "$H/done/failed_${j// /_}";; esac
        unset "running[$p]" "started[$p]"
      fi
    done
    # guarded windows: pause / resume running engines
    if w=$(busy_window); then
      if (( paused == 0 )); then
        for f in "$PIDS"/*.pid; do [[ -f "$f" ]] && kill -STOP "$(cat "$f")" 2>/dev/null; done
        paused=1; say "PAUSE ($w) — ${#running[@]} replay(s) stopped"
      fi
    elif (( paused == 1 )); then
      for f in "$PIDS"/*.pid; do [[ -f "$f" ]] && kill -CONT "$(cat "$f")" 2>/dev/null; done
      paused=0; say "RESUME — ${#running[@]} replay(s) continued"
    fi
    # launch
    # disk guard: Postgres grows ~0.1 GB per replayed NQ month (paper_trades + paper_signals); never start below 40 GB
    local free_gb; free_gb=$(( $(df -B1 --output=avail / | tail -1) / 1073741824 ))
    if (( free_gb < ${MIN_FREE_GB:-40} )); then
      (( ${low_disk_said:-0} )) || say "DISK GUARD: ${free_gb} GB free < ${MIN_FREE_GB:-40} — no new jobs until space is freed"; low_disk_said=1
    elif (( paused == 0 )) && (( $(busy_slots ${#running[@]}) < $(slots) )); then
      local mk m launched=0
      while read -r mk m; do
        [[ "$(job_state "$mk" "$m")" == pending ]] || continue
        [[ -f "$H/done/failed_${mk}_$m" ]] && continue
        local busy=0; for p in "${!running[@]}"; do [[ "${running[$p]}" == "$mk $m" ]] && busy=1; done; (( busy )) && continue
        [[ -f "$PIDS/${mk}_$m.pid" ]] && continue                 # run started outside the queue (history_month.sh by hand)
        case "$mk" in nq|nqx) lbl=nq_hist;; nqcombo) lbl=nq_combo_hist;; nqbars) lbl=nq_bars;; nqbcheck) lbl=nq_barcheck;;
                      es) lbl=es_hist;; esbars) lbl=es_bars;; btc|eth|btclv|ethlv) lbl=crypto_hist;; *) lbl="";; esac
        [[ -n "$lbl" ]] && [[ -n "$(procs_matching paper_engine "account-label $lbl --replay-from .* --replay-to $m-")" ]] && continue   # same month replaying elsewhere
        if [[ "$mk" == flow ]]; then
          flow_job "$m" > /dev/null 2>&1 9>&- &
          running[$!]="flow $m"; started[$!]=$(date +%s); launched=1; say "START flow $m (pid $!)"; break
        fi
        if [[ "$mk" == crypto_bars ]]; then
          [[ -f "$H/done/crypto_download.done" ]] || continue   # needs every file first (ATR spans months)
          (crypto_bars && touch "$H/done/crypto_bars.done") > /dev/null 2>&1 9>&- &
          running[$!]="crypto_bars -"; started[$!]=$(date +%s); launched=1; break
        fi
        if [[ "$mk" == btc || "$mk" == eth ]]; then
          [[ -f "$H/done/crypto_bars.done" ]] || continue
          for p in "${!running[@]}"; do [[ "${running[$p]}" == crypto_bars* ]] && continue 2; done
          [[ -n "$(other_crypto_replay)" ]] && continue
        fi
        scripts/history_month.sh "$mk" "$m" > /dev/null 2>&1 9>&- &   # 9>&-: children never hold the queue lock
        running[$!]="$mk $m"; started[$!]=$(date +%s); launched=1
        say "START $mk $m (pid $!)"; break
      done < <(jobs_list)
      if (( launched == 0 && ${#running[@]} == 0 )); then
        local left; left=$(jobs_list | while read -r a b; do [[ "$(job_state "$a" "$b")" == pending && ! -f "$H/done/failed_${a}_$b" ]] && echo x; done | wc -l)
        if (( left == 0 )) && { [[ -z "$dl_pid" ]] || ! kill -0 "$dl_pid" 2>/dev/null; }; then say "queue finished"; write_status "[]"; break; fi
      fi
      (( launched )) && { sleep 5 9>&-; continue; }
    fi
    local rj=""; for p in "${!running[@]}"; do rj+="{\"job\":\"${running[$p]}\",\"pid\":$p,\"secs\":$(( $(date +%s) - ${started[$p]} ))},"; done
    write_status "${rj%,}"
    sleep 30 9>&-
  done
}

case "$CMD" in
  run) run_queue ;;
  status) [[ -f "$STATUS" ]] && jq . "$STATUS" || echo "no status yet" ;;
  list) jobs_list | while read -r a b; do echo "$a $b $(job_state "$a" "$b")"; done ;;
  *) echo "usage: $0 {run [--parallel N]|status|list}"; exit 2 ;;
esac
