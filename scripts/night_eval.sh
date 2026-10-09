#!/usr/bin/env bash
# night_eval.sh — overnight (Asia + London) combination grid: replay + stats report.
#
# Founder 2026-10-04: trade momentum / trend on Asia and London when the book is not thin, and
# "create as many combinations as possible to check the stats". config/night_fleet.json holds
# ~950 nt_* variants: trend-engine momentum/trend/bounce bases, mtf scalpers and session ORBs in
# Tokyo 20:00-22:00, London 02:00-05:00 and London-late 07:00-09:15 ET, x stop sets x the
# liquidity gate (liq_vol_min / liq_impact_max, paper_quote.hpp RegimeState).
#
#   scripts/night_eval.sh            replay every completed overnight session not yet replayed, then report
#   scripts/night_eval.sh report     report only
#   scripts/night_eval.sh --force    allow running inside regular hours or the Asia session
#
# A session = the CME day ending at 17:00 ET on date d; its overnight part is replayed from
# d-1 12:00 ET (indicator history) to d 09:20 ET. Replays read the live tick table, so they
# refuse during RTH (09:25-16:05 ET weekdays) and the Asia session (18:00-03:00 ET). Paper
# research only: label `night`, nothing in the live paper fleet or any live config.
set -uo pipefail
cd "$(dirname "$0")/.."

# Defaults = the overnight grid. NIGHT_CFG / NIGHT_LABEL / NIGHT_OUT run another window grid the same way
# (2026-10-08: config/gap_fleet.json, label gap — the windows no signal strategy covers).
CFG=${NIGHT_CFG:-config/night_fleet.json}
LABEL=${NIGHT_LABEL:-night}
OUT=${NIGHT_OUT:-data/night}
DONE=$OUT/replayed.txt
LOG=data/logs/${LABEL}_eval.log
[[ $LABEL == night ]] && LOG=data/logs/night_eval.log
COMMISSION_RT=$(jq -r '.commission_rt' config/tradeify_config.json)
# A combination counts as consistent with >= 15 trades on >= 6 sessions, PF >= 1.3, net > 0,
# both halves positive, breakeven scratches <= 0.5 and no day worse than -$500 (NQ $).
MIN_TRADES=15 MIN_DAYS=6 MIN_PF=1.3 MAX_SCRATCH=0.5 MAX_DAY_LOSS=-500

mkdir -p "$OUT" data/logs
touch "$DONE"
set -a; . ./.env; set +a
export PGPASSWORD="$PG_PASSWORD"
psqlq() { psql -X -q -h "$PG_HOST" -p "${PG_PORT:-5432}" -U "$PG_USER" -d "$PG_DB" -v ON_ERROR_STOP=1 "$@"; }

force=0 mode=run
for a in "$@"; do
  case "$a" in --force) force=1 ;; report) mode=report ;; *) echo "usage: $0 [report] [--force]"; exit 2 ;; esac
done

