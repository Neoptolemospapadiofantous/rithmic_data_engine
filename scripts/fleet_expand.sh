#!/usr/bin/env bash
# fleet_expand.sh — build STAGED paper-fleet expansions (2026-10-01, founder: "we need to expand our fleet",
# then: around what works + more markets + new strategy types + every combination; start after the
# 2026-10-01 16:05 ET reload has proven the 4,818-strategy load).
#
# Writes config/staging/*.json only. Nothing here is read by a running engine: a staged file joins
# the paper fleet when it is merged (`merge`) or given its own paper-engine unit. Every subcommand is
# idempotent and never edits an existing strategy.
#
#   winners   tight exit grids around the strategies that held up forward AND in replay
#             (trail_step 10/15/20/25 x trail_be_trigger 5/8/12, each alone and with the best gates
#             rg_nochase / rg_range / inv)            -> config/staging/paper_winners.json   (NQ, tradeify)
#   markets   every enabled NQ base mirrored to ES, YM, RTY, GC, CL as the micro contract, point knobs
#             scaled by the market's range ratio to NQ, plus __inv / __rg_nochase / __rg_range
#                                                     -> config/staging/paper_fleet_<mkt>.json (own label)
#   combos    every pair of filters from DIFFERENT categories (book x regime x volume x exit) on every
#             NQ base                                 -> config/staging/paper_fleet_combo.json (label nq_combo)
#   register  insert the staged ids into paper_strategies (account_label 'staging', so the fleet audit's
#             row count is unaffected) — replay trades of unregistered ids are dropped by the FK
#   replay    replay every staged file over its feed's recorded sessions (full Globex days) under
#             research labels exp_<name>; refuses during weekday regular hours and pauses through the Asia
#             session 23:55-02:35 ET (the live executor and the Asia dry run read the same tick table)
#   activate {nq|markets|combo|<code>|all} --yes   put staged fleets into service: nq = merge + restart the NQ
#             engine; markets / combo = config/paper_fleet_<code>.json + unit paper-engine-local-<code>
#   merge     append paper_winners.json (+ paper_newmodes.json when present) to config/paper_fleet.json
#             with a backup in data/backups/fleet/ — run only after the current fleet load is proven
#
#   newmodes  the 2026-10-01 trend modes level_fade / vwap_reclaim / range_break on a small grid + __inv
#                                                     -> config/staging/paper_newmodes.json  (NQ, tradeify)
#
# Usage: scripts/fleet_expand.sh {winners|markets|combos|newmodes|register|replay|activate|merge|all} [--dry-run]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
CMD="${1:-}"; DRY=0; [[ "${2:-}" == "--dry-run" ]] && DRY=1
FLEET=config/paper_fleet.json
STAGE=config/staging; mkdir -p "$STAGE"

# Filter definitions — identical to the ones already in the fleet (read from it 2026-10-01) and to
# scripts/fleet_add_variants.sh. Category decides which pairs `combos` builds (never two from one).
OVERLAYS='{
  "inv":        {"cat":"book",   "p":{"imbalance_max":0.45}},
  "imb":        {"cat":"book",   "p":{"imbalance_min":0.55}},
  "sg":         {"cat":"book",   "p":{"spread_gate_rel":1.5}},
  "rg_trend":   {"cat":"regime", "p":{"regime_min_eff":0.5, "regime_min_minutes":30, "regime_with_move":1}},
  "rg_range":   {"cat":"regime", "p":{"regime_max_eff":0.4, "regime_min_minutes":30}},
  "rg_expand":  {"cat":"regime", "p":{"regime_min_range_atr":0.35, "regime_min_minutes":15}},
  "rg_fade":    {"cat":"regime", "p":{"regime_min_move_atr":0.35, "regime_min_minutes":30, "regime_with_move":-1}},
  "rg_nochase": {"cat":"regime", "p":{"regime_max_move_atr":0.5, "regime_min_minutes":15}},
  "rvol15":     {"cat":"volume", "p":{"rvol_min":1.5, "rvol_bars":20}},
  "tr25":       {"cat":"exit",   "p":{"trail_step":25}}
}'

