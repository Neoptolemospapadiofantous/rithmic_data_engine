#!/usr/bin/env bash
# slot2_eval.sh — paper test of a SECOND strategy for the 10:30-12:00 ET slot after the ORB.
#
# Founder 2026-10-04: keep the ORB as is; find the best bounce / trend / momentum strategy for
# the window after it where volume and volatility still give consistent trades. The candidates
# (config/slot2_fleet.json, ids s2_*) are windowed copies of the best live-runnable variants per
# family on the forward test up to 2026-10-02. Because they were SELECTED on those sessions, the
# verdict uses only sessions after SELECTED_THROUGH; earlier sessions are shown as in-sample.
#
#   scripts/slot2_eval.sh            replay every recorded session not yet replayed, then report
#   scripts/slot2_eval.sh report     report only
#   scripts/slot2_eval.sh --force    allow running during regular hours (not recommended)
#
# Replays read the same ticks table the live executor polls, so they refuse 09:25-16:05 ET on
# weekdays. Output: data/slot2/report.md + report.json; label slot2 in paper_trades. Paper only —
# nothing here touches a live config. The first time a candidate passes, one grid-notify alert.
set -uo pipefail
cd "$(dirname "$0")/.."

CFG=config/slot2_fleet.json
LABEL=slot2
SELECTED_THROUGH=2026-10-02          # candidates were picked on sessions up to this date
OUT=data/slot2
DONE=$OUT/replayed.txt
LOG=data/logs/slot2_eval.log
COMMISSION_RT=$(jq -r '.commission_rt' config/tradeify_config.json)   # live NQ figure
# Pass criteria (forward sessions only) — fixed before any forward data exists.
MIN_TRADES=20 MIN_TRADE_DAYS=8 MIN_PF=1.3 MAX_SCRATCH=0.5 MAX_DAY_LOSS=-500

mkdir -p "$OUT" data/logs
touch "$DONE"
set -a; . ./.env; set +a
export PGPASSWORD="$PG_PASSWORD"
psqlq() { psql -X -q -h "$PG_HOST" -p "${PG_PORT:-5432}" -U "$PG_USER" -d "$PG_DB" -v ON_ERROR_STOP=1 "$@"; }

force=0 mode=run
for a in "$@"; do
  case "$a" in
    --force) force=1 ;;
    report)  mode=report ;;
    *) echo "usage: $0 [report] [--force]"; exit 2 ;;
  esac
done