busy_now() {   # weekday RTH, or the Asia session on any evening the market is open
  local dow hm; dow=$(TZ=America/New_York date +%u); hm=$((10#$(TZ=America/New_York date +%H%M)))
  if (( dow <= 5 )) && (( hm >= 925 && hm < 1605 )); then echo rth; return 0; fi
  if (( (hm >= 1800 && dow != 5 && dow != 6) || (hm < 300 && dow != 6 && dow != 7) )); then echo asia; return 0; fi
  return 1
}

register() {
  local tsv; tsv=$(mktemp)
  jq -r '.strategies[] | [.id, .engine, (.params|tojson), "true"] | @tsv' "$CFG" > "$tsv"
  printf '%s\n' "CREATE TEMP TABLE s(id text, engine text, params jsonb, enabled boolean);" \
    "\\copy s FROM '$tsv'" \
    "INSERT INTO paper_strategies (strategy_id, account_label, engine, params_json, enabled)
       SELECT id, 'staging', engine, params, enabled FROM s
     ON CONFLICT (strategy_id) DO UPDATE SET params_json = EXCLUDED.params_json, engine = EXCLUDED.engine;" | psqlq
  rm -f "$tsv"
}

replay_pending() {
  local last d from to t0 rc before after why
  # an overnight session is complete once d 09:20 ET has passed
  if (( 10#$(TZ=America/New_York date +%H%M) >= 920 )); then last=$(TZ=America/New_York date +%F)
  else last=$(TZ=America/New_York date -d yesterday +%F); fi
  for d in $(psqlq -At -c "SELECT session_date FROM session_stats WHERE symbol='NQ' AND session_date <= '$last'
                            AND session_date >= '2026-09-22' AND extract(isodow FROM session_date) < 6 ORDER BY 1"); do
    grep -qx "$d" "$DONE" && continue
    if (( !force )) && why=$(busy_now); then
      echo "$(date -Is) stopped before $d — $why in progress; rerun later (finished days are kept)" | tee -a "$LOG"; return 1
    fi
    from="$(date -d "$d -1 day" +%F) 12:00"; to="$d 09:20"
    psqlq -c "DELETE FROM paper_trades WHERE account_label='$LABEL'
              AND entry_time >= ('$from'::timestamp AT TIME ZONE 'America/New_York')
              AND entry_time <  ('$to'::timestamp AT TIME ZONE 'America/New_York')"
    before=$(grep -c 'Writer queue full' data/logs/collector.log 2>/dev/null || true)
    t0=$SECONDS
    nice -n 15 ionice -c 3 ./build/paper_engine --config "$CFG" --account-label "$LABEL" \
      --replay-from "$from" --replay-to "$to" > "$OUT/replay_$d.log" 2>&1
    rc=$?
    after=$(grep -c 'Writer queue full' data/logs/collector.log 2>/dev/null || true)
    echo "$(date -Is) $d rc=$rc $((SECONDS - t0))s collector_drops=$(( ${after:-0} - ${before:-0} ))" | tee -a "$LOG"
    (( rc == 0 )) && echo "$d" >> "$DONE"
  done
}

report() {
  # Per combination, then aggregated by family x window x gate. Session key = the CME day the
  # trade belongs to (entry + 6 h, so a 20:00 Tokyo trade counts toward the next date).
  psqlq -At -c "
  CREATE TEMP TABLE nt AS
  SELECT p.strategy_id, s.engine, s.params_json->>'family' AS family, s.params_json->>'night_window' AS win,
         CASE WHEN s.params_json ? 'liq_vol_min' THEN 'gate ' || (s.params_json->>'liq_vol_min') || '/' || (s.params_json->>'liq_impact_max')
              ELSE 'no gate' END AS gate,
         ((p.entry_time AT TIME ZONE 'America/New_York') + interval '6 hours')::date AS d,
         p.pnl_pts * 20.0 - $COMMISSION_RT AS usd, p.exit_reason
  FROM paper_trades p JOIN paper_strategies s USING (strategy_id)
  WHERE p.account_label = '$LABEL' AND p.exit_time IS NOT NULL;
  CREATE TEMP TABLE per AS
  WITH days AS (SELECT strategy_id, d, sum(usd) AS day_usd FROM nt GROUP BY 1,2),
       mid AS (SELECT strategy_id, percentile_disc(0.5) WITHIN GROUP (ORDER BY d) AS d_mid FROM days GROUP BY 1)
  SELECT n.strategy_id, min(n.engine) engine, min(n.family) family, min(n.win) win, min(n.gate) gate,
         count(*) trades, count(DISTINCT n.d) days, sum(n.usd) net,
         sum(n.usd) FILTER (WHERE n.usd > 0) / NULLIF(-sum(n.usd) FILTER (WHERE n.usd < 0), 0) pf,
         avg((n.exit_reason = 'breakeven')::int) scratch,
         ((sum(n.usd) FILTER (WHERE n.d <= m.d_mid)) > 0)::int + ((sum(n.usd) FILTER (WHERE n.d > m.d_mid)) > 0)::int halves,
         (SELECT min(day_usd) FROM days x WHERE x.strategy_id = n.strategy_id) worst_day
  FROM nt n JOIN mid m USING (strategy_id) GROUP BY n.strategy_id, m.d_mid;
  SELECT json_build_object(
    'groups', (SELECT json_agg(g ORDER BY g.win, g.family, g.gate) FROM (
       SELECT win, family, gate, count(*) combos, sum(trades) trades,
              round(100.0 * avg((net > 0)::int)) pct_profitable,
              round(percentile_cont(0.5) WITHIN GROUP (ORDER BY net)::numeric) median_net,
              round(avg(net)::numeric) avg_net,
              count(*) FILTER (WHERE trades >= $MIN_TRADES AND days >= $MIN_DAYS AND COALESCE(pf, 99) >= $MIN_PF AND net > 0
                               AND halves = 2 AND scratch <= $MAX_SCRATCH AND worst_day >= $MAX_DAY_LOSS) consistent
       FROM per GROUP BY 1,2,3) g),
    'top', (SELECT json_agg(t ORDER BY t.net DESC) FROM (
       SELECT strategy_id, engine, family, win, gate, trades, days, round(net::numeric) net, round(pf::numeric, 2) pf,
              round(scratch::numeric, 2) scratch, halves, round(worst_day::numeric) worst_day
       FROM per WHERE trades >= $MIN_TRADES AND days >= $MIN_DAYS AND COALESCE(pf, 99) >= $MIN_PF AND net > 0
         AND halves = 2 AND scratch <= $MAX_SCRATCH AND worst_day >= $MAX_DAY_LOSS
       ORDER BY net DESC LIMIT 40) t),
    'sessions', (SELECT count(DISTINCT d) FROM nt),
    'combos_traded', (SELECT count(*) FROM per))" > "$OUT/report.json"

  {
    echo "# ${LABEL} grid ($CFG) — combination stats"
    echo
    echo "Generated $(date '+%F %H:%M %Z'). Sessions: $(jq .sessions "$OUT/report.json"); combinations with trades: $(jq .combos_traded "$OUT/report.json") of $(jq '.strategies|length' "$CFG"). NQ \$ = points x \$20 - \$$COMMISSION_RT per trade; fills carry 3-tick slippage."
    echo
    echo "Consistent = >= $MIN_TRADES trades on >= $MIN_DAYS sessions, PF >= $MIN_PF, net > 0, both halves positive, scratches <= $MAX_SCRATCH, worst day >= \$$MAX_DAY_LOSS. With ~950 combinations some will pass by luck: read the GROUP rows first, and treat a lone passer as a lead for the forward test, not a result."
    echo
    echo "## By window x family x liquidity gate"
    echo
    echo "| window | family | gate | combos | trades | profitable | median net \$ | avg net \$ | consistent |"
    echo "|---|---|---|---|---|---|---|---|---|"
    jq -r '.groups[]? | "| \(.win) | \(.family) | \(.gate) | \(.combos) | \(.trades) | \(.pct_profitable)% | \(.median_net) | \(.avg_net) | \(.consistent) |"' "$OUT/report.json"
    echo
    echo "## Consistent combinations (top 40 by net)"
    echo
    echo "| strategy | family | window | gate | trades | sessions | net \$ | PF | scratch | worst day |"
    echo "|---|---|---|---|---|---|---|---|---|---|"
    jq -r '.top[]? | "| \(.strategy_id) | \(.family) | \(.win) | \(.gate) | \(.trades) | \(.days) | \(.net) | \(.pf // "∞") | \(.scratch) | \(.worst_day) |"' "$OUT/report.json"
  } > "$OUT/report.md"
  echo "report: $OUT/report.md (sessions: $(jq .sessions "$OUT/report.json"); consistent: $(jq '.top|length' "$OUT/report.json"))"
}

if [[ $mode == run ]]; then register; replay_pending; fi
report
