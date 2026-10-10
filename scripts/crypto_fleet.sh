#!/usr/bin/env bash
# crypto_fleet.sh — every paper-fleet strategy mirrored to crypto (founder 2026-10-10: "test all strategies for
# crypto"). Paper/research only: nothing here touches a live account.
#
#   scripts/crypto_fleet.sh build            config/paper_fleet_crypto_{btc,eth}.json from config/paper_fleet.json
#   scripts/crypto_fleet.sh register         register the ids in paper_strategies (account_label 'crypto')
#   scripts/crypto_fleet.sh replay FROM TO   replay both files over recorded ticks under label crypto_hist
#   scripts/crypto_fleet.sh report [label]   leaderboard summary (default crypto_hist) -> data/crypto/report.md
#   scripts/crypto_fleet.sh month-config CODE YYYY-MM   data/history/configs/paper_fleet_crypto_<code>_<YYYY-MM>.json:
#                                            the built file with every point knob × (month's mean close / knob_ref_price)
#
# Market: Binance USDⓈ-M perpetuals BTCUSDT / ETHUSDT (ticks from scripts/crypto_backfill.sh and the live
# crypto_collector). One contract = 0.1 coin (point_value 0.1, the CME micro size). Point knobs (stops, trails,
# buffers…) are scaled by the coin's mean RTH-range ratio to NQ over the sessions both have, snapped to the tick
# with floors of 8 ticks for stops / 4 for trails — the method fleet_expand.sh uses for ES/YM/RTY/GC/CL.
# Costs (2026-10-10, relative so history is charged at its own price level): commission_pct 0.001 = 0.10 % of
# notional round trip (Binance taker 0.05 % a side, half on each fill at its price) and slippage_bps = NQ's 3 ticks
# (0.75 pt) × the ratio, expressed in bps of the build-time price. paper_engine uses these instead of the fixed
# commission_rt / slippage_ticks, which stay in the file for reference only. knob_ref_price = the price the point
# knobs were scaled at; `month-config` rescales them to a month's own price level (constant in % of price) for the
# history replays (scripts/history_month.sh). Ids are prefixed btc_/eth_ (strategy_id is global).
# History has NO quotes (Binance stopped publishing futures bookTicker files in 2024): book-gated variants
# (__inv/__imb/__sg/…) cannot fire on replayed days and are judged on the forward feed only.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
FLEET=config/paper_fleet.json
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -X -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
# code | feed/traded symbol | tick | reference symbol (MTF SMT module)
COINS='btc BTCUSDT 0.1 ETHUSDT
eth ETHUSDT 0.01 BTCUSDT'

