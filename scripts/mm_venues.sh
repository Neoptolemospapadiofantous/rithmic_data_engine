#!/usr/bin/env bash
# mm_venues.sh — the venue comparison of the market-making plan (docs/market_making_plan.md §4 "other venues"): the
# frozen setting replayed flat-to-flat (build/mm_sim --daily) over every recorded day of each dex_recorder archive
# (Extended, Aster, Paradex), at that venue's REAL maker fee and with its own price grid, in the same Hyperliquid tick
# unit the backtests used. Writes $OUT/venues.json (+ dash/venues.json, dash/venues_daily.json) and prints one line
# per venue. mm_gates.sh calls it after G1 (fail-open); by hand: scripts/mm_venues.sh [OUT_DIR].
#
# Per-venue costs (2026-10-25, from each venue's fee page): Extended maker 0 / taker 2.5 bps; Aster maker 0 / taker
# 3.5 bps (taker rate not published for API users — assumed; only flattens pay it); Paradex "Pro" (API) maker 0.3 bps
# / taker 4.5 bps (the retail UI tier is 0/0 but API orders are Pro). Delay 100 ms for all three: none imposes a
# Standard-account delay like Lighter's 200 ms, so this is the near-server figure — the measured delay curve is flat
# below 100 ms. A venue's day counts as complete at ≥ 23.5 h of books, like G1.
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-data/mm_gates}; mkdir -p "$OUT/dash"
ROOT=/media/theone/381C48C51C48803C/rithmic_dex
STAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)
log() { printf '[%s] [mm_venues] %s\n' "$(date -u +%H:%M:%S)" "$*"; }
[ -x build/mm_sim ] || { log "build/mm_sim missing"; exit 0; }
HL=$(curl -s -m 30 -X POST -H 'Content-Type: application/json' -d '{"type":"meta"}' https://api.hyperliquid.xyz/info | jq -c '[.universe[]|{name,szDecimals}]' 2>/dev/null || echo '[]')
[ "$HL" != "[]" ] || { log "Hyperliquid meta unavailable — skipped this run"; exit 0; }

ROWS='[]'; DAILY='[]'
for V in extended aster paradex; do
  case $V in
    extended) MAKER=0;   TAKER=2.5;;
    aster)    MAKER=0;   TAKER=3.5;;
    paradex)  MAKER=0.3; TAKER=4.5;;
  esac
  ARCH="$ROOT/$V"
  [ -d "$ARCH" ] && [ -s "$ARCH/markets.json" ] || { log "$V: no archive yet"; continue; }
  COINS=$(ls "$ARCH" | grep -v '\.json$' | paste -sd,)
  jq -n --argjson hl "$HL" --slurpfile mk "$ARCH/markets.json" --arg coins "$COINS" --arg venue "$V" --argjson maker "$MAKER" --argjson taker "$TAKER" '
    ($hl | map({(.name): .szDecimals}) | add) as $sz | ($mk[0].markets) as $m |
    {venue: $venue, maker_bps: $maker, taker_bps: $taker, latency_ms: 100, order_usd: 100, max_inv: 3,
     coins: ($coins | split(",") | map(select($m[.] != null and $sz[.] != null) |
       {(.): {strategy:"frozen", half_width_ticks:5, min_spread_ticks:4, imb_pull:0.6, pause_move_ticks:2, pause_ms:2000,
              requote_ticks:1, venue_tick: $m[.].tick, tick_rule:"hyperliquid", hl_sz_decimals: $sz[.]}}) | add // {})}' \
    > "$OUT/params_$V.json"
  if ! build/mm_sim --dir "$ARCH" --params "$OUT/params_$V.json" --daily "$OUT/venue_${V}_daily.json" > "$OUT/venue_${V}_mm_sim.log" 2>&1; then
    log "$V: mm_sim failed (see $OUT/venue_${V}_mm_sim.log)"; continue
  fi
  ROW=$(jq -c --arg venue "$V" --argjson maker "$MAKER" '
    [.rows[] | select(.cfg=="chosen/pess")] as $r |
    ($r | group_by(.day) | map({day: .[0].day, hours: (map(.hours)|max), pnl_usd: (map(.pnl_usd)|add), volume_usd: (map(.volume_usd)|add),
         fills: (map(.fills)|add), markets: length, mk10_w: (map(.mk10_bps * .volume_usd)|add)})) as $days |
    [$days[] | select(.hours >= 23.5)] as $full |
    ($full | map(.pnl_usd) | add // 0) as $pnl | ($full | map(.volume_usd) | add // 0) as $vol |
    {venue: $venue, maker_bps: $maker, latency_ms: 100, days: ($days|length), complete_days: ($full|length),
     pnl_usd: $pnl, volume_usd: $vol, fills: ($full|map(.fills)|add // 0), markets: ($r | map(.coin) | unique | length),
     net_bps: (if $vol > 0 then $pnl / $vol * 1e4 else null end),
     mk10_bps: (if $vol > 0 then ($full | map(.mk10_w) | add) / $vol else null end),
     daily: ($days | map({venue: $venue, day, hours, pnl_usd, volume_usd, fills, markets, complete: (.hours >= 23.5),
                          net_bps: (if .volume_usd > 0 then .pnl_usd/.volume_usd*1e4 else 0 end),
                          mk10_bps: (if .volume_usd > 0 then .mk10_w/.volume_usd else 0 end)}))}' "$OUT/venue_${V}_daily.json")
  ROWS=$(jq -c --argjson r "$ROW" '. + [$r | del(.daily)]' <<< "$ROWS")
  DAILY=$(jq -c --argjson r "$ROW" '. + $r.daily' <<< "$DAILY")
  log "$(jq -r '"\(.venue): \(.complete_days) complete day(s) of \(.days), net \(if .net_bps==null then "—" else (.net_bps*100|round/100|tostring) end) bps, mk10 \(if .mk10_bps==null then "—" else (.mk10_bps*100|round/100|tostring) end), \(.fills) fills, \(.markets) markets"' <<< "$ROW")"
done
jq -n --arg stamp "$STAMP" --argjson rows "$ROWS" --argjson daily "$DAILY" '{generated: $stamp, venues: $rows, daily: $daily}' > "$OUT/venues.json.tmp" && mv "$OUT/venues.json.tmp" "$OUT/venues.json"
jq -c '{rows: .venues}' "$OUT/venues.json" > "$OUT/dash/venues.json"
jq -c '{rows: .daily}' "$OUT/venues.json" > "$OUT/dash/venues_daily.json"
