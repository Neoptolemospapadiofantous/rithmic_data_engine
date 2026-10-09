#!/usr/bin/env bash
# gap_eval.sh — research for the hours no manual-signal strategy covers (2026-10-08, founder: "trading on all
# sessions everyday … add research for the missing windows"). Replays config/gap_fleet.json (label gap) over
# every recorded session with scripts/night_eval.sh, then Telegrams the lowest-drawdown profitable strategy per
# window on 1 MNQ. Replays refuse RTH and the Asia session (night_eval.sh) — run 16:05-18:00 or 03:00-09:25 ET;
# finished sessions are kept, so a later run resumes. Paper research only.
#
#   scripts/gap_eval.sh            replay pending sessions, report, Telegram the summary (once per new session count)
#   scripts/gap_eval.sh report     report + summary only
set -uo pipefail
cd "$(dirname "$0")/.."
export NIGHT_CFG=config/gap_fleet.json NIGHT_LABEL=gap NIGHT_OUT=data/gap
scripts/night_eval.sh "$@"
set -a; . ./.env; set +a; export PGPASSWORD="$PG_PASSWORD"
# Bar for a manual-signal candidate (same as the set): >= 10 trades on >= 5 sessions, net > 0, PF >= 1.5;
# ranked by max drawdown on 1 MNQ ($2/pt, $1.82 round trip).
summary=$(psql -X -At -h "$PG_HOST" -p "${PG_PORT:-5432}" -U "$PG_USER" -d "$PG_DB" -c "
WITH t AS (SELECT p.strategy_id sid, s.params_json->>'night_window' w, p.entry_time, p.pnl_pts pts, p.pnl_pts*2 - 1.82 usd,
                  ((p.entry_time AT TIME ZONE 'America/New_York') + interval '6 hours')::date d
           FROM paper_trades p JOIN paper_strategies s USING (strategy_id)
           WHERE p.account_label = 'gap' AND p.exit_time IS NOT NULL),
c AS (SELECT *, sum(usd) OVER (PARTITION BY sid ORDER BY entry_time) cum FROM t),
dd AS (SELECT sid, max(pk - cum) dd FROM (SELECT *, greatest(0, max(cum) OVER (PARTITION BY sid ORDER BY entry_time)) pk FROM c) x GROUP BY 1),
a AS (SELECT t.sid, min(t.w) w, count(*) n, count(DISTINCT d) days, avg((pts >= 2)::int) won, avg((pts <= -2)::int) lost, sum(usd) net,
             sum(usd) FILTER (WHERE usd > 0) / nullif(-sum(usd) FILTER (WHERE usd < 0), 0) pf, max(dd.dd) dd
      FROM t JOIN dd USING (sid) GROUP BY 1),
r AS (SELECT *, row_number() OVER (PARTITION BY w ORDER BY dd, net DESC) rk FROM a
      WHERE n >= 10 AND days >= 5 AND net > 0 AND coalesce(pf, 99) >= 1.5)
SELECT w || ': ' || coalesce(string_agg(sid || ' — ' || n || ' tr/' || days || 'd, ' || round(100*won) || '% won, ' || round(100*lost)
       || '% lost, PF ' || coalesce(round(pf::numeric,2)::text,'∞') || ', +\$' || round(net) || ', DD \$' || round(dd), ' | ' ORDER BY rk), 'none')
FROM (SELECT DISTINCT night_window w FROM (SELECT params_json->>'night_window' night_window FROM paper_strategies WHERE strategy_id LIKE 'gw\_%') q) ws
LEFT JOIN r USING (w) WHERE r.rk IS NULL OR r.rk <= 2 GROUP BY w ORDER BY w")
sessions=$(jq -r .sessions data/gap/report.json 2>/dev/null)
echo "$summary" > data/gap/summary.txt
if [[ "${1:-}" != report && "$sessions" != "$(cat data/gap/.notified 2>/dev/null)" && "${sessions:-0}" != 0 ]]; then
  "$HOME/.local/bin/grid-notify" "📊 Missing-window research (${sessions} sessions, 1 MNQ, lowest drawdown first; bar ≥10 trades / ≥5 sessions, PF ≥1.5, net >0) — ue 16:00-16:55 · ae 18-20 · al1 22-24 · al2 00-02 · lm 05-07 ET
${summary}
Full report: data/gap/report.md. Nothing is added to the signals without your OK." >/dev/null 2>&1 || true
  echo "$sessions" > data/gap/.notified
fi
echo "$summary"
