#!/usr/bin/env bash
# mm_ab.sh — the paper A/B of the Binance-lead pull (paper_bn) against the live setting without it (paper_opt), plus
# the live bot's execution numbers, judged by the rules written BEFORE the data (Claude Doc "Lighter MM — Binance-lead
# pull", https://claude.ai/artifact/H8hJGTbSeWSbh1XyV4n6Vt, §Next steps). READ-ONLY: Postgres, the live journal and
# status.json are only read.
#
#   scripts/mm_ab.sh [--from ISO] [--a LABEL] [--b LABEL] [--json] [--notify]
#
# --from  default 2026-10-28T10:16:43Z (both twins restarted together, flat, at the measured live delays)
# --a/--b default paper_opt (control) / paper_bn (pull)
# Per UTC day and label: P&L = the sum, over the runs of that day, of each run's last '_total' minute equity (equity is
# marked to market since the day roll or the run's start, so a restart neither double-counts nor loses a run), quote
# fills (taker flattens excluded), volume, bps = P&L / volume. Verdict (needs ≥ 7 days with both labels and ≥ 300
# fills each): KEEP = B net ≥ A net and B ahead on ≥ 4 of the days · DROP = B behind on ≥ 5 of 7 (scaled: ≥ 5/7 of the
# days) or B net below A by more than the standard deviation of the daily differences · else KEEP RUNNING.
# The first day is partial (from --from); it counts like any other day.
# Live (journal since --from): fills, fills that arrived while our cancel for that order was in flight, pulls.
set -euo pipefail
cd "$(dirname "$0")/.."

FROM=2026-10-28T10:16:43Z A=paper_opt B=paper_bn JSON=0 NOTIFY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --from) FROM=$2; shift 2 ;;
        --a) A=$2; shift 2 ;;
        --b) B=$2; shift 2 ;;
        --json) JSON=1; shift ;;
        --notify) NOTIFY=1; shift ;;
        *) echo "usage: mm_ab.sh [--from ISO] [--a LABEL] [--b LABEL] [--json] [--notify]" >&2; exit 2 ;;
    esac
done
for l in "$A" "$B"; do [[ "$l" =~ ^[a-z0-9_]+$ ]] || { echo "bad label $l" >&2; exit 2; }; done
[[ "$FROM" =~ ^[0-9T:.Z+-]+$ ]] || { echo "bad --from" >&2; exit 2; }

PW=$(grep -E '^PG_PASSWORD=' .env | head -1 | cut -d= -f2- | tr -d "'\"")
q() { PGPASSWORD="$PW" psql -h localhost -U rithmic_user -d rithmic -At -v ON_ERROR_STOP=1 -c "$1"; }

