#!/usr/bin/env bash
# mm_gates.sh — the daily overview of the Lighter market-making plan (docs/market_making_plan.md): computes the
# four stage gates from the recorded ticks and the paper engine's Postgres rows, writes data/mm_gates/gates.json
# + report.md and sends one Telegram line through grid-notify (fail-open). Run by mm-gates.timer at 00:10 UTC,
# or by hand: scripts/mm_gates.sh [--no-notify].
#
# G1 Data   — Lighter's own books re-test the frozen setting: complete UTC days recorded (on the busiest market —
#             most book events that day: ≥ 23.5 h of books AND no silence between two books longer than 15 min, from
#             mm_sim --daily's hours / max_gap_min / books), flat-to-flat replay (build/mm_sim --daily, Hyperliquid tick rule, Lighter grid)
#             over those days: net bps and volume-weighted 10 s markout.
# G2 Paper  — mm_paper_daily (label 'paper', the official 300 ms engine; the 220 ms shadow is 'paper_220'): complete paper days, cumulative P&L, positive-day share, mean day vs the backtest's
#             95% range ($9..$68), loss stops. Only COMPLETE paper days count toward any G2 number (audit 2026-10-26); a day is
#             complete when ALL of these hold, otherwise it is listed under excluded with its reasons:
#               * no engine restart (a 'start' event) — the engine resets at every start, its row covers only the hours after it;
#               * the engine ran ≥ 23.5 h of that UTC day (minute '_total' equity rows ≥ 1410);
#               * every configured market has a daily row (symbols from the installed mm-paper-local unit, else deploy/);
#               * no OUTAGE longer than 30 min in a row (minute equity rows). An outage minute is one where the portfolio
#                 was halted by the kill switch, or where at least HALF of the configured markets were halted at once
#                 (feed down halts them all; mass 'stale' = a feed/engine problem). A single thin market going quiet
#                 ('stale' on its own) is not an outage, and the loss stop's halt-until-midnight is a strategy decision,
#                 never counted (decided 2026-10-26);
#               * zero DB drops that day (data/mm_paper/db_drops_paper.json, the engine's persisted drop ledger; a day before
#                 the ledger's `since` is unknown and therefore not complete).
# G3 Pilot / G4 Scale — live; no data until Phase 4. Reported as pending with their criteria.
# Numbers in RULES below are the plan's pass/fail thresholds; change them there and in the plan together.
set -uo pipefail
cd "$(dirname "$0")/.."
NOTIFY=1; [ "${1:-}" = "--no-notify" ] && NOTIFY=0
OUT=data/mm_gates; mkdir -p "$OUT"
ARCH=/media/theone/381C48C51C48803C/rithmic_dex/lighter
TODAY=$(date -u +%F)
STAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)
BT_LOW=9; BT_HIGH=68                       # backtest holdout: 95% interval of the mean day, $
# RULES
G1_DAYS=10; G1_NET_BPS=0.25; G1_MK10_PASS=-1.0; G1_MK10_FAIL=-1.5
G1_MIN_HOURS=23.5; G1_MAX_GAP_MIN=15
G2_DAYS=20; G2_POS_SHARE=0.55; G2_FAIL_CUM=-300; G2_FAIL_STOPS=2
G2_MIN_HOURS=23.5; G2_MAX_HALT_MIN=30

log() { printf '[%s] [mm_gates] %s\n' "$(date -u +%H:%M:%S)" "$*"; }