ratio_of() {   # mean RTH range of $1 / mean RTH range of NQ over the sessions both have
  "${PSQL[@]}" -c "SELECT round((avg(c.range_pts) / avg(n.range_pts))::numeric, 4) FROM session_stats c
                   JOIN session_stats n ON n.session_date = c.session_date AND n.symbol = 'NQ'
                   WHERE c.symbol = '$1' AND c.bars >= 300 AND n.bars >= 300"; }
last_price() { "${PSQL[@]}" -c "SELECT price FROM ticks WHERE symbol = '$1' ORDER BY ts_event DESC LIMIT 1"; }

cmd_build() {
  "${PSQL[@]}" -c "SELECT refresh_session_stats(60)" >/dev/null
  while read -r code sym tick ref; do
    local r px comm slip out=config/paper_fleet_crypto_$code.json
    r=$(ratio_of "$sym"); px=$(last_price "$sym")
    [[ -n "$r" && -n "$px" ]] || { echo "$code: no session_stats / ticks for $sym — run crypto_backfill.sh first"; return 1; }
    comm=$(awk -v p="$px" 'BEGIN{printf "%.2f", p * 0.1 * 0.001}')
    slip=$(awk -v r="$r" -v t="$tick" 'BEGIN{s = int(0.75 * r / t + 0.5); print (s < 1 ? 1 : s)}')
    local slip_bps; slip_bps=$(awk -v s="$slip" -v t="$tick" -v p="$px" 'BEGIN{printf "%.2f", s * t / p * 10000}')
    jq --arg code "$code" --arg sym "$sym" --argjson tick "$tick" --arg ref "$ref" --argjson r "$r" \
       --argjson comm "$comm" --argjson slip "$slip" --argjson sbps "$slip_bps" --argjson px "$px" '
      def snap: (((. / $tick) | round) * $tick * 1000000 | round) / 1000000;
      def floor_ticks($k): if $k == "sl_points" then 8 elif $k == "trail_step" then 4 else 1 end;
      def scaled($k): if type == "number" and . > 0
                      then ((. * $r) | snap) as $v | ((floor_ticks($k) * $tick * 1000000 | round) / 1000000) as $min
                           | (if $v < $min then $min else $v end)
                      else . end;
      ["sl_points","trail_step","trail_be_trigger","trail_be_offset","breakout_buffer","max_entry_offset",
       "tp_points","trail_step_trend","trail_step_range","gap_min_pts","gf_min_pts"] as $PTS
      | def mirror_params:
          ((if .engine == "mtf_scalper" then {sl_points: 15, tp_points: 35}
            else {sl_points: 15, trail_step: 10, trail_be_trigger: 3, trail_be_offset: 1} end) + .params)
          | with_entries(.key as $k | if ($PTS | index($k)) then .value |= scaled($k) else . end);
      {_comment: ("2026-10-10 CRYPTO MIRROR (founder: \"test all strategies for crypto\") — every config/paper_fleet.json strategy on "
                  + $sym + ", 1 contract = 0.1 coin; point knobs x " + ($r|tostring) + " (mean RTH-range ratio to NQ), snapped to the "
                  + ($tick|tostring) + " tick, floors sl 8 / trail 4 / other 1 tick; commission $" + ($comm|tostring)
                  + " RT (0.10 % of notional), slippage " + ($slip|tostring) + " ticks — both as RELATIVE costs since 2026-10-10: commission_pct 0.001,"
                  + " slippage_bps " + ($sbps|tostring) + " (at " + ($px|tostring) + "). Paper only. scripts/crypto_fleet.sh."),
       account_label: "crypto", symbol: $sym, feed_symbol: $sym, reference_symbol: $ref, exchange: "BINANCE",
       point_value: 0.1, tick_size: $tick, starting_balance: 25000.0, daily_loss_limit: 0, trailing_drawdown_cap: 0,
       consistency_cap_pct: 0.3, commission_rt: $comm, slippage_ticks: $slip,
       commission_pct: 0.001, slippage_bps: $sbps, knob_ref_price: $px, knob_ratio: $r,
       poll_ms: 100, feed_gap_reset_secs: 300,
       strategy_daily_loss_limit: -250.0,
       strategies: [ .strategies[] | {id: ($code + "_" + .id), engine, enabled: true,
                                      params: (mirror_params + {mirror_of: .id})} ]}
    ' "$FLEET" > "$out.tmp" && mv "$out.tmp" "$out"
    echo "$code: $(jq '.strategies|length' "$out") strategies -> $out (ratio $r, last $px, commission 0.10 % RT, slippage $slip_bps bps)"
  done <<< "$COINS"
}

cmd_month_config() {   # point knobs rescaled to a month's price level (constant in % of price)
  local code="$1" m="$2" src="config/paper_fleet_crypto_$1.json" out
  [[ "$m" =~ ^[0-9]{4}-[0-9]{2}$ && -f "$src" ]] || { echo "usage: month-config {btc|eth} YYYY-MM (and $src must exist)"; return 2; }
  local sym tick ref f; sym=$(jq -r .feed_symbol "$src"); tick=$(jq .tick_size "$src"); ref=$(jq '.knob_ref_price // empty' "$src")
  [[ -n "$ref" ]] || { echo "$src has no knob_ref_price — rebuild it (crypto_fleet.sh build)"; return 1; }
  local px; px=$("${PSQL[@]}" -c "SELECT avg(close) FROM bars_1m WHERE symbol = '$sym' AND minute >= '$m-01' AND minute < ('$m-01'::date + interval '1 month')")
  [[ -n "$px" ]] || { echo "no $sym bars in bars_1m for $m (dbn_tool agg-bars first)"; return 1; }
  f=$(awk -v a="$px" -v b="$ref" 'BEGIN{printf "%.6f", a / b}')
  mkdir -p data/history/configs; out="data/history/configs/paper_fleet_crypto_${code}_$m.json"
  jq --argjson f "$f" --argjson tick "$tick" --arg m "$m" --argjson px "$px" '
    def snap: (((. / $tick) | round) * $tick * 1000000 | round) / 1000000;
    def floor_ticks($k): if $k == "sl_points" then 8 elif $k == "trail_step" then 4 else 1 end;
    ["sl_points","trail_step","trail_be_trigger","trail_be_offset","breakout_buffer","max_entry_offset",
     "tp_points","trail_step_trend","trail_step_range","gap_min_pts","gf_min_pts"] as $PTS
    | ._comment = ("history month " + $m + ": point knobs x " + ($f|tostring) + " (mean close " + ($px|tostring)
                   + " / knob_ref_price) — generated by scripts/crypto_fleet.sh month-config; do not edit")
    | .knob_month_factor = $f
    | .strategies |= map(.params |= with_entries(.key as $k | if ($PTS | index($k)) and (.value | type) == "number" and .value > 0
          then .value |= ((. * $f | snap) as $v | ((floor_ticks($k) * $tick * 1000000 | round) / 1000000) as $min | if $v < $min then $min else $v end)
          else . end))' "$src" > "$out.tmp" && mv "$out.tmp" "$out"
  echo "$out (factor $f)"
}

cmd_register() {
  local f tsv
  for f in config/paper_fleet_crypto_*.json; do
    tsv=$(mktemp)
    jq -r '.strategies[] | [.id, .engine, (.params|tojson), (.enabled|tostring)] | @tsv' "$f" > "$tsv"
    printf '%s\n' "CREATE TEMP TABLE s(id text, engine text, params jsonb, enabled boolean);" "\\copy s FROM '$tsv'" \
      "INSERT INTO paper_strategies (strategy_id, account_label, engine, params_json, enabled)
         SELECT id, 'crypto', engine, params, enabled FROM s
       ON CONFLICT (strategy_id) DO UPDATE SET params_json = EXCLUDED.params_json, engine = EXCLUDED.engine;" |
      "${PSQL[@]}" || { rm -f "$tsv"; echo "register FAILED for $f"; return 1; }
    rm -f "$tsv"; echo "registered $f: $(jq '.strategies|length' "$f") ids"
  done
}

# Same guard as fleet_expand.sh replay: never during weekday regular hours (the live executor polls the same
# ticks table) or the Asia session (live rota window).
busy_window() {
  local dow hm; dow=$(TZ=America/New_York date +%u); hm=$((10#$(TZ=America/New_York date +%H%M)))
  (( dow <= 5 && hm >= 925 && hm < 1605 )) && { echo "regular hours"; return 0; }
  (( hm >= 2355 || hm < 235 )) && (( dow != 6 )) && { echo "Asia session"; return 0; }
  return 1; }

cmd_replay() {
  local from="${1:?from YYYY-MM-DD}" to="${2:?to YYYY-MM-DD}" label="${LABEL:-crypto_hist}" w pids=() f
  if w=$(busy_window) && [[ "${FORCE:-0}" != 1 ]]; then echo "refused: $w (FORCE=1 to override)"; return 1; fi
  [[ "$label" == crypto* ]] || { echo "label must start with crypto"; return 1; }
  mkdir -p data/crypto
  # whole calendar days in UTC — crypto never closes; idempotent: the label's rows in the range are replaced
  for f in config/paper_fleet_crypto_*.json; do
    local pfx; pfx=$(jq -r '.strategies[0].id | split("_")[0]' "$f")
    "${PSQL[@]}" -c "DELETE FROM paper_trades WHERE account_label = '$label' AND strategy_id LIKE '${pfx}\_%'
                     AND entry_time >= '$from 00:00+00' AND entry_time < ('$to'::date + 1)::timestamp AT TIME ZONE 'UTC'"
    echo "$(date -Is) replay $f label $label $from..$to"
    nice -n 15 ionice -c 3 ./build/paper_engine --config "$f" --account-label "$label" \
      --replay-from "$(TZ=America/New_York date -d "$from 00:00 UTC" '+%F %H:%M')" \
      --replay-to   "$(TZ=America/New_York date -d "$to 23:59 UTC" '+%F %H:%M')" \
      > "data/logs/paper_${label}_$pfx.log" 2>&1 &
    pids+=($!)
  done
  local rc=0; for p in "${pids[@]}"; do wait "$p" || rc=1; done
  echo "$(date -Is) replay done rc=$rc"; return $rc
}

cmd_report() {
  local label="${1:-crypto_hist}" out=data/crypto/report.md; mkdir -p data/crypto
  {
    echo "# Crypto paper fleet — label \`$label\` ($(date -u '+%F %H:%M UTC'))"; echo
    echo "1 contract = 0.1 BTC / 0.1 ETH; net = after costs (Binance taker 0.10 % round trip + scaled slippage), gross = price move only."
    echo "Book-gated variants cannot fire on history (no quotes). Crypto trades every day, so weekends count as sessions."; echo
    for c in btc eth; do
      echo "## ${c^^}: by engine"; echo; echo "| engine | strategies with trades | trades | gross \$ (before costs) | net \$ | profitable net | profitable gross |"; echo "|---|---:|---:|---:|---:|---:|---:|"
      "${PSQL[@]}" -F '|' -c "WITH s AS (SELECT t.strategy_id, p.engine, count(*) n, sum(t.pnl_usd) net, sum(t.pnl_pts * t.qty * 0.1) gross FROM paper_trades t
              JOIN paper_strategies p USING (strategy_id) WHERE t.account_label = '$label' AND t.strategy_id LIKE '${c}\_%'
              GROUP BY 1, 2)
            SELECT '| ' || engine, count(*), sum(n), round(sum(gross)::numeric, 0), round(sum(net)::numeric, 0),
                   count(*) FILTER (WHERE net > 0), count(*) FILTER (WHERE gross > 0) || ' |' FROM s GROUP BY engine ORDER BY 1"
      echo; echo "## ${c^^}: top 25 (≥20 trades, ≥5 days, PF ≥ 1.3)"; echo
      echo "| strategy | trades | days | win % | PF | net \$ | worst day \$ |"; echo "|---|---:|---:|---:|---:|---:|---:|"
      "${PSQL[@]}" -F '|' -c "WITH t AS (SELECT strategy_id, pnl_usd, (entry_time AT TIME ZONE 'UTC')::date d FROM paper_trades
              WHERE account_label = '$label' AND strategy_id LIKE '${c}\_%'),
            day AS (SELECT strategy_id, d, sum(pnl_usd) p FROM t GROUP BY 1, 2),
            s AS (SELECT strategy_id, count(*) n, count(DISTINCT d) days, avg((pnl_usd > 0)::int) wr,
                         sum(pnl_usd) FILTER (WHERE pnl_usd > 0) / NULLIF(-sum(pnl_usd) FILTER (WHERE pnl_usd < 0), 0) pf,
                         sum(pnl_usd) net FROM t GROUP BY 1)
            SELECT '| ' || s.strategy_id, n, days, round(100 * wr), round(pf::numeric, 2), round(net::numeric, 0),
                   round((SELECT min(p) FROM day WHERE day.strategy_id = s.strategy_id)::numeric, 0) || ' |'
            FROM s WHERE n >= 20 AND days >= 5 AND pf >= 1.3 ORDER BY net DESC LIMIT 25"
      echo
    done
  } > "$out"
  echo "wrote $out"
}

case "${1:-}" in
  build) cmd_build ;; register) cmd_register ;; replay) shift; cmd_replay "$@" ;; report) shift; cmd_report "$@" ;; month-config) shift; cmd_month_config "$@" ;;
  *) sed -n 2,9p "$0"; exit 2 ;;
esac