PAPER=$(q "
  with starts as (select label, ts from mm_paper_events where label in ('$A','$B') and kind = 'start'),
       eq as (select e.label, e.ts, e.equity,
                     (select max(s.ts) from starts s where s.label = e.label and s.ts <= e.ts) run_start
              from mm_paper_equity e
              where e.label in ('$A','$B') and e.market = '_total' and e.ts >= '$FROM'::timestamptz),
       last as (select distinct on (label, day, run_start) label, (ts at time zone 'UTC')::date as day, run_start, equity
                from eq order by label, day, run_start, ts desc),
       pnl as (select label, day, sum(equity) pnl from last group by 1, 2),
       fl as (select label, (ts at time zone 'UTC')::date as day, count(*) fills, sum(price * qty) vol
              from mm_paper_fills where label in ('$A','$B') and not taker and ts >= '$FROM'::timestamptz group by 1, 2)
  select coalesce(json_agg(json_build_object('label', p.label, 'day', p.day, 'pnl', p.pnl,
                                             'fills', coalesce(f.fills, 0), 'vol', coalesce(f.vol, 0)) order by p.day, p.label), '[]')
  from pnl p left join fl f using (label, day);")

# live: journal lines since --from (run ids ≥ the first run at or after it)
FROM_MS=$(( $(date -d "$FROM" +%s) * 1000 ))
LIVE=$(cat data/mm_live/journal/lighter_main-*.jsonl 2>/dev/null | jq -s -c --argjson t0 "$FROM_MS" '
  map(select((.t // 0) >= $t0)) as $j
  | ($j | map(select(.ev == "tx" and .kind == "cancel")) | group_by(.ref) | map({key: (.[0].ref|tostring), value: (map(.t)|min)}) | from_entries) as $c
  | ($j | map(select(.ev == "fill"))) as $f
  | {fills: ($f|length),
     fills_cancel_in_flight: ($f | map(select(($c[(.coi|tostring)] // 1e18) <= .t)) | length),
     bn_pulls: ($j | map(select(.ev == "bn_pull")) | length),
     hours: ((($j | map(.t) | max // $t0) - $t0) / 3600000)}')
STATUS=$(jq -c '{equity_usd, position, quoting, bn_age_ms: .bn_pull.age_ms}' data/mm_live/status.json 2>/dev/null || echo null)

OUT=$(jq -n -c --argjson rows "$PAPER" --argjson live "$LIVE" --argjson st "$STATUS" --arg a "$A" --arg b "$B" --arg from "$FROM" '
  def tot(l): ($rows | map(select(.label == l))) as $r
    | {pnl: ($r|map(.pnl)|add // 0), fills: ($r|map(.fills)|add // 0), vol: ($r|map(.vol)|add // 0)}
    | . + {bps: (if .vol > 0 then .pnl / .vol * 1e4 else null end)};
  ($rows | group_by(.day) | map(select(length == 2))
         | map({day: .[0].day, a: (map(select(.label == $a))[0]), b: (map(select(.label == $b))[0])})
         | map(. + {diff: (.b.pnl - .a.pnl)})) as $days
  | (tot($a)) as $ta | (tot($b)) as $tb
  | ($days | length) as $n
  | ($days | map(select(.diff > 0)) | length) as $ahead
  | ($days | map(select(.diff < 0)) | length) as $behind
  | (if $n > 1 then ($days | map(.diff) | (add / length) as $m | map((. - $m) * (. - $m)) | add / (length - 1) | sqrt) else null end) as $sd
  | ($tb.pnl - $ta.pnl) as $net
  | (if $n < 7 or $ta.fills < 300 or $tb.fills < 300 then "WAIT (" + ($n|tostring) + "/7 days, fills " + ($ta.fills|tostring) + " / " + ($tb.fills|tostring) + " of 300)"
     elif $net >= 0 and $ahead >= 4 then "KEEP"
     elif ($behind * 7 >= 5 * $n) or ($sd != null and $net < -$sd) then "DROP"
     else "KEEP RUNNING (review after another week)" end) as $verdict
  | {from: $from, a: $a, b: $b, days: $days, total_a: $ta, total_b: $tb, net_diff_usd: $net, days_ahead: $ahead, days_behind: $behind,
     diff_sd_usd: $sd, verdict: $verdict, live: ($live + {status: $st,
       in_flight_share: (if $live.fills > 0 then $live.fills_cancel_in_flight / $live.fills else null end),
       pulls_per_hour: (if $live.hours > 0 then $live.bn_pulls / $live.hours else null end)})}')

if [ "$JSON" = 1 ]; then echo "$OUT"; else
    echo "$OUT" | jq -r '
      def m(x): if x == null then "—" else (x * 1000 | round / 1000 | tostring) end;
      def b(x): if x == null then "—" else (x * 100 | round / 100 | tostring) end;
      "Binance-pull A/B since \(.from): \(.b) (pull) vs \(.a) (no pull) — verdict \(.verdict)",
      "",
      "day         \(.a) $ / fills / bps        \(.b) $ / fills / bps        diff $",
      (.days[] | "\(.day)  \(m(.a.pnl)) / \(.a.fills) / \(b(if .a.vol > 0 then .a.pnl / .a.vol * 1e4 else null end))      \(m(.b.pnl)) / \(.b.fills) / \(b(if .b.vol > 0 then .b.pnl / .b.vol * 1e4 else null end))      \(m(.diff))"),
      "",
      "total  \(.a): $\(m(.total_a.pnl)), \(.total_a.fills) fills, \(b(.total_a.bps)) bps  ·  \(.b): $\(m(.total_b.pnl)), \(.total_b.fills) fills, \(b(.total_b.bps)) bps",
      "B − A $\(m(.net_diff_usd)), ahead \(.days_ahead) / behind \(.days_behind) days, sd of daily diff \(m(.diff_sd_usd))",
      "",
      "live since \(.from): \(.live.fills) fills, \(.live.fills_cancel_in_flight) while our cancel was in flight (\(b(if .live.in_flight_share == null then null else .live.in_flight_share * 100 end)) %), \(.live.bn_pulls) pulls (\(b(.live.pulls_per_hour))/h)",
      "live now: equity $\(m(.live.status.equity_usd)), position \(.live.status.position), quoting \(.live.status.quoting), Binance age \(.live.status.bn_age_ms) ms"'
fi
if [ "$NOTIFY" = 1 ] && command -v grid-notify >/dev/null 2>&1; then
    LINE=$(echo "$OUT" | jq -r 'def m(x): if x == null then "—" else (x * 100 | round / 100 | tostring) end;
      "MM Binance-pull A/B · \(.verdict) · \(.b) $\(m(.total_b.pnl)) (\(.total_b.fills) fills) vs \(.a) $\(m(.total_a.pnl)) (\(.total_a.fills)) · ahead \(.days_ahead)/\(.days|length) days · live \(.live.fills) fills, \(.live.fills_cancel_in_flight) during an in-flight cancel"')
    grid-notify "$LINE" >/dev/null 2>&1 || true
fi