# ── G1: Lighter archive, complete days only ─────────────────────────────────────────────────────────
G1_JSON='{"days":0,"net_bps":null,"mk10_bps":null,"fills":0,"volume_usd":0,"pnl_usd":0,"status":"pending","daily":[]}'
if [ -d "$ARCH" ] && [ -x build/mm_sim ]; then
  # per-coin params: Lighter grid from supported_price_decimals, Hyperliquid szDecimals for the strategy tick
  LM=$(curl -s -m 30 "https://mainnet.zklighter.elliot.ai/api/v1/orderBooks" | jq -c '[.order_books[]|select(.market_type=="perp" and .status=="active")|{symbol,supported_price_decimals}]' 2>/dev/null || echo '[]')
  HL=$(curl -s -m 30 -X POST -H 'Content-Type: application/json' -d '{"type":"meta"}' https://api.hyperliquid.xyz/info | jq -c '[.universe[]|{name,szDecimals}]' 2>/dev/null || echo '[]')
  if [ "$LM" != "[]" ] && [ "$HL" != "[]" ]; then
    jq -n --argjson lm "$LM" --argjson hl "$HL" --arg coins "$(ls "$ARCH" | paste -sd,)" '
      ($lm | map({(.symbol): .supported_price_decimals}) | add) as $pd |
      ($hl | map({(.name): .szDecimals}) | add) as $sz |
      {venue:"lighter", maker_bps:0, taker_bps:0, latency_ms:300, order_usd:100, max_inv:3,
       coins: ($coins | split(",") | map(select(($pd[.] != null) and ($sz[.] != null)) |
         {(.): {strategy:"frozen", half_width_ticks:5, min_spread_ticks:4, imb_pull:0.6, pause_move_ticks:2, pause_ms:2000,
                requote_ticks:1, venue_tick: (pow(10; -($pd[.]))), tick_rule:"hyperliquid", hl_sz_decimals: $sz[.]}}) | add)}' \
      > "$OUT/params_lighter.json"
    # every recorded day; only COMPLETE days (≥ 23.5 h of books) count toward the gate — today is never complete,
    # it shows in the daily list as incomplete so the dashboard can draw it hollow
    if build/mm_sim --dir "$ARCH" --params "$OUT/params_lighter.json" --daily "$OUT/g1_daily.json" > "$OUT/g1_mm_sim.log" 2>&1; then
      G1_JSON=$(jq -c --argjson D "$G1_DAYS" --argjson NET "$G1_NET_BPS" --argjson MKP "$G1_MK10_PASS" --argjson MKF "$G1_MK10_FAIL" \
          --argjson MH "$G1_MIN_HOURS" --argjson MG "$G1_MAX_GAP_MIN" '
        [.rows[] | select(.cfg=="chosen/pess")] as $r |
        # completeness is judged on the busiest market (most book events): ≥ MH hours of books, no gap > MG minutes
        ($r | group_by(.day) | map(max_by(.books // 0) as $b |
             {day: .[0].day, busiest: $b.coin, hours: $b.hours, max_gap_min: ($b.max_gap_min // null), pnl_usd: (map(.pnl_usd)|add),
              volume_usd: (map(.volume_usd)|add), fills: (map(.fills)|add), mk10_w: (map(.mk10_bps * .volume_usd)|add)}
             | .complete = (.hours >= $MH and .max_gap_min != null and .max_gap_min <= $MG))) as $days |
        [$days[] | select(.complete)] as $full |
        ($full | map(.pnl_usd) | add // 0) as $pnl | ($full | map(.volume_usd) | add // 0) as $vol |
        (if $vol > 0 then $pnl / $vol * 1e4 else null end) as $net |
        (if $vol > 0 then ($full | map(.mk10_w) | add) / $vol else null end) as $mk |
        {days: ($full|length), net_bps: $net, mk10_bps: $mk, fills: ($full|map(.fills)|add // 0), volume_usd: $vol, pnl_usd: $pnl,
         status: (if ($mk != null and $mk < $MKF and ($full|length) >= $D) then "fail"
                  elif (($full|length) >= $D and $net != null and $net >= $NET and $mk >= $MKP) then "pass" else "pending" end),
         daily: ($days | map({day, busiest, hours, max_gap_min, pnl_usd, volume_usd, fills, net_bps: (if .volume_usd > 0 then .pnl_usd/.volume_usd*1e4 else 0 end),
                              mk10_bps: (if .volume_usd > 0 then .mk10_w/.volume_usd else 0 end), complete}))}' "$OUT/g1_daily.json")
    else log "mm_sim failed (see $OUT/g1_mm_sim.log)"; fi
  else log "market metadata unavailable — G1 skipped this run"; fi
fi

# ── other venues (Extended, Aster, Paradex): the same replay at each venue's real fee → $OUT/venues.json ──────
scripts/mm_venues.sh "$OUT" || log "mm_venues.sh failed (venue comparison skipped)"

# ── G2: paper engine ──────────────────────────────────────────────────────────────────────────────────
G2_JSON='{"days":0,"cum_pnl":0,"pos_share":null,"mean_day":null,"in_range":null,"loss_stops":0,"status":"pending","daily":[],"excluded":[]}'
PW=$(grep -E '^PG_PASSWORD=' .env | head -1 | cut -d= -f2- | tr -d "'\"")
# configured markets: the installed unit (what actually runs), else the repo copy
UNIT=$HOME/.config/systemd/user/mm-paper-local.service; [ -f "$UNIT" ] || UNIT=deploy/mm-paper-local.service
NCFG=$(grep -E '^ExecStart=' "$UNIT" 2>/dev/null | grep -oE -- '--symbols [^ ]+' | head -1 | awk '{print $2}' | tr ',' '\n' | grep -c . || true)
NCFG=${NCFG:-0}
DROPS_FILE=data/mm_paper/db_drops_paper.json
DROPS=$(jq -c . "$DROPS_FILE" 2>/dev/null || echo null); [ -n "$DROPS" ] || DROPS=null
if [ -n "$PW" ]; then
  DAILY=$(PGPASSWORD="$PW" psql -h localhost -U rithmic_user -d rithmic -At -c "
    with days as (select day, sum(pnl) pnl, sum(volume) vol, sum(fills) fills, count(*) n
                  from mm_paper_daily where label='paper' and day < current_date group by day),
         starts as (select (ts at time zone 'UTC')::date as day, count(*) c from mm_paper_events where label='paper' and kind='start' group by 1),
         mins as (select (ts at time zone 'UTC')::date as day, count(distinct ts) c from mm_paper_equity where label='paper' and market='_total' group by 1),
         ls as (select (ts at time zone 'UTC')::date as day, min(ts) t from mm_paper_events where label='paper' and kind='loss_stop' group by 1),
         hm as (select e.ts, (e.ts at time zone 'UTC')::date as day, count(*) filter (where e.halted) nh,
                       bool_or(coalesce(tot.halted, false)) th, max(ls.t) lst
                from mm_paper_equity e
                left join mm_paper_equity tot on tot.label = e.label and tot.ts = e.ts and tot.market = '_total'
                left join ls on ls.day = (e.ts at time zone 'UTC')::date
                where e.label='paper' and e.market <> '_total'
                group by e.ts),
         h as (select ts, day from hm
               where not (th and lst is not null and ts >= lst)                -- the loss stop's halt is not an outage
                 and (th or nh * 2 >= greatest($NCFG, 1))),                    -- kill switch, or ≥ half the markets halted
         runs as (select day, count(*) len
                  from (select day, ts - (row_number() over (partition by day order by ts)) * interval '1 minute' g from h) x
                  group by day, g),
         mh as (select day, max(len) m from runs group by day)
    select coalesce(json_agg(json_build_object('day', d.day, 'pnl_usd', d.pnl, 'volume_usd', d.vol, 'fills', d.fills, 'markets', d.n,
             'restarts', coalesce(s.c, 0), 'hours', coalesce(mi.c, 0) / 60.0, 'max_halt_min', coalesce(mh.m, 0)) order by d.day), '[]')
    from days d left join starts s using (day) left join mins mi using (day) left join mh using (day);" 2>/dev/null || echo '[]')
  STOPS=$(PGPASSWORD="$PW" psql -h localhost -U rithmic_user -d rithmic -At -c "select count(*) from mm_paper_events where kind='loss_stop' and label='paper';" 2>/dev/null || echo 0)
  G2_JSON=$(jq -c -n --argjson d "${DAILY:-[]}" --argjson stops "${STOPS:-0}" --argjson D "$G2_DAYS" --argjson PS "$G2_POS_SHARE" \
      --argjson FC "$G2_FAIL_CUM" --argjson FS "$G2_FAIL_STOPS" --argjson LO "$BT_LOW" --argjson HI "$BT_HIGH" \
      --argjson MH "$G2_MIN_HOURS" --argjson MX "$G2_MAX_HALT_MIN" --argjson NCFG "$NCFG" --argjson drops "$DROPS" '
    ($d | map(. as $x | ($drops.days[$x.day] // 0) as $dr |
       . + {db_drops: (if $drops == null or ($drops.since // "") == "" or $x.day < $drops.since then null else $dr end)}
         | .reasons = ([ (if (.restarts // 0) > 0 then "engine restarted" else empty end),
                         (if .hours < $MH then "ran \(.hours*10|round/10) h < \($MH) h" else empty end),
                         (if .markets < $NCFG then "\(.markets)/\($NCFG) markets" else empty end),
                         (if .max_halt_min > $MX then "outage (kill / feed / ≥ half the markets halted) \(.max_halt_min) min in a row" else empty end),
                         (if .db_drops == null then "DB drops unknown (before the drop ledger)" elif .db_drops > 0 then "\(.db_drops) DB drop(s)" else empty end) ])
         | .complete = (.reasons | length == 0))) as $all |
    ($all | map(select(.complete))) as $d |          # only complete days are counted; the rest are listed with why
    ($d|length) as $n | ($d|map(.pnl_usd)|add // 0) as $cum |
    (if $n > 0 then ($d|map(select(.pnl_usd > 0))|length) / $n else null end) as $pos |
    (if $n > 0 then $cum / $n else null end) as $mean |
    {days: $n, cum_pnl: $cum, pos_share: $pos, mean_day: $mean, in_range: (if $mean == null then null else ($mean >= $LO and $mean <= $HI) end),
     loss_stops: $stops,
     status: (if ($cum < $FC or $stops >= $FS) then "fail"
              elif ($n >= $D and $cum > 0 and $pos >= $PS and $mean >= $LO and $mean <= $HI and $stops <= 1) then "pass" else "pending" end),
     daily: ($d | to_entries | map(.value + {cum_pnl: ([$d[0:(.key+1)][].pnl_usd] | add)})),
     excluded: ($all | map(select(.complete | not) | {day, pnl_usd, reasons}))}')
fi

# ── assemble ──────────────────────────────────────────────────────────────────────────────────────────
jq -n --arg stamp "$STAMP" --argjson g1 "$G1_JSON" --argjson g2 "$G2_JSON" \
  --argjson g1d "$G1_DAYS" --argjson g1n "$G1_NET_BPS" --argjson g1p "$G1_MK10_PASS" --argjson g1f "$G1_MK10_FAIL" \
  --argjson g2d "$G2_DAYS" --argjson g2p "$G2_POS_SHARE" --argjson g2fc "$G2_FAIL_CUM" --argjson g2fs "$G2_FAIL_STOPS" --argjson lo "$BT_LOW" --argjson hi "$BT_HIGH" '
  {generated: $stamp, backtest_range_usd: [$lo, $hi],
   gates: [
    {id:"G1", name:"Data: the setting on Lighter’s own books", status: $g1.status, progress: {done: $g1.days, needed: $g1d},
     metrics: [{name:"complete days", value: $g1.days, rule: ("≥ " + ($g1d|tostring)), ok: ($g1.days >= $g1d)},
               {name:"net bps", value: $g1.net_bps, rule: ("≥ " + ($g1n|tostring)), ok: ($g1.net_bps != null and $g1.net_bps >= $g1n)},
               {name:"10 s markout bps", value: $g1.mk10_bps, rule: ("≥ " + ($g1p|tostring) + " (fail < " + ($g1f|tostring) + ")"), ok: ($g1.mk10_bps != null and $g1.mk10_bps >= $g1p)},
               {name:"fills", value: $g1.fills, rule: "—", ok: null}]},
    {id:"G2", name:"Paper: 24/7 engine on live Lighter", status: $g2.status, progress: {done: $g2.days, needed: $g2d},
     metrics: [{name:"complete paper days", value: $g2.days, rule: ("≥ " + ($g2d|tostring)), ok: ($g2.days >= $g2d)},
               {name:"cumulative P&L $", value: $g2.cum_pnl, rule: ("> 0 (fail < " + ($g2fc|tostring) + ")"), ok: ($g2.days > 0 and $g2.cum_pnl > 0)},
               {name:"positive-day share", value: $g2.pos_share, rule: ("≥ " + ($g2p|tostring)), ok: ($g2.pos_share != null and $g2.pos_share >= $g2p)},
               {name:"mean day $", value: $g2.mean_day, rule: ("inside " + ($lo|tostring) + "…" + ($hi|tostring)), ok: $g2.in_range},
               {name:"loss stops", value: $g2.loss_stops, rule: ("≤ 1 (fail ≥ " + ($g2fs|tostring) + ")"), ok: ($g2.loss_stops <= 1)}]},
    {id:"G3", name:"Pilot: $250 live, $10 quotes, 15 markets", status:"pending", progress:{done:0, needed:10},
     metrics: [{name:"real vs paper fills", value:null, rule:"within 20%", ok:null}, {name:"real slippage bps", value:null, rule:"≤ 1", ok:null},
               {name:"P&L sign agrees with paper", value:null, rule:"yes", ok:null}]},
    {id:"G4", name:"Scale: $2k → $5k → $10k", status:"pending", progress:{done:0, needed:3},
     metrics: [{name:"G3 numbers hold at each step", value:null, rule:"2 weeks per step", ok:null}, {name:"markout vs size", value:null, rule:"no degradation", ok:null}]}],
   g1_daily: $g1.daily, g2_daily: $g2.daily, g2_excluded: ($g2.excluded // [])}
  # a pending gate has not failed: its unmet rules show as open (null), not as ✗
  | .gates |= map(if .status == "pending" then .metrics |= map(if .ok == true then . else .ok = null end) else . end)' \
  > "$OUT/gates.json.tmp" && mv "$OUT/gates.json.tmp" "$OUT/gates.json"

# ── flat tables for the dashboard (claude.ai "Market-Maker Gates" reads these files) ───────────────────
mkdir -p "$OUT/dash"
jq -c '{rows: [.gates[] | {id, name, status, done: .progress.done, needed: .progress.needed}]}' "$OUT/gates.json" > "$OUT/dash/gates_status.json"
jq -c '{rows: [.gates[] | .id as $g | .metrics[] | {gate: $g, name, value, rule, ok}]}' "$OUT/gates.json" > "$OUT/dash/gate_metrics.json"
jq -c '{rows: .g1_daily}' "$OUT/gates.json" > "$OUT/dash/g1_daily.json"
jq -c '{rows: .g2_daily}' "$OUT/gates.json" > "$OUT/dash/g2_daily.json"
jq -c '{rows: [{generated, bt_low: .backtest_range_usd[0], bt_high: .backtest_range_usd[1]}]}' "$OUT/gates.json" > "$OUT/dash/gates_meta.json"

# ── report + notify ───────────────────────────────────────────────────────────────────────────────────
{
  echo "# Market-making gates — $STAMP"; echo
  jq -r '.gates[] | "## \(.id) · \(.name) — **\(.status|ascii_upcase)** (\(.progress.done)/\(.progress.needed))\n" +
         (.metrics | map("- \(.name): \(if .value == null then "—" else (if (.value|type)=="number" then (.value*100|round/100|tostring) else (.value|tostring) end) end)  (rule \(.rule))\(if .ok == true then " ✓" elif .ok == false then " ✗" else "" end)") | join("\n")) + "\n"' "$OUT/gates.json"
  echo "G1 daily:"; jq -r '.g1_daily[] | "- \(.day) \(if .complete then "full" else (.hours*10|round/10|tostring) + " h, max gap " + (if .max_gap_min == null then "?" else (.max_gap_min*10|round/10|tostring) end) + " min" end): P&L \(.pnl_usd*100|round/100), \(.fills) fills, \(.net_bps*100|round/100) bps, mk10 \(.mk10_bps*100|round/100)"' "$OUT/gates.json"
  echo; echo "G2 daily:"; jq -r '.g2_daily[] | "- \(.day): P&L \(.pnl_usd*100|round/100) (cum \(.cum_pnl*100|round/100)), \(.fills) fills, \(.markets) markets"' "$OUT/gates.json"
  if [ "$(jq '.g2_excluded | length' "$OUT/gates.json")" != 0 ]; then
    echo; echo "G2 days not counted (incomplete):"; jq -r '.g2_excluded[] | "- \(.day): P&L \(.pnl_usd*100|round/100) — \(.reasons | join("; "))"' "$OUT/gates.json"
  fi
  if [ -s "$OUT/venues.json" ]; then
    echo; echo "## Other venues — frozen setting at each venue's real maker fee, 100 ms (complete days only)"
    jq -r '.venues[] | "- \(.venue) (maker \(.maker_bps) bps): \(.complete_days)/\(.days) complete days, net \(if .net_bps==null then "—" else (.net_bps*100|round/100|tostring) end) bps, mk10 \(if .mk10_bps==null then "—" else (.mk10_bps*100|round/100|tostring) end), P&L \(.pnl_usd*100|round/100), \(.fills) fills, \(.markets) markets"' "$OUT/venues.json"
  fi
} > "$OUT/report.md"
LINE=$(jq -r '"MM gates " + (.generated|.[0:10]) + " · " + (.gates | map("\(.id) \(.status) \(.progress.done)/\(.progress.needed)") | join(" · ")) +
  (if .g2_daily|length > 0 then " · paper yesterday $" + (.g2_daily[-1].pnl_usd*100|round/100|tostring) + " cum $" + (.g2_daily[-1].cum_pnl*100|round/100|tostring) else "" end) +
  (if .gates[0].metrics[1].value != null then " · Lighter re-test " + (.gates[0].metrics[1].value*100|round/100|tostring) + " bps, mk10 " + (.gates[0].metrics[2].value*100|round/100|tostring) else "" end)' "$OUT/gates.json")
log "$LINE"
if [ "$NOTIFY" = 1 ] && command -v grid-notify >/dev/null 2>&1; then grid-notify "$LINE" >/dev/null 2>&1 || true; fi

# all-data view for the /mm page (history + every live recording + regime filters; fail-open)
[ -x scripts/mm_alldata.sh ] && scripts/mm_alldata.sh || true
