#!/usr/bin/env bash
# research_eval.sh — replay a research grid over every recorded session and report its stats.
#
#   scripts/research_eval.sh <config.json> <label>            replay unreplayed sessions, then report
#   scripts/research_eval.sh <config.json> <label> report     report only
#   add --force to run inside RTH (09:25-16:05 ET weekdays) or the Asia session (18:00-03:00 ET)
#
# Generalises night_eval.sh (2026-10-07, founder: dedicated trend/momentum strategy research). Paper
# research only: rows land in paper_trades under <label>; nothing in the live fleet or any live config.
#
# Warm-up and de-duplication (why each session is replayed this way): indicators (EMA 50 on 5m bars,
# ATR, efficiency ratio) need history before a session's first entry, so session d (the CME day that
# opens at 18:00 ET the evening before) is replayed from the PREVIOUS WEEKDAY's 09:00 ET (Friday for a
# Monday) to d 16:05 ET. That run also produces earlier entries — warm-up artefacts that would duplicate
# the previous session's own rows — so every run writes to a TEMPORARY label <label>_tmp; afterwards only
# the entries in [prev-weekday 18:00, d 16:05) ET (Asia evening + London + RTH of session d) are moved to
# <label> and the rest of the temporary rows are deleted. The kept ranges of consecutive sessions do not
# overlap; a rerun of d first deletes <label>'s rows in that range (idempotent).
# Replays read the live tick table, hence the RTH / Asia refusals.
#
# Report: data/research/<label>/report.md + report.json — overall, by every params key that varies
# across the grid (median net NQ $ per variant, % profitable, trades), and the top 25 that clear the
# consistency bar. NQ $ = pnl_pts x $20 - $1.82 per trade (1 contract; fills carry 3-tick slippage).
set -uo pipefail
cd "$(dirname "$0")/.."

CFG="${1:-}"; LABEL="${2:-}"
[[ -f "$CFG" && -n "$LABEL" ]] || { echo "usage: $0 <config.json> <label> [report] [--force]"; exit 2; }
shift 2
force=0 mode=run
for a in "$@"; do case "$a" in --force) force=1 ;; report) mode=report ;; *) echo "unknown arg $a"; exit 2 ;; esac; done

OUT="data/research/$LABEL"; DONE="$OUT/replayed.txt"; LOG="data/logs/research_${LABEL}.log"; TMP="${LABEL}_tmp"
COMMISSION_RT=$(jq -r '.commission_rt' config/tradeify_config.json)
# reference move the founder pointed at (2026-10-07: clean 5m London downtrend NQ 31426 -> 31249)
REF_FROM="${REF_FROM:-2026-10-07 04:30}"; REF_TO="${REF_TO:-2026-10-07 07:30}"
MIN_TRADES=15 MIN_DAYS=6 MIN_PF=1.3 MAX_SCRATCH=0.5 MAX_DAY_LOSS=-500
mkdir -p "$OUT" data/logs; touch "$DONE"
set -a; . ./.env; set +a
export PGPASSWORD="$PG_PASSWORD"
psqlq() { psql -X -q -h "$PG_HOST" -p "${PG_PORT:-5432}" -U "$PG_USER" -d "$PG_DB" -v ON_ERROR_STOP=1 "$@"; }