# jq helpers shared by every subcommand
JQ_LIB='
  def start_hhmm:
    if .engine == "orb" then ((.params.session_open_hour // 9) * 100 + (.params.session_open_min // 30))
    elif .engine == "trend" then (.params.win_start // 930)
    elif .engine == "mtf_scalper" then ((.params.session_window // "0900-1200") | .[0:4] | tonumber)
    else null end;
  # regime gates measure the session from the 09:30 ET open — only bases that trade in regular hours
  def rth: (start_hhmm) as $s | ($s != null and $s >= 900 and $s < 1600);
  def has_trail: (.engine != "mtf_scalper");
  def is_base: (.id | contains("__") | not) and .enabled;
  def ov_ok($name): ($ov[$name].cat != "regime" or rth) and ($name != "tr25" or has_trail);
'

write_or_show() {   # write_or_show <tmp> <dest> <label>
  local n; n=$(jq '.strategies | length' "$1")
  if (( DRY )); then echo "$3: $n strategies (dry run — $2 not written)"; rm -f "$1"; return; fi
  mv "$1" "$2"; echo "$3: $n strategies -> $2"
}

cmd_winners() {
  local tmp; tmp=$(mktemp)
  jq --argjson ov "$OVERLAYS" "$JQ_LIB"'
    ["fib_pb_1m_deep","fib_pb_1m_deep__am","keltner_ride_15_1m_chand","keltner_ride_15_5m","delta_div_3m",
     "delta_div_2m","ichimoku_1m_fast","trend_ema_pb_9_21_1m","orb_t1000_2nd_5","orb_2_15_10","orb_5_10_5",
     "volume_burst_1m_notrend","volume_burst_1m_vm15","mr_vwap_fade_5m"] as $W
    | ["mtf_london_v5","mtf_asia_0000_0230"] as $M
    | . as $root
    | (.strategies | map(.id)) as $have
    | (.strategies | map({key:.id, value:.}) | from_entries) as $by
    | [ ( $W[] | $by[.] | select(.) as $b
          | [10,15,20,25][] as $ts | [5,8,12][] as $be | [null,"rg_nochase","rg_range","inv"][] as $g
          | select($g == null or ($b | ov_ok($g)))
          | ("x_ts\($ts)_be\($be)" + (if $g then "+" + $g else "" end)) as $tag
          | {id: ($b.id + "__" + $tag), engine: $b.engine, enabled: true,
             params: ($b.params + {trail_step: $ts, trail_be_trigger: $be}
                      + (if $g then $ov[$g].p else {} end)
                      + {base_id: $b.id, overlay: $tag})} ),
        ( $M[] | $by[.] | select(.) as $b
          | ["inv","imb","rg_nochase","rg_range","rvol15"][] as $g | select($b | ov_ok($g))
          | {id: ($b.id + "__" + $g), engine: $b.engine, enabled: true,
             params: ($b.params + $ov[$g].p + {base_id: $b.id, overlay: $g})} )
      ]
    | map(select(.id as $i | $have | index($i) | not)) | unique_by(.id) as $new
    | ($root | del(.strategies) | ._comment = "STAGED 2026-10-01 — exit grids + gates around the strategies that held up forward and in replay (fleet_expand.sh winners). NQ header copied so the file replays on its own; merge into config/paper_fleet.json with fleet_expand.sh merge.")
      + {strategies: $new}
  ' "$FLEET" > "$tmp"
  write_or_show "$tmp" "$STAGE/paper_winners.json" "winners"
}

# market code | feed | traded micro | $/pt | tick | point-knob ratio to NQ | reference feed
# Ratios: mean of the RTH-range and first-hour-range ratios to NQ over 09-28..30 (the only sessions
# all markets share); ES keeps the 0.144 ATR14 ratio its 09-28 mirror was built on (longer sample).
MARKETS='es ES MES 5.0 0.25 0.144 NQ
ym YM MYM 0.5 1.0 1.05 ES
rty RTY M2K 5.0 0.1 0.077 ES
gc GC MGC 10.0 0.1 0.11 ES
cl CL MCL 100.0 0.01 0.0055 ES'

cmd_markets() {
  while read -r code feed sym pv tick ratio ref; do
    local tmp existing="[]"; tmp=$(mktemp)
    [[ "$code" == es ]] && existing=$(jq -c '.strategies' config/paper_fleet_es.json)
    jq --argjson ov "$OVERLAYS" --argjson existing "$existing" --arg code "$code" --arg feed "$feed" \
       --arg sym "$sym" --argjson pv "$pv" --argjson tick "$tick" --argjson r "$ratio" --arg ref "$ref" \
       "$JQ_LIB"'
      # snap to the tick, clean float noise, and keep each knob a sane number of ticks wide: a scaled
      # stop inside the 3-tick paper slippage would measure the fill model, not the strategy
      def snap: (((. / $tick) | round) * $tick * 1000000 | round) / 1000000;
      def floor_ticks($k): if $k == "sl_points" then 8 elif $k == "trail_step" then 4 else 1 end;
      def scaled($k): if type == "number" and . > 0
                      then ((. * $r) | snap) as $v | ((floor_ticks($k) * $tick * 1000000 | round) / 1000000) as $min
                           | (if $v < $min then $min else $v end)
                      else . end;
      ["sl_points","trail_step","trail_be_trigger","trail_be_offset","breakout_buffer","max_entry_offset",
       "tp_points","trail_step_trend","trail_step_range","gap_min_pts","gf_min_pts"] as $PTS
      # the engine default exits come from config/tradeify_config.json at NQ scale: write them explicitly
      | def mirror_params:
          ((if .engine == "mtf_scalper" then {sl_points: 15, tp_points: 35}
            else {sl_points: 15, trail_step: 10, trail_be_trigger: 3, trail_be_offset: 1} end) + .params)
          | with_entries(.key as $k | if ($PTS | index($k)) then .value |= scaled($k) else . end);
      ($existing | map(.id)) as $have
      | [ .strategies[] | select(is_base) as $b
          | ($code + "_" + $b.id) as $mid
          | ($b | mirror_params) as $mp
          | ( {id: $mid, engine: $b.engine, enabled: true, params: ($mp + {mirror_of: $b.id})},
              ( ["inv","rg_nochase","rg_range"][] as $g | select($b | ov_ok($g))
                | {id: ($mid + "__" + $g), engine: $b.engine, enabled: true,
                   params: ($mp + $ov[$g].p + {mirror_of: $b.id, base_id: $mid, overlay: $g})} ) ) ]
      | map(select(.id as $i | $have | index($i) | not)) as $new
      | {_comment: ("STAGED 2026-10-01 — every enabled NQ base mirrored to " + $feed + " traded as " + $sym
                    + " ($" + ($pv|tostring) + "/pt, tick " + ($tick|tostring) + "); point knobs x " + ($r|tostring)
                    + " (range ratio to NQ) snapped to the tick, floors sl 8 / trail 4 / other 1 tick; + __inv / __rg_nochase / __rg_range. ids prefixed "
                    + $code + "_ (strategy_id is global); mirror_of = the NQ base. fleet_expand.sh markets."),
         account_label: $code, symbol: $sym, feed_symbol: $feed, reference_symbol: $ref, exchange: (if $feed == "GC" then "COMEX" elif $feed == "CL" then "NYMEX" elif $feed == "YM" then "CBOT" else "CME" end),
         point_value: $pv, tick_size: $tick, starting_balance: 25000.0, daily_loss_limit: 0, trailing_drawdown_cap: 0,
         consistency_cap_pct: 0.3, commission_rt: 1.82, slippage_ticks: 3, poll_ms: 100, feed_gap_reset_secs: 300,
         strategy_daily_loss_limit: -250.0,
         strategies: ($existing + $new)}
    ' "$FLEET" > "$tmp"
    write_or_show "$tmp" "$STAGE/paper_fleet_$code.json" "market $code ($feed as $sym)"
  done <<< "$MARKETS"
}

cmd_combos() {
  local tmp; tmp=$(mktemp)
  jq --argjson ov "$OVERLAYS" "$JQ_LIB"'
    ($ov | keys) as $names
    | [ $names[] as $a | $names[] as $b | select($a < $b and $ov[$a].cat != $ov[$b].cat) | [$a,$b] ] as $pairs
    | [ .strategies[] | select(is_base) as $s
        | $pairs[] as [$a,$b] | select(($s | ov_ok($a)) and ($s | ov_ok($b)))
        | ($a + "+" + $b) as $tag
        | {id: ($s.id + "__" + $tag), engine: $s.engine, enabled: true,
           params: ($s.params + $ov[$a].p + $ov[$b].p + {base_id: $s.id, overlay: $tag})} ] as $new
    | (del(.strategies) | ._comment = "STAGED 2026-10-01 — every pair of filters from DIFFERENT categories (book inv/imb/sg x regime rg_* x volume rvol15 x exit tr25) on every enabled NQ base; own engine + label nq_combo so ~12k rows stay off the main fleet board. fleet_expand.sh combos."
       | .account_label = "nq_combo") + {strategies: $new}
  ' "$FLEET" > "$tmp"
  write_or_show "$tmp" "$STAGE/paper_fleet_combo.json" "combos"
}

# New trend modes (2026-10-01, trend_strategy.hpp): level_fade, vwap_reclaim, range_break. Exits use the
# fib research's best (trail 15, break-even at 8 — BE 3 was the worst exit in every combination).
cmd_newmodes() {
  local tmp; tmp=$(mktemp)
  jq --argjson ov "$OVERLAYS" "$JQ_LIB"'
    . as $root
    | {trail_step: 15, trail_be_trigger: 8} as $X
    | [ ( ["prior_day","overnight"][] as $lv | [0.5,1.0,0][] as $pk | [1,5][] as $tf
          | [{w:"am",s:930,e:1130},{w:"day",s:930,e:1555}][] as $win
          | {id: "lvl_fade_\(if $lv == "prior_day" then "pd" else "on" end)_p\($pk|tostring|gsub("\\.";""))_\($tf)m_\($win.w)",
             engine: "trend", enabled: true,
             params: ($X + {mode: "level_fade", level: $lv, lf_max_poke_atr: $pk, tf_min: $tf, win_start: $win.s, win_end: $win.e})} ),
        ( [5,10,20][] as $n | [1,3,5][] as $tf | [{w:"am",s:930,e:1200},{w:"day",s:930,e:1555}][] as $win
          | {id: "vwap_reclaim_\($n)_\($tf)m_\($win.w)", engine: "trend", enabled: true,
             params: ($X + {mode: "vwap_reclaim", vr_min_bars: $n, tf_min: $tf, win_start: $win.s, win_end: $win.e})} ),
        ( [ {r:"asia_ldn", f:2000, t:200,  s:200,  e:500,  sess:"globex"},
            {r:"asia_us",  f:2000, t:200,  s:930,  e:1100, sess:"rth"},
            {r:"on_us",    f:1800, t:930,  s:930,  e:1100, sess:"rth"},
            {r:"ldn_us",   f:200,  t:500,  s:930,  e:1100, sess:"rth"},
            {r:"open30",   f:930,  t:1000, s:1000, e:1200, sess:"rth"} ][] as $R
          | [0, 0.25][] as $buf | [1,5][] as $tf
          | {id: "range_break_\($R.r)_b\($buf|tostring|gsub("\\.";""))_\($tf)m", engine: "trend", enabled: true,
             params: ($X + {mode: "range_break", rb_from: $R.f, rb_to: $R.t, rb_buffer_atr: $buf, tf_min: $tf,
                             win_start: $R.s, win_end: $R.e, session: $R.sess})} )
      ] as $bases
    | ($bases + [ $bases[] | . as $b | {id: ($b.id + "__inv"), engine: "trend", enabled: true,
                                         params: ($b.params + $ov.inv.p + {base_id: $b.id, overlay: "inv"})} ]) as $all
    | (.strategies | map(.id)) as $have
    | ($root | del(.strategies) | ._comment = "STAGED 2026-10-01 — the three new trend modes (level_fade, vwap_reclaim, range_break) on a small grid, each with its __inv (book fade) sibling. NQ header so it replays on its own. fleet_expand.sh newmodes.")
      + {strategies: [ $all[] | select(.id as $i | $have | index($i) | not) ]}
  ' "$FLEET" > "$tmp"
  write_or_show "$tmp" "$STAGE/paper_newmodes.json" "newmodes"
}

# Replay every staged file over the recorded sessions of its feed, one full Globex day at a time
# (D-1 18:00 → D 17:00 ET, so Asia / London strategies are judged too), under a research label
# exp_<name> — never a live label. Refuses to run in regular hours on a weekday: replays read the tick
# table hard, and the live executor polls that same table for its feed (pg_feed_stale halts 09-30).
in_rth_now() {   # weekday 09:25-16:05 ET
  local now dow; now=$(TZ=America/New_York date +%H%M); dow=$(TZ=America/New_York date +%u)
  (( dow <= 5 && 10#$now >= 925 && 10#$now < 1605 ))
}
in_asia_now() {  # 23:55-02:35 ET — the Asia dry-run executor (tradeify_mtfasia) reads the same tick table
  local now; now=$(TZ=America/New_York date +%H%M)
  (( 10#$now >= 2355 || 10#$now < 235 ))
}
cmd_replay() {
  local force=0; [[ "${2:-}" == "--force" ]] && force=1
  if (( !force )) && in_rth_now; then
    echo "refusing: $(TZ=America/New_York date +%H:%M) ET is inside regular hours — the live executor reads the same tick table (use --force to override)"; return 1
  fi
  set -a; . ./.env; set +a
  local log=data/logs/fleet_expand_replay.log f name feed label d
  mkdir -p data/logs
  for f in "$STAGE"/paper_winners.json "$STAGE"/paper_newmodes.json "$STAGE"/paper_fleet_*.json; do
    [[ -f "$f" ]] || continue
    name=$(basename "$f" .json); name=${name#paper_}; name=${name#fleet_}
    label="exp_$name"
    feed=$(jq -r '.feed_symbol // "NQ"' "$f")
    # replay trades of unregistered ids are dropped by the FK — refuse rather than produce an empty label
    local staged registered
    staged=$(jq '.strategies|length' "$f")
    local ids; ids=$(mktemp); jq -r '.strategies[].id' "$f" > "$ids"
    registered=$(printf '%s\n' "CREATE TEMP TABLE i(id text);" "\\copy i FROM '$ids'" \
                 "SELECT count(*) FROM i JOIN paper_strategies p ON p.strategy_id=i.id;" |
                 PGPASSWORD="$PG_PASSWORD" psql -X -At -q -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" -v ON_ERROR_STOP=1 | tail -1)
    rm -f "$ids"; registered=${registered:-0}
    if (( registered < staged )); then echo "$f: only $registered of $staged ids registered — run: $0 register"; return 1; fi
    for d in $(PGPASSWORD="$PG_PASSWORD" psql -X -At -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" \
                 -c "select session_date from session_stats where symbol='$feed' and session_date < current_date order by 1"); do
      if (( !force )) && in_rth_now; then          # checked before EVERY day: a long run must not drift into RTH
        echo "$(date -Is) stopped at $label $d — regular hours began; rerun after 16:05 ET (finished days are kept)" | tee -a "$log"; return 1
      fi
      if (( !force )) && in_asia_now; then         # pause (not stop) through the Asia session, then carry on
        echo "$(date -Is) pausing before $label $d — Asia session (23:55-02:35 ET) in progress" | tee -a "$log"
        while in_asia_now; do sleep 60; done
        echo "$(date -Is) resuming" | tee -a "$log"
      fi
      local from to; from="$(date -d "$d -1 day" +%F) 18:00"; to="$d 17:00"
      # idempotent: a rerun replaces this label's rows for the day instead of doubling them
      PGPASSWORD="$PG_PASSWORD" psql -X -q -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" -c \
        "DELETE FROM paper_trades WHERE account_label='$label' AND entry_time >= ('$from'::timestamp AT TIME ZONE 'America/New_York') AND entry_time < ('$to'::timestamp AT TIME ZONE 'America/New_York')"
      local t0=$SECONDS
      nice -n 15 ionice -c 3 ./build-exp/paper_engine --config "$f" --account-label "$label" \
        --replay-from "$from" --replay-to "$to" >> "$log.$label" 2>&1
      echo "$(date -Is) $label $feed $d rc=$? $((SECONDS - t0))s" | tee -a "$log"
    done
  done
  echo "$(date -Is) DONE" | tee -a "$log"
}

cmd_register() {
  set -a; . ./.env; set +a
  local f n tsv
  for f in "$STAGE"/*.json; do
    n=$(jq '.strategies|length' "$f")
    if (( DRY )); then echo "register $f: $n ids (dry run)"; continue; fi
    tsv=$(mktemp)
    jq -r '.strategies[] | [.id, .engine, (.params|tojson), (.enabled|tostring)] | @tsv' "$f" > "$tsv"
    if ! printf '%s\n' \
        "CREATE TEMP TABLE s(id text, engine text, params jsonb, enabled boolean);" \
        "\\copy s FROM '$tsv'" \
        "INSERT INTO paper_strategies (strategy_id, account_label, engine, params_json, enabled) SELECT id, 'staging', engine, params, enabled FROM s ON CONFLICT (strategy_id) DO NOTHING;" |
        PGPASSWORD="$PG_PASSWORD" psql -X -q -h "$PG_HOST" -p "${PG_PORT:-5432}" -U "$PG_USER" -d "$PG_DB" -v ON_ERROR_STOP=1; then
      rm -f "$tsv"; echo "register FAILED for $f"; return 1
    fi
    rm -f "$tsv"; echo "registered $f: $n ids (existing ids untouched)"
  done
}

cmd_merge() {
  local adds=() f tmp
  for f in "$STAGE/paper_winners.json" "$STAGE/paper_newmodes.json"; do [[ -f "$f" ]] && adds+=("$f"); done
  (( ${#adds[@]} )) || { echo "nothing staged for the NQ fleet"; return; }
  tmp=$(mktemp)
  jq -s '.[0] as $f | ([.[1:][].strategies[]]) as $add | ($f.strategies | map(.id)) as $have
         | $f | .strategies += [$add[] | select(.id as $i | $have | index($i) | not)]' "$FLEET" "${adds[@]}" > "$tmp"
  echo "fleet: $(jq '.strategies|length' "$FLEET") -> $(jq '.strategies|length' "$tmp")"
  if (( DRY )); then rm -f "$tmp"; echo "(dry run — nothing written)"; return; fi
  local bk=data/backups/fleet; mkdir -p "$bk"; cp "$FLEET" "$bk/paper_fleet.json.bak-$(date +%Y%m%d-%H%M%S)"
  mv "$tmp" "$FLEET"; echo "written: $FLEET (backup in $bk/) — restart paper-engine-local to load it"
}

# Put the staged fleets into service (run after the current load is proven — e.g. the collector log has
# no "Writer queue full" since the last reload). nq = merge winners + newmodes into the NQ fleet and
# restart paper-engine-local; markets / combo = copy each staged paper_fleet_<code>.json into config/
# and run it in its own unit paper-engine-local-<code> (deploy/ file generated from the ES unit).
# Refuses during weekday regular hours: a restart warms up for minutes and must not overlap live trading.
cmd_activate() {
  local what="${2:-all}"
  # it restarts live paper engines: never by accident (2026-10-01 a test run of this command merged the
  # staged NQ variants a day early) — the operator must say --yes
  [[ "${3:-}" == "--yes" ]] || { echo "activate restarts paper engines — rerun as: $0 activate $what --yes"; return 1; }
  if in_rth_now; then echo "refusing: inside regular hours — activate after 16:05 ET or before 09:25 ET"; return 1; fi
  if [[ "$what" == nq || "$what" == all ]]; then
    cmd_merge && systemctl --user restart paper-engine-local && echo "paper-engine-local restarted with $(jq '.strategies|length' "$FLEET") strategies"
  fi
  local f code unit dest
  for f in "$STAGE"/paper_fleet_*.json; do
    [[ -f "$f" ]] || continue
    code=$(basename "$f" .json); code=${code#paper_fleet_}
    [[ "$what" == all || "$what" == "$code" || ( "$what" == markets && "$code" != combo ) ]] || continue
    dest=config/paper_fleet_$code.json; unit=paper-engine-local-$code
    if [[ -f "$dest" ]]; then mkdir -p data/backups/fleet; cp "$dest" "data/backups/fleet/paper_fleet_$code.json.bak-$(date +%Y%m%d-%H%M%S)"; fi
    cp "$f" "$dest"
    if [[ ! -f "deploy/$unit.service" ]]; then
      sed -e "s#^Description=.*#Description=Paper fleet $code ($dest, label $(jq -r .account_label "$f")) on the collector's Postgres ticks#" \
          -e "s#config/paper_fleet_es.json#$dest#g" -e "s#paper_engine_es.log#paper_engine_$code.log#g" \
          deploy/paper-engine-local-es.service > "deploy/$unit.service"
    fi
    cp "deploy/$unit.service" "$HOME/.config/systemd/user/$unit.service"
    systemctl --user daemon-reload
    systemctl --user enable "$unit" >/dev/null 2>&1
    systemctl --user restart "$unit" && echo "$unit running $dest ($(jq '.strategies|length' "$dest") strategies)"
  done
}

case "$CMD" in
  winners) cmd_winners ;;
  markets) cmd_markets ;;
  combos)  cmd_combos ;;
  register) cmd_register ;;
  merge)   cmd_merge ;;
  newmodes) cmd_newmodes ;;
  replay)  cmd_replay "$@" ;;
  activate) cmd_activate "$@" ;;
  all)     cmd_winners; cmd_markets; cmd_combos; cmd_newmodes ;;
  *) sed -n '2,24p' "$0"; exit 2 ;;
esac
