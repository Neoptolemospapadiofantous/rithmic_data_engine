#!/usr/bin/env bash
# daily_plan.sh — the day's trading plan for NQ, for trading by hand (docs/manual_playbook.md), read by the dashboard's
# Plan page (bot repo ui/routers/lab.py → /api/lab/plan).
#
#   scripts/daily_plan.sh [YYYY-MM-DD]      default: today in New York (the next weekday on a weekend)
#
# Runs on weekdays at 09:01 and then every 5 minutes until 14:01 ET (deploy/daily-plan.timer): 09:01 = the plan from yesterday's session and
# the overnight; 09:31 adds the real 09:30 open, the gap and the cascade levels; 09:46 adds the 15-minute opening range for
# the gap retest. Read-only on Postgres; writes data/plan/<date>.json and data/plan/latest.json.
#
# The strategies are the working fleet's (config/working_fleet.json), with the rules they were tested with. The tape regime
# (1-minute autocorrelation of the Asia and London sessions) is CONTEXT only: it persists into New York (58 %, 2026-10-17)
# but trading EMA / fades by it failed testing (research/preregistered/2026-10-17_regime_switch.md).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
export PGPASSWORD="$(grep -E '^PG_PASSWORD=' .env | head -1 | cut -d= -f2- | tr -d "'\"")"
PSQL=(psql -h 127.0.0.1 -U rithmic_user -d rithmic -Atq -v ON_ERROR_STOP=1)
OUT=data/plan; mkdir -p "$OUT"

plan="${1:-}"
if [[ -z "$plan" ]]; then
    plan=$(TZ=America/New_York date +%F)
    case $(TZ=America/New_York date +%u) in 6) plan=$(date -d "$plan +2 day" +%F);; 7) plan=$(date -d "$plan +1 day" +%F);; esac