busy_now() {
  local dow hm; dow=$(TZ=America/New_York date +%u); hm=$((10#$(TZ=America/New_York date +%H%M)))
  if (( dow <= 5 )) && (( hm >= 925 && hm < 1605 )); then echo rth; return 0; fi
  if (( (hm >= 1800 && dow != 5 && dow != 6) || (hm < 300 && dow != 6 && dow != 7) )); then echo asia; return 0; fi
  return 1
}

register() {
  local tsv; tsv=$(mktemp)
  jq -r '.strategies[] | [.id, .engine, (.params|tojson), "true"] | @tsv' "$CFG" > "$tsv"
  printf '%s\n' "CREATE TEMP TABLE s(id text, engine text, params jsonb, enabled boolean);" "\\copy s FROM '$tsv'" \
    "INSERT INTO paper_strategies (strategy_id, account_label, engine, params_json, enabled)
       SELECT id, 'staging', engine, params, enabled FROM s
     ON CONFLICT (strategy_id) DO UPDATE SET params_json = EXCLUDED.params_json, engine = EXCLUDED.engine;" | psqlq
  rm -f "$tsv"
}

replay_pending() {
  local last d from to t0 rc before after why
  if (( 10#$(TZ=America/New_York date +%H%M) >= 1605 )); then last=$(TZ=America/New_York date +%F)
  else last=$(TZ=America/New_York date -d yesterday +%F); fi
  for d in $(psqlq -At -c "SELECT session_date FROM session_stats WHERE symbol='NQ' AND session_date <= '$last'
                            AND session_date >= '2026-09-21' AND extract(isodow FROM session_date) < 6 ORDER BY 1"); do
    grep -qx "$d" "$DONE" && continue
    if (( !force )) && why=$(busy_now); then
      echo "$(date -Is) stopped before $d — $why in progress; rerun later (finished days are kept)" | tee -a "$LOG"; return 1
    fi
    local pd; if [[ $(date -d "$d" +%u) == 1 ]]; then pd=$(date -d "$d -3 day" +%F); else pd=$(date -d "$d -1 day" +%F); fi
    from="$pd 09:00"; to="$d 16:05"; local keep="$pd 18:00"
    psqlq -c "DELETE FROM paper_trades WHERE account_label='$TMP'"
    before=$(grep -c 'Writer queue full' data/logs/collector.log 2>/dev/null || true); t0=$SECONDS
    nice -n 15 ionice -c 3 ./build/paper_engine --config "$CFG" --account-label "$TMP" \
      --replay-from "$from" --replay-to "$to" > "$OUT/replay_$d.log" 2>&1
    rc=$?
    after=$(grep -c 'Writer queue full' data/logs/collector.log 2>/dev/null || true)
    if (( rc == 0 )); then
      psqlq -c "BEGIN;
        DELETE FROM paper_trades WHERE account_label='$LABEL'
          AND entry_time >= ('$keep'::timestamp AT TIME ZONE 'America/New_York')
          AND entry_time <  ('$d 16:05'::timestamp AT TIME ZONE 'America/New_York');
        UPDATE paper_trades SET account_label='$LABEL' WHERE account_label='$TMP'
          AND entry_time >= ('$keep'::timestamp AT TIME ZONE 'America/New_York')
          AND entry_time <  ('$d 16:05'::timestamp AT TIME ZONE 'America/New_York');
        DELETE FROM paper_trades WHERE account_label='$TMP';
        COMMIT;"
      echo "$d" >> "$DONE"
    fi
    echo "$(date -Is) $d rc=$rc $((SECONDS - t0))s collector_drops=$(( ${after:-0} - ${before:-0} ))" | tee -a "$LOG"
  done
}

report() {
  # every params key whose value differs across the grid is a reporting dimension
  local dims vals
  dims=$(jq -r '[.strategies[].params | to_entries[]] | group_by(.key)
                | map(select((map(.value|tostring)|unique|length) > 1 or (length < ('"$(jq '.strategies|length' "$CFG")"'))))
                | .[][0].key' "$CFG" | grep -v '^base_id$')
  vals=$(for k in $dims; do printf "('%s', coalesce(pj->>'%s','-'))," "$k" "$k"; done); vals=${vals%,}
  psqlq -At -c "
  CREATE TEMP TABLE t AS
  SELECT p.strategy_id, s.params_json pj, ((p.entry_time AT TIME ZONE 'America/New_York') + interval '6 hours')::date d,
         p.pnl_pts * 20.0 - $COMMISSION_RT usd, p.exit_reason
  FROM paper_trades p JOIN paper_strategies s USING (strategy_id)
  WHERE p.account_label='$LABEL' AND p.exit_time IS NOT NULL;
  CREATE TEMP TABLE v AS
  WITH days AS (SELECT strategy_id, d, sum(usd) du FROM t GROUP BY 1,2),
       mid AS (SELECT strategy_id, percentile_disc(0.5) WITHIN GROUP (ORDER BY d) dm FROM days GROUP BY 1)
  SELECT t.strategy_id, min(t.pj::text)::jsonb pj, count(*) trades, count(DISTINCT t.d) days, sum(t.usd) net,
         sum(t.usd) FILTER (WHERE t.usd > 0) / NULLIF(-sum(t.usd) FILTER (WHERE t.usd < 0), 0) pf,
         avg((t.usd > 0)::int) wr, avg((t.exit_reason = 'breakeven')::int) scratch,
         ((sum(t.usd) FILTER (WHERE t.d <= m.dm)) > 0)::int + ((sum(t.usd) FILTER (WHERE t.d > m.dm)) > 0)::int halves,
         (SELECT min(du) FROM days x WHERE x.strategy_id = t.strategy_id) worst
  FROM t JOIN mid m USING (strategy_id) GROUP BY t.strategy_id, m.dm;
  SELECT json_build_object(
    'overall', (SELECT json_build_object('variants', $(jq '.strategies|length' "$CFG"), 'traded', count(*), 'trades', sum(trades),
                 'sessions', (SELECT count(DISTINCT d) FROM t), 'pct_profitable', round(100.0*avg((net>0)::int)),
                 'median_net', round(percentile_cont(0.5) WITHIN GROUP (ORDER BY net)::numeric),
                 'consistent', count(*) FILTER (WHERE trades>=$MIN_TRADES AND days>=$MIN_DAYS AND COALESCE(pf,99)>=$MIN_PF AND net>0
                                                AND halves=2 AND scratch<=$MAX_SCRATCH AND worst>=$MAX_DAY_LOSS)) FROM v),
    'dims', (SELECT json_agg(x ORDER BY x.dim, x.val) FROM (
       SELECT dim, val, count(*) variants, sum(trades) trades, round(100.0*avg((net>0)::int)) pct_profitable,
              round(percentile_cont(0.5) WITHIN GROUP (ORDER BY net)::numeric) median_net, round(avg(net)::numeric) avg_net
       FROM v, LATERAL (VALUES $vals) d(dim, val) GROUP BY 1,2) x),
    'reference', (SELECT json_agg(r ORDER BY r.entry) FROM (
       SELECT p.strategy_id, p.direction, to_char(p.entry_time AT TIME ZONE 'America/New_York','HH24:MI') entry, p.entry_price,
              to_char(p.exit_time AT TIME ZONE 'America/New_York','HH24:MI') exit, p.exit_price, p.exit_reason,
              round((p.pnl_pts*20.0 - $COMMISSION_RT)::numeric) net
       FROM paper_trades p WHERE p.account_label='$LABEL' AND p.exit_time IS NOT NULL
         AND p.entry_time >= ('$REF_FROM'::timestamp AT TIME ZONE 'America/New_York')
         AND p.entry_time <  ('$REF_TO'::timestamp AT TIME ZONE 'America/New_York')) r),
    'top', (SELECT json_agg(y ORDER BY y.net DESC) FROM (
       SELECT strategy_id, trades, days, round(net::numeric) net, round(pf::numeric,2) pf, round(100*wr) wr,
              round(scratch::numeric,2) scratch, halves, round(worst::numeric) worst
       FROM v WHERE trades>=$MIN_TRADES AND days>=$MIN_DAYS AND COALESCE(pf,99)>=$MIN_PF AND net>0 AND halves=2
                AND scratch<=$MAX_SCRATCH AND worst>=$MAX_DAY_LOSS ORDER BY net DESC LIMIT 25) y))" > "$OUT/report.json"
  {
    echo "# Research grid \`$LABEL\` — $(basename "$CFG")"
    echo
    echo "Generated $(date '+%F %H:%M %Z'). $(jq -r '.overall | "Variants \(.variants), traded \(.traded), trades \(.trades), sessions \(.sessions), \(.pct_profitable)% profitable, median net $\(.median_net), consistent \(.consistent)."' "$OUT/report.json")"
    echo
    echo "NQ \$ = points x \$20 - \$$COMMISSION_RT per trade, 1 contract, 3-tick slippage in fills. Consistent = >= $MIN_TRADES trades on >= $MIN_DAYS sessions, PF >= $MIN_PF, net > 0, both halves positive, scratches <= $MAX_SCRATCH, worst day >= \$$MAX_DAY_LOSS. With thousands of variants some pass by luck: read the dimension rows first."
    echo
    echo "## By dimension"
    echo
    echo "| key | value | variants | trades | profitable | median net \$ | avg net \$ |"
    echo "|---|---|---|---|---|---|---|"
    jq -r '.dims[]? | "| \(.dim) | \(.val) | \(.variants) | \(.trades) | \(.pct_profitable)% | \(.median_net) | \(.avg_net) |"' "$OUT/report.json"
    echo
    echo "## Reference trend $REF_FROM – ${REF_TO#* } ET"
    echo
    echo "$(jq -r '[.reference[]?] | "\(length) trades by \([.[].strategy_id]|unique|length) variants; \([.[]|select(.direction=="SHORT")]|length) short, \([.[]|select(.direction=="LONG")]|length) long; net $\([.[].net]|add // 0)"' "$OUT/report.json")"
    echo
    echo "| strategy | dir | entry | entry px | exit | exit px | reason | net \$ |"
    echo "|---|---|---|---|---|---|---|---|"
    jq -r '.reference[]? | "| \(.strategy_id) | \(.direction) | \(.entry) | \(.entry_price) | \(.exit) | \(.exit_price) | \(.exit_reason) | \(.net) |"' "$OUT/report.json"
    echo
    echo "## Consistent variants (top 25 by net)"
    echo
    echo "| strategy | trades | sessions | net \$ | PF | win % | scratch | worst day |"
    echo "|---|---|---|---|---|---|---|---|"
    jq -r '.top[]? | "| \(.strategy_id) | \(.trades) | \(.days) | \(.net) | \(.pf // "∞") | \(.wr) | \(.scratch) | \(.worst) |"' "$OUT/report.json"
  } > "$OUT/report.md"
  echo "report: $OUT/report.md ($(jq -r '.overall | "sessions \(.sessions), consistent \(.consistent)"' "$OUT/report.json"))"
}

if [[ $mode == run ]]; then
  # one run per label at a time (the nightly timer and a manual backfill must not interleave)
  exec 9>"/tmp/research_eval_${LABEL}.lock"
  flock -n 9 || { echo "another research_eval run for $LABEL is in progress — skipping"; exit 0; }
  # every mode the grid uses must exist in the engine binary, or a replay "completes" with 0 trades
  # and marks the session done
  for m in $(jq -r '[.strategies[].params.mode // empty] | unique | .[]' "$CFG"); do
    grep -aq "$m" build/paper_engine || { echo "refusing: build/paper_engine has no mode '$m' yet — rebuild it after 16:05 ET"; exit 2; }
  done
  register; replay_pending
fi
report
