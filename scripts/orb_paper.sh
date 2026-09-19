#!/usr/bin/env bash
# orb_paper.sh — zero-setup ORB evaluation on free NQ 1-minute bars.
#
# Needs no Rithmic login, no broker account and no API key: pulls public
# 1-minute NQ bars, replays each RTH session through build/orb_strategy
# (the SAME ORB logic the live executor runs) and scores the outcome.
#
#   bash scripts/orb_paper.sh          # last 7 days
#   bash scripts/orb_paper.sh 5        # last 5 days
#
# Appends one row per session to data/orb_paper_sessions.csv so repeated
# daily runs accumulate a genuine forward record.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"

DAYS="${1:-7}"
CFG="config/live_config.json"
OUT="data/orb_paper_sessions.csv"
RAW="$(mktemp)"; trap 'rm -f "$RAW"' EXIT

command -v jq >/dev/null || { echo "jq required" >&2; exit 1; }
[[ -x build/orb_strategy ]] || { echo "build/orb_strategy missing — cmake --build build" >&2; exit 1; }

curl -s --max-time 30 -H 'User-Agent: Mozilla/5.0' \
  "https://query1.finance.yahoo.com/v8/finance/chart/NQ=F?interval=1m&range=${DAYS}d" -o "$RAW"
jq -e '.chart.result[0].timestamp' "$RAW" >/dev/null 2>&1 || { echo "no data returned" >&2; exit 1; }

PV=$(jq -r '.orb.point_value'   "$CFG")
TS=$(jq -r '.orb.tick_size'     "$CFG")
COMM=$(jq -r '.commission_rt'   "$CFG")
EOD_BEFORE=$(jq -r '.orb.eod_exit_minutes_before_close' "$CFG")

# Bars -> {session_date, minute_of_day_ET, o,h,l,c}, RTH only (09:30-16:00 ET)
jq -r --argjson eod "$EOD_BEFORE" '
  .chart.result[0] as $r
  | $r.meta.gmtoffset as $off
  | [range(0; ($r.timestamp|length))] | map(
      . as $i
      | ($r.timestamp[$i] + $off) as $lt
      | {d: (($lt/86400)|floor), m: ((($lt % 86400)/60)|floor),
         o: $r.indicators.quote[0].open[$i], h: $r.indicators.quote[0].high[$i],
         l: $r.indicators.quote[0].low[$i],  c: $r.indicators.quote[0].close[$i]}
    )
  | map(select(.o != null and .h != null and .l != null and .c != null))
  | map(select(.m >= 570 and .m <= 960))
  | group_by(.d) | .[] | @json
' "$RAW" > "$RAW.sessions"

[[ -f "$OUT" ]] || echo "session_date,signal,entry,stop,target,exit_reason,exit_price,pnl_points,pnl_usd" > "$OUT"

printf "%-12s %-6s %9s %9s %9s %-12s %9s %8s\n" DATE SIGNAL ENTRY STOP TARGET EXIT PNL_PTS PNL_USD
tot=0; wins=0; losses=0; n=0
while IFS= read -r sess; do
  DATESTR=$(jq -r '.[0].d * 86400 | strftime("%Y-%m-%d")' <<<"$sess")
  SIG=$(jq -c 'map({high:.h, low:.l, close:.c})' <<<"$sess" | ./build/orb_strategy --config "$CFG" 2>/dev/null || echo '{"signal":null}')
  S=$(jq -r '.signal // "null"' <<<"$SIG")
  [[ "$S" == "null" ]] && { printf "%-12s %-6s\n" "$DATESTR" "-"; continue; }

  ENTRY=$(jq -r '.entry' <<<"$SIG"); STOP=$(jq -r '.stop_loss' <<<"$SIG"); TGT=$(jq -r '.target' <<<"$SIG")
  # Walk bars after the breakout; SL checked before target within a bar (conservative)
  RES=$(jq -r --arg s "$S" --argjson e "$ENTRY" --argjson sl "$STOP" --argjson tg "$TGT" --argjson eod "$EOD_BEFORE" '
    (map(.c) | index($e)) as $bi
    | if $bi == null then "NOFILL,0" else
      (.[($bi+1):]) as $rest
      | ($rest | map(select(.m <= (960 - $eod)))) as $win
      | (reduce $win[] as $b ({done:false, why:"EOD", px:0};
          if .done then . else
            if $s == "LONG" then
              if $b.l <= $sl then {done:true, why:"SL",     px:$sl}
              elif $b.h >= $tg then {done:true, why:"TARGET", px:$tg}
              else {done:false, why:"EOD", px:$b.c} end
            else
              if $b.h >= $sl then {done:true, why:"SL",     px:$sl}
              elif $b.l <= $tg then {done:true, why:"TARGET", px:$tg}
              else {done:false, why:"EOD", px:$b.c} end
            end
          end)) as $r
      | "\($r.why),\($r.px)"
      end' <<<"$sess")
  WHY="${RES%%,*}"; PX="${RES##*,}"
  [[ "$WHY" == "NOFILL" ]] && { printf "%-12s %-6s %9s\n" "$DATESTR" "$S" "no-fill"; continue; }

  PTS=$(awk -v s="$S" -v e="$ENTRY" -v x="$PX" 'BEGIN{printf "%.2f", (s=="LONG")?(x-e):(e-x)}')
  USD=$(awk -v p="$PTS" -v pv="$PV" -v c="$COMM" 'BEGIN{printf "%.2f", p*pv - c}')
  printf "%-12s %-6s %9.2f %9.2f %9.2f %-12s %9s %8s\n" "$DATESTR" "$S" "$ENTRY" "$STOP" "$TGT" "$WHY" "$PTS" "$USD"
  echo "$DATESTR,$S,$ENTRY,$STOP,$TGT,$WHY,$PX,$PTS,$USD" >> "$OUT"
  tot=$(awk -v t="$tot" -v u="$USD" 'BEGIN{print t+u}'); n=$((n+1))
  awk -v u="$USD" 'BEGIN{exit !(u>0)}' && wins=$((wins+1)) || losses=$((losses+1))
done < "$RAW.sessions"
rm -f "$RAW.sessions"

echo
echo "sessions with a signal: $n   wins: $wins   losses: $losses"
[[ $n -gt 0 ]] && awk -v t="$tot" -v n="$n" -v w="$wins" 'BEGIN{
  printf "net: $%.2f   avg/trade: $%.2f   win rate: %.1f%%\n", t, t/n, (w*100.0)/n}'
echo "appended to $OUT"