in_rth_now() {
  local dow hm; dow=$(TZ=America/New_York date +%u); hm=$(TZ=America/New_York date +%H%M)
  (( dow <= 5 )) && (( 10#$hm >= 925 && 10#$hm < 1605 ))
}

register() {   # paper_trades rows of unregistered ids are dropped by the FK
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
  local d from to t0 rc before after
  # a session is complete once its 16:05 ET has passed: every earlier date, and today after 16:05
  local last
  if (( 10#$(TZ=America/New_York date +%H%M) >= 1605 )); then last=$(TZ=America/New_York date +%F)
  else last=$(TZ=America/New_York date -d yesterday +%F); fi
  for d in $(psqlq -At -c "SELECT session_date FROM session_stats WHERE symbol='NQ' AND session_date <= '$last'
                            AND extract(isodow FROM session_date) < 6 ORDER BY 1"); do
    grep -qx "$d" "$DONE" && continue
    if (( !force )) && in_rth_now; then
      echo "$(date -Is) stopped before $d — regular hours began" | tee -a "$LOG"; return 1
    fi
    # from the prior evening so indicators and the mtf warm-up have history before 10:30
    from="$(date -d "$d -1 day" +%F) 18:00"; to="$d 12:30"
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
  # Per candidate: in-sample (<= SELECTED_THROUGH) vs forward (after). NQ $ = points x $20 per
  # contract minus the live round-trip commission; paper fills already carry 3-tick slippage.
  psqlq -At -c "
  WITH t AS (
    SELECT p.strategy_id, s.params_json->>'family' AS family,
           (p.entry_time AT TIME ZONE 'America/New_York')::date AS d,
           p.pnl_pts * 20.0 - $COMMISSION_RT AS usd, p.pnl_pts, p.exit_reason
    FROM paper_trades p JOIN paper_strategies s USING (strategy_id)
    WHERE p.account_label = '$LABEL' AND p.exit_time IS NOT NULL
      AND (p.entry_time AT TIME ZONE 'America/New_York')::time >= '10:30'
      AND (p.entry_time AT TIME ZONE 'America/New_York')::time <  '12:00'),
  seg AS (SELECT *, CASE WHEN d <= '$SELECTED_THROUGH' THEN 'in_sample' ELSE 'forward' END AS part FROM t),
  days AS (SELECT strategy_id, part, d, sum(usd) AS day_usd FROM seg GROUP BY 1,2,3),
  mid AS (SELECT strategy_id, part, percentile_disc(0.5) WITHIN GROUP (ORDER BY d) AS d_mid FROM days GROUP BY 1,2),
  agg AS (
    SELECT s.strategy_id, s.family, s.part, count(*) AS trades, count(DISTINCT s.d) AS trade_days,
           round(sum(s.usd)::numeric, 0) AS net_usd,
           round((sum(s.usd) FILTER (WHERE s.usd > 0) / NULLIF(-sum(s.usd) FILTER (WHERE s.usd < 0), 0))::numeric, 2) AS pf,
           round(avg((s.usd > 0)::int)::numeric, 2) AS win_rate,
           round(avg((s.exit_reason = 'breakeven')::int)::numeric, 2) AS scratch_rate,
           ((sum(s.usd) FILTER (WHERE s.d <= m.d_mid)) > 0)::int + ((sum(s.usd) FILTER (WHERE s.d > m.d_mid)) > 0)::int AS positive_halves
    FROM seg s JOIN mid m USING (strategy_id, part) GROUP BY 1,2,3),
  worst AS (SELECT strategy_id, part, round(min(day_usd)::numeric, 0) AS worst_day FROM days GROUP BY 1,2)
  SELECT json_agg(row_to_json(x) ORDER BY x.part, x.net_usd DESC) FROM (
    SELECT a.*, w.worst_day,
           (a.part = 'forward' AND a.trades >= $MIN_TRADES AND a.trade_days >= $MIN_TRADE_DAYS
            AND COALESCE(a.pf, 99) >= $MIN_PF AND a.net_usd > 0 AND a.positive_halves = 2
            AND a.scratch_rate <= $MAX_SCRATCH AND w.worst_day >= $MAX_DAY_LOSS) AS pass
    FROM agg a JOIN worst w USING (strategy_id, part)) x" > "$OUT/rows.json"

  local fwd_days; fwd_days=$(psqlq -At -c "SELECT count(*) FROM session_stats WHERE symbol='NQ'
      AND session_date > '$SELECTED_THROUGH' AND extract(isodow FROM session_date) < 6")
  jq -n --slurpfile rows "$OUT/rows.json" --arg sel "$SELECTED_THROUGH" --argjson fd "${fwd_days:-0}" \
     --arg gen "$(date -Is)" --argjson c "{\"min_trades\":$MIN_TRADES,\"min_trade_days\":$MIN_TRADE_DAYS,\"min_pf\":$MIN_PF,\"max_scratch\":$MAX_SCRATCH,\"max_day_loss\":$MAX_DAY_LOSS}" \
     '{generated:$gen, selected_through:$sel, forward_sessions:$fd, criteria:$c, rows:($rows[0] // [])}' > "$OUT/report.json"

  {
    echo "# Slot 2 (10:30-12:00 ET) — second-strategy paper test"
    echo
    echo "Generated $(date '+%F %H:%M %Z'). Candidates were selected on sessions up to $SELECTED_THROUGH; only later sessions count toward a verdict. Forward sessions so far: ${fwd_days:-0}."
    echo
    echo "Pass (forward only): >= $MIN_TRADES trades on >= $MIN_TRADE_DAYS days, PF >= $MIN_PF, net > 0, both halves positive, breakeven scratches <= ${MAX_SCRATCH}, worst day >= \$$MAX_DAY_LOSS. NQ \$ = points x \$20 per contract - \$$COMMISSION_RT commission; fills include 3-tick slippage."
    for part in forward in_sample; do
      echo
      [[ $part == forward ]] && echo "## Forward (decides)" || echo "## In-sample (selection period — for reference only)"
      echo
      echo "| strategy | family | trades | days | net NQ \$ | PF | win | scratch | halves+ | worst day | verdict |"
      echo "|---|---|---|---|---|---|---|---|---|---|---|"
      jq -r --arg p "$part" '.rows[] | select(.part==$p) |
        "| \(.strategy_id) | \(.family) | \(.trades) | \(.trade_days) | \(.net_usd) | \(.pf // "∞") | \(.win_rate) | \(.scratch_rate) | \(.positive_halves)/2 | \(.worst_day) | \(if .part=="forward" then (if .pass then "PASS" else "—" end) else "" end) |"' "$OUT/report.json"
    done
  } > "$OUT/report.md"

  local passed; passed=$(jq -r '[.rows[] | select(.pass) | .strategy_id] | join(", ")' "$OUT/report.json")
  if [[ -n "$passed" && ! -f "$OUT/.alerted" ]]; then
    local gn; gn=$(command -v grid-notify || echo "$HOME/.local/bin/grid-notify")
    [[ -x "$gn" ]] && "$gn" "Slot-2 paper test: PASS for $passed (10:30-12:00 ET). Review data/slot2/report.md before any dry run." || true
    echo "$passed" > "$OUT/.alerted"
  fi
  echo "report: $OUT/report.md (forward sessions: ${fwd_days:-0}; passing: ${passed:-none})"
}

if [[ $mode == run ]]; then
  register
  replay_pending
fi
report
