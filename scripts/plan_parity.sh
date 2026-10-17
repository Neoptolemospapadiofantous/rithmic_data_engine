#!/usr/bin/env bash
# plan_parity.sh FROM TO — does the Plan page (scripts/daily_plan_live.jq) flag the same momentum setups the paper engine trades?
# For every session FROM..TO it replays the live section minute by minute (as the timer does) and collects the 5-min closes the
# page flags as a setup; then every engine entry of mom_dir_30_ema20 / mom_dir_45_vwap (label backfill) must be among them.
# The page flags every qualifying bar (it cannot know whether you are already in a trade), so page ⊇ engine is the test.
# Read-only on Postgres; writes data/plan/<day>.json for the days it replays and rebuilds the newest plan at the end.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
FROM="${1:?usage: plan_parity.sh FROM TO}"; TO="${2:?}"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
Q() { psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -c "$1"; }
ok=0; miss=0; days=0
for d in $(Q "select distinct (entry_time at time zone 'America/New_York')::date from paper_trades where account_label='backfill' and strategy_id in ('mom_dir_30_ema20','mom_dir_45_vwap') and (entry_time at time zone 'America/New_York')::date between '$FROM' and '$TO' order by 1"); do
  p=$(date -d "$d -1 day" +%F); days=$((days + 1))
  scripts/daily_plan.sh "$d" >/dev/null 2>&1 || { echo "$d plan build failed"; continue; }
  bars=$(Q "select coalesce(json_agg(json_build_object('m',(extract(epoch from (minute at time zone 'America/New_York')::time)/60)::int-570,'o',open,'h',high,'l',low,'c',close,'v',volume,'bv',buy_volume) order by minute),'[]') from bars_1m where symbol='NQ' and minute >= ('$d 09:30'::timestamp at time zone 'America/New_York') and minute < ('$d 16:00'::timestamp at time zone 'America/New_York')")
  pre1=$(Q "select coalesce(json_agg(close order by minute),'[]') from bars_1m where symbol='NQ' and minute >= ('$d 09:30'::timestamp at time zone 'America/New_York') - interval '60 minutes' and minute < ('$d 09:30'::timestamp at time zone 'America/New_York')")
  pre5=$(Q "select coalesce(json_agg(c order by bk),'[]') from (select floor(extract(epoch from minute)/300) bk,(array_agg(close order by minute desc))[1] c from bars_1m where symbol='NQ' and minute >= ('$p 18:00'::timestamp at time zone 'America/New_York') and minute < ('$d 09:30'::timestamp at time zone 'America/New_York') group by 1) t")
  flags=""
  for n in $(seq 15 5 265); do
    flags+=$(jq -r --argjson bars "$(jq -c --argjson n "$n" '[.[] | select(.m < $n)]' <<<"$bars")" --argjson pre2 '[]' --argjson pre5 "$pre5" --argjson pre1 "$pre1" --argjson now "$n" \
      -f scripts/daily_plan_live.jq "data/plan/$d.json" | jq -r '[(if .live.mom.setup != null and .live.mom.allowed then "mom_dir_30_ema20@" + .live.mom.setup.at else empty end),
        (if .live.mom45.setup != null then "mom_dir_45_vwap@" + .live.mom45.setup.at else empty end)] | .[]' | tr '\n' ' ')" "
  done
  while IFS='|' read -r sid at; do
    [[ -n "$sid" ]] || continue
    if [[ " $flags " == *" $sid@$at "* ]]; then ok=$((ok + 1)); else miss=$((miss + 1)); echo "MISS  $d $sid entry $at — the page did not flag it (page flags: ${flags:-none})"; fi
  done < <(Q "select strategy_id, to_char(entry_time at time zone 'America/New_York','HH24:MI') from paper_trades where account_label='backfill' and strategy_id in ('mom_dir_30_ema20','mom_dir_45_vwap') and (entry_time at time zone 'America/New_York')::date = '$d' order by entry_time")
done
newest=$(ls data/plan/20*.json 2>/dev/null | sort | tail -1); [[ -n "$newest" ]] && scripts/daily_plan.sh "$(basename "$newest" .json)" >/dev/null 2>&1
echo "plan_parity $FROM..$TO: $days sessions, $ok engine entries flagged by the page, $miss missed"
[[ $miss -eq 0 ]]