fi
[[ "$plan" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || { sed -n 2,6p "$0"; exit 2; }
prev_day=$(date -d "$plan -1 day" +%F)

# ── VIX close of the last session before the plan day (data/regime/VIX_History.csv, MM/DD/YYYY)
vix=$(awk -F, -v p="${plan//-/}" 'NR > 1 { split($1, d, "/"); k = d[3] d[1] d[2]; if (k < p) { v = $5; vd = d[3] "-" d[1] "-" d[2] } }
      END { if (v != "") printf "{\"close\": %.2f, \"date\": \"%s\"}", v, vd; else print "null" }' data/regime/VIX_History.csv 2>/dev/null || echo null)

# ── everything else from Postgres in one JSON object
raw=$("${PSQL[@]}" -c "
WITH prior AS (
  SELECT session_date, rth_open, rth_high, rth_low, rth_close, range_pts
  FROM session_stats WHERE symbol = 'NQ' AND session_date < '$plan'::date AND bars >= 350 AND range_pts > 0
  ORDER BY session_date DESC LIMIT 60),
w AS (SELECT ('$prev_day 18:00'::timestamp AT TIME ZONE 'America/New_York') AS on0,
             ('$plan 09:30'::timestamp AT TIME ZONE 'America/New_York') AS rth0,
             ('$plan 09:45'::timestamp AT TIME ZONE 'America/New_York') AS or1,
             ('$plan 03:00'::timestamp AT TIME ZONE 'America/New_York') AS ldn0,
             ('$plan 08:00'::timestamp AT TIME ZONE 'America/New_York') AS ldn1),
b AS (SELECT minute, high, low, close, close - lag(close) OVER (ORDER BY minute) AS r
      FROM bars_1m, w WHERE symbol = 'NQ' AND minute >= w.on0 AND minute < w.or1),
ac AS (SELECT CASE WHEN minute < (SELECT ldn0 FROM w) THEN 'asia' WHEN minute < (SELECT ldn1 FROM w) THEN 'london' ELSE 'pre' END AS sess,
              r, lag(r) OVER (ORDER BY minute) AS rp, minute FROM b)
SELECT json_build_object(
  'prior', (SELECT json_agg(json_build_object('date', session_date, 'o', rth_open, 'h', rth_high, 'l', rth_low, 'c', rth_close, 'range', range_pts)
                            ORDER BY session_date DESC) FROM prior),
  'overnight', (SELECT json_build_object('hi', max(high), 'lo', min(low), 'bars', count(*),
                       'last', (SELECT close FROM b WHERE minute < (SELECT rth0 FROM w) ORDER BY minute DESC LIMIT 1),
                       'last_at', (SELECT to_char(max(minute) AT TIME ZONE 'America/New_York', 'HH24:MI') FROM b WHERE minute < (SELECT rth0 FROM w)))
                FROM b WHERE minute < (SELECT rth0 FROM w)),
  'tape', (SELECT json_object_agg(sess, json_build_object('ac1', ac1, 'n', n)) FROM (
             SELECT sess, CASE WHEN sum(r * r) > 0 THEN sum(r * rp) / sum(r * r) END AS ac1, count(*) AS n
             FROM ac WHERE rp IS NOT NULL AND sess IN ('asia', 'london') GROUP BY sess) t),
  'rth_open', (SELECT open FROM bars_1m, w WHERE symbol = 'NQ' AND minute = w.rth0),
  'or15', (SELECT CASE WHEN count(*) >= 15 THEN json_build_object('hi', max(high), 'lo', min(low)) END
           FROM bars_1m, w WHERE symbol = 'NQ' AND minute >= w.rth0 AND minute < w.or1),
  'news', (SELECT coalesce(json_agg(json_build_object('kind', kind, 'label', label, 'time', to_char(time_et, 'HH24:MI'))), '[]')
           FROM calendar WHERE day = '$plan'::date),
  'rth_bars', (SELECT coalesce(json_agg(json_build_object('m', (extract(epoch FROM (minute AT TIME ZONE 'America/New_York')::time) / 60)::int - 570,
                 'o', open, 'h', high, 'l', low, 'c', close, 'v', volume, 'bv', buy_volume) ORDER BY minute), '[]')
               FROM bars_1m, w WHERE symbol = 'NQ' AND minute >= w.rth0 AND minute < w.rth0 + interval '390 minutes'),
  'pre1', (SELECT coalesce(json_agg(close ORDER BY minute), '[]') FROM (
           SELECT minute, close FROM bars_1m, w WHERE symbol = 'NQ' AND minute >= w.rth0 - interval '60 minutes' AND minute < w.rth0) t1),
  'pre5', (SELECT coalesce(json_agg(c ORDER BY bk), '[]') FROM (
           SELECT floor(extract(epoch FROM minute) / 300) AS bk, (array_agg(close ORDER BY minute DESC))[1] AS c
           FROM bars_1m, w WHERE symbol = 'NQ' AND minute >= w.on0 AND minute < w.rth0 GROUP BY 1) t5b),
  'pre2', (SELECT coalesce(json_agg(c ORDER BY bk), '[]') FROM (
           SELECT floor(extract(epoch FROM minute) / 120) AS bk, (array_agg(close ORDER BY minute DESC))[1] AS c
           FROM bars_1m, w WHERE symbol = 'NQ' AND minute >= w.on0 AND minute < w.rth0 GROUP BY 1) t5),
  'generated_et', to_char(now() AT TIME ZONE 'America/New_York', 'YYYY-MM-DD HH24:MI'))") || { echo "query failed"; exit 1; }

# ── RSI(2) (Wilder) and the down streak from the prior closes, oldest first
closes=$(jq -r '[.prior[]?.c] | reverse | .[]' <<<"$raw")
rsi2=$(awk 'NR == 1 { p = $1; next } { ch = $1 - p; u = ch > 0 ? ch : 0; d = ch < 0 ? -ch : 0;
            if (NR == 2) { up = u; dn = d } else { up = (up + u) / 2; dn = (dn + d) / 2 } p = $1 }
            END { if (NR >= 3) printf "%.1f", (up + dn > 0 ? 100 * up / (up + dn) : 50); else print "null" }' <<<"$closes")

jq -n --arg plan "$plan" --argjson raw "$raw" --argjson vix "$vix" --argjson rsi2 "$rsi2" '
  def r2: . * 100 | round / 100;
  def tick: . * 4 | round / 4;
  $raw as $r | ($r.prior // []) as $p | ($p[0] // null) as $y |
  ([$p[0:14][].range] | if length == 14 then add / 14 else null end) as $atr |
  (if $y and ($y.h > $y.l) then ($y.c - $y.l) / ($y.h - $y.l) else null end) as $ibs |
  (if ($p | length) >= 3 then ($p[0].c < $p[1].c and $p[1].c < $p[2].c) else false end) as $streak2 |
  (if $y and $atr then ($y.c - $y.o) / $atr else null end) as $body |
  ([ (if $ibs != null and $ibs <= 0.3 then "closed in its bottom \(($ibs * 100) | round)%" else empty end),
     (if $rsi2 != null and $rsi2 <= 20 then "RSI(2) \($rsi2)" else empty end),
     (if $streak2 then "two lower closes" else empty end),
     (if $body != null and $body <= -1 then "fell \(-$body | r2) ATR open to close" else empty end) ]) as $weak |
  (($plan | strptime("%Y-%m-%d") | mktime | strftime("%u")) == "1") as $monday |
  ($r.overnight // {}) as $on |
  (if $atr and $on.hi then ($on.hi - $on.lo) / $atr else null end) as $onr |
  ($r.rth_open) as $open |
  (if $open and $y then ($open - $y.c) else (if $on.last and $y then ($on.last - $y.c) else null end) end) as $gap_pts |
  (if $gap_pts != null and $atr then $gap_pts / $atr else null end) as $gap |
  ([$r.news[]? | select(.kind == "cpi" or .kind == "ppi" or .kind == "nfp" or .kind == "fomc")]) as $events |
  ([$r.news[]? | select(.kind == "holiday")] | length > 0) as $holiday |
  # VIX must be the close of the prior session itself; an older row (file not updated yet) counts as missing
  ((if $vix and $y and ($vix.date == $y.date) then $vix.close else null end)) as $vx |
  ($weak | length > 0) as $is_weak |
  {
    date: $plan, generated_et: $r.generated_et,
    stage: (if $r.or15 then "after 09:45 — opening range set" elif $open then "after 09:30 — open known" else "pre-open" end),
    holiday: $holiday,
    numbers: {
      atr: ($atr | if . then r2 else null end), prior: $y, ibs: ($ibs | if . then r2 else null end), rsi2: $rsi2, two_lower_closes: $streak2,
      prior_body_atr: ($body | if . then r2 else null end), vix: $vix, vix_current: ($vx != null), open: $open,
      gap_pts: ($gap_pts | if . != null then r2 else null end), gap_atr: ($gap | if . != null then r2 else null end),
      gap_basis: (if $open then "09:30 open" elif $on.last then "last price \($on.last_at) ET (estimate until the open)" else null end),
      overnight: {hi: $on.hi, lo: $on.lo, range_atr: ($onr | if . then r2 else null end)},
      or15: $r.or15, news: $r.news
    },
    context: {
      expected_day: (if $onr == null then null elif $onr > 0.8 then "wide — overnight range \($onr | r2) ATR (such days average about 1.24 ATR)"
                     elif $onr < 0.4 then "quiet — overnight range \($onr | r2) ATR (such days average about 0.78 ATR)"
                     else "normal — overnight range \($onr | r2) ATR (about 1.0 ATR)" end),
      tape: ($r.tape // {} | with_entries(.value |= {ac1: (.ac1 | if . then r2 else null end), n: .n,
               read: (if .ac1 == null then "no data" elif .ac1 > 0 then "trending tape" elif .ac1 < -0.05 then "mean-reverting tape" else "close to random" end)})),
      tape_note: "The overnight tape regime carries into New York about 58% of the time (2026-10-17). Context only: trading EMA or fades by the regime failed testing.",
      events: $events
    },
    strategies: [
      { id: "weak_day_long", name: "Weak-day long", tested: "PASS, +13 pts/trade, no stop",
        on: ($is_weak and $vx != null and $vx >= 20),
        status: (if ($is_weak | not) then "off — yesterday was not weak"
                 elif $vx == null then "check — yesterday was weak (\($weak | join(", "))); on only if yesterday'"'"'s VIX close ≥ 20 (not in our file yet: latest is \($vix.date // "none"))"
                 elif $vx >= 20 then "ON — weak (\($weak | join(", "))) and VIX \($vx) ≥ 20"
                 else "off — weak (\($weak | join(", "))) but VIX \($vx) < 20" end),
        window: "buy 09:30, sell 15:55", risk: (if $atr then "no stop: 1 ATR against = $\(($atr * 2) | round) per MNQ" else null end) },
      { id: "monday_long", name: "Monday long", tested: "PASS but fragile (flat 2010–18); paper candidate",
        on: $monday, status: (if $monday then "ON (candidate — paper first)" else "off — not Monday" end),
        window: "buy 09:30, sell 15:55", risk: (if $atr then "no stop: 1 ATR against = $\(($atr * 2) | round) per MNQ" else null end) },
      { id: "so_cascade", name: "Strong-open cascade", tested: "PASS, +5.8 pts/trade with VWAP + stall exits",
        on: true, status: (if ($is_weak and $vx != null and $vx >= 20) or $monday then "WATCH — longs only today (a long hold is on: skip shorts)" else "WATCH" end),
        window: "checks 09:50 (0.25 ATR) · 10:30 / 11:00 / 11:30 (0.30 ATR); one trade (no 12:00 check — it loses, 2026-10-17)",
        levels: (if $open and $atr then [
            {at: "09:50", long_above: (($open + 0.25 * $atr) | tick), short_below: (($open - 0.25 * $atr) | tick)},
            {at: "10:30 · 11:00 · 11:30", long_above: (($open + 0.30 * $atr) | tick), short_below: (($open - 0.30 * $atr) | tick)}] else null end),
        stop_pts: (if $atr then ((0.25 * $atr) | tick) else null end), target_pts: (if $atr then ((0.5 * $atr) | tick) else null end),
        risk: (if $atr then "stop 0.25 ATR = $\((0.25 * $atr * 2) | round) per MNQ, target 2R" else null end),
        exits: "out early on a 5-minute close back through VWAP, or if not +0.5R after 60 minutes; flat 15:55" },
      { id: "gap_or_retest", name: "Gap side + opening-range retest", tested: "PASS +9.0 (holdout +7.2, t 0.6) — paper candidate",
        on: ($gap != null and ($gap | fabs) >= 0.1),
        side: (if $gap == null then null elif $gap >= 0.1 then "long" elif $gap <= -0.1 then "short" else null end),
        status: (if $gap == null then "waiting for the gap"
                 elif ($gap | fabs) < 0.1 then "off — gap \($gap | r2) ATR is inside ±0.1"
                 else "\(if $gap > 0 then "LONG" else "SHORT" end) side — gap \($gap | r2) ATR\(if $open then "" else " (estimate until 09:30)" end)" end),
        window: "from 09:45 to 13:55: the first 5-minute bar that touches the 15-minute opening-range edge on the gap side and closes beyond it",
        level: (if $r.or15 and $gap != null and ($gap | fabs) >= 0.1 then (if $gap > 0 then {edge: "opening-range high", price: $r.or15.hi} else {edge: "opening-range low", price: $r.or15.lo} end) else null end),
        stop_pts: (if $atr then ((0.35 * $atr) | tick) else null end), target_pts: (if $atr then ((0.7 * $atr) | tick) else null end),
        risk: (if $atr then "stop 0.35 ATR = $\((0.35 * $atr * 2) | round) per MNQ, target 2R, flat 15:55" else null end) }
    ],
    rules: ["The cascade and the gap retest may both trade the same day if they agree in direction (tested 2026-10-17: more paid, fewer blown 25K accounts); never in opposite directions.",
            "At most 3 trades a day, all in the direction of the first. A 2nd or 3rd trade only after a WINNER (target hit): the next plan setup or a 5-minute bar that touches VWAP and closes back in the day direction, before 13:55, stop 0.25 ATR, 2R. After a loss, done for the day (tested 2026-10-17: re-entries after a win +5.7 pts vs +0.8 at random times, but they do not speed up 25K payouts).",
            "A long hold is on → no shorts that day. Never hold opposite positions.",
            "No signal = no trade. Fading quiet days lost money in every test.",
            "EMA by hand, best tested form (2-min bars, a bar within 0.03 ATR of EMA-9 closing back, EMA-9 beyond EMA-21; 2,880 variants 2026-10-17, trend / momentum filters add nothing): about BREAKEVEN after a realistic 1.6-pt cost (2010-26, holdout 2025-26 -0.35 pts/trade) — not a tested edge. If you take it: only in the day direction, on that side of VWAP and the 09:30 open, before 13:55, done after a loss, stop 0.25 ATR, break-even at +1R, no fixed target. Smaller stops, swing / EMA / VWAP trails and stop entries above the bar all did worse (96 + 125 combinations tested 2026-10-17, all negative per unit of risk).",
            "The live ORB rules and mtf_london_v5__sg lose in every condition tested (2026-10-17) — no direction or permission filter makes them positive.",
            "Overnight sessions (Asia, London) are context, not trades."]
  }' >"$OUT/$plan.json.tmp" && mv "$OUT/$plan.json.tmp" "$OUT/$plan.json" && cp "$OUT/$plan.json" "$OUT/latest.json.tmp" && mv "$OUT/latest.json.tmp" "$OUT/latest.json" \
  || { echo "plan build failed"; exit 1; }
jq -r '"plan \(.date) (\(.stage), \(.generated_et) ET): " + ([.strategies[] | "\(.name): \(.status)"] | join(" | "))' "$OUT/$plan.json"

# ── live: "am I allowed to trade right now?" (scripts/daily_plan_live.jq) — minutes after 09:30 now, or the whole session for a past day
if [[ "$plan" == "$(TZ=America/New_York date +%F)" ]]; then now_m=$(( 10#$(TZ=America/New_York date +%H) * 60 + 10#$(TZ=America/New_York date +%M) - 570 )); else now_m=390; fi
raw_now_m=$now_m
(( now_m < 0 )) && now_m=0; (( now_m > 390 )) && now_m=390
jq --argjson bars "$(jq -c '.rth_bars // []' <<<"$raw")" --argjson pre2 "$(jq -c '.pre2 // []' <<<"$raw")" --argjson pre5 "$(jq -c '.pre5 // []' <<<"$raw")" --argjson pre1 "$(jq -c '.pre1 // []' <<<"$raw")" --argjson now "$now_m" -f scripts/daily_plan_live.jq "$OUT/$plan.json" >"$OUT/$plan.json.tmp" \
  && mv "$OUT/$plan.json.tmp" "$OUT/$plan.json" && cp "$OUT/$plan.json" "$OUT/latest.json" || echo "live section failed"
# ── Telegram alerts for the hand-traded sprint (scripts/plan_alerts.sh), today's plan only, never for a replayed day
if [[ "$plan" == "$(TZ=America/New_York date +%F)" ]]; then scripts/plan_alerts.sh "$OUT/$plan.json" "$raw_now_m" || true; fi
jq -r '"live \(.live.at // "-"): \(.live.status) — " + ([.live.setups[]? | "\(.setup) \(.dir) at \(.at) (flow \(if .flow_agrees then "agrees" else "against" end))\(if .allowed then "" else " NOT ALLOWED: \(.why_not)" end)"] | join(" | "))' "$OUT/$plan.json"
