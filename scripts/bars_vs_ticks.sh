#!/usr/bin/env bash
# bars_vs_ticks.sh — is the 1-minute-bar replay (paper_engine --dbn-bars) good enough for each strategy?
#
#   usage: scripts/bars_vs_ticks.sh [--min-trades 20] [--max-count-diff 0.2] [--max-per-trade-diff 2] [--min-corr 0.7]
#
# Compares the same months replayed both ways — nq_barcheck (bars, job nqbcheck) vs nq_hist (MBO ticks) —
# finished months only (hist_trades). Per strategy: trade count, net, and the correlation of daily P&L.
# Reliable = ≥ min-trades on ticks, trade count within ±max-count-diff, net per trade within ±$max-per-trade-diff
# (per MNQ), daily-P&L correlation ≥ min-corr. Writes history_bar_reliability (read by walk_forward.sh /
# payout_sim.sh to decide which nq_bars results count) and data/history/bars_vs_ticks.md. Read-only otherwise.
# Order-flow strategies (book gates, delta / absorption / volume burst) cannot pass: bars carry no book and only
# a crude aggressor side — that is the point of the check, not a bug in it.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
MIN=20; CNT=0.2; PT=2; CORR=0.7
while [[ $# -gt 0 ]]; do case "$1" in
  --min-trades) MIN="$2"; shift;; --max-count-diff) CNT="$2"; shift;;
  --max-per-trade-diff) PT="$2"; shift;; --min-corr) CORR="$2"; shift;;
  *) echo "unknown arg $1"; exit 2;; esac; shift; done
for v in "$MIN" "$CNT" "$PT" "$CORR"; do [[ "$v" =~ ^[0-9.]+$ ]] || { echo "bad number $v"; exit 2; }; done
OUT=data/history/bars_vs_ticks.md
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
scripts/history_done_sync.sh >/dev/null || { echo "history_done_sync failed"; exit 1; }
months=$("${PSQL[@]}" -c "SELECT string_agg(month, ',' ORDER BY month) FROM history_done WHERE market = 'nqbcheck'")
[[ -n "$months" ]] || { echo "[bars_vs_ticks] no finished nqbcheck month yet"; exit 0; }

"${PSQL[@]}" -F '|' <<SQL > "$OUT.tmp" || exit 1
SET work_mem = '512MB';
CREATE TEMP TABLE t AS
SELECT strategy_id, account_label AS lbl, ((entry_time AT TIME ZONE 'America/New_York') + interval '6 hours')::date AS d,
       pnl_usd / GREATEST(qty, 1) AS p
FROM hist_trades
WHERE account_label IN ('nq_hist', 'nq_barcheck')
  AND to_char(entry_time AT TIME ZONE 'UTC', 'YYYY-MM') IN (SELECT month FROM history_done WHERE market = 'nqbcheck');
-- only strategies the tick side has for these months (nq_hist main / nqx sets finish at different times)
CREATE TEMP TABLE days AS
SELECT strategy_id, d, sum(p) FILTER (WHERE lbl = 'nq_hist') AS tk, sum(p) FILTER (WHERE lbl = 'nq_barcheck') AS br
FROM t GROUP BY 1, 2;
CREATE TEMP TABLE cnt AS
SELECT strategy_id, count(*) FILTER (WHERE lbl = 'nq_hist')::int AS ticks_n, count(*) FILTER (WHERE lbl = 'nq_barcheck')::int AS bars_n
FROM t GROUP BY 1;
CREATE TEMP TABLE s AS
SELECT days.strategy_id, max(cnt.ticks_n) AS ticks_n, max(cnt.bars_n) AS bars_n,
       COALESCE(sum(tk), 0) AS ticks_net, COALESCE(sum(br), 0) AS bars_net,
       corr(COALESCE(tk, 0), COALESCE(br, 0)) AS day_corr
FROM days JOIN cnt USING (strategy_id) GROUP BY days.strategy_id
HAVING count(tk) > 0;
CREATE TEMP TABLE v AS
SELECT s.*,
       CASE WHEN ticks_n < $MIN                                         THEN 'thin'
            WHEN abs(bars_n - ticks_n) > $CNT * ticks_n                   THEN 'trade_count'
            WHEN abs(bars_net / GREATEST(bars_n, 1) - ticks_net / ticks_n) > $PT THEN 'per_trade'
            WHEN COALESCE(day_corr, 0) < $CORR                            THEN 'daily_corr'
            ELSE 'ok' END AS reason
FROM s;
BEGIN;
DELETE FROM history_bar_reliability;
INSERT INTO history_bar_reliability (strategy_id, months, ticks_n, bars_n, ticks_net, bars_net, day_corr, reliable, reason)
SELECT strategy_id, (SELECT count(*) FROM history_done WHERE market = 'nqbcheck'), ticks_n, bars_n, ticks_net, bars_net,
       day_corr, reason = 'ok', reason FROM v;
COMMIT;
\echo ##SUMMARY
SELECT count(*), count(*) FILTER (WHERE reason = 'ok'), count(*) FILTER (WHERE reason = 'thin'),
       count(*) FILTER (WHERE reason = 'trade_count'), count(*) FILTER (WHERE reason = 'per_trade'),
       count(*) FILTER (WHERE reason = 'daily_corr') FROM v;
\echo ##FAMILY
SELECT split_part(strategy_id, '_', 1), count(*) FILTER (WHERE reason <> 'thin'), count(*) FILTER (WHERE reason = 'ok'),
       round(100.0 * count(*) FILTER (WHERE reason = 'ok') / NULLIF(count(*) FILTER (WHERE reason <> 'thin'), 0)),
       round(percentile_cont(0.5) WITHIN GROUP (ORDER BY day_corr)::numeric, 2),
       round(percentile_cont(0.5) WITHIN GROUP (ORDER BY (bars_net / GREATEST(bars_n, 1) - ticks_net / GREATEST(ticks_n, 1)))::numeric, 2)
FROM v GROUP BY 1 HAVING count(*) FILTER (WHERE reason <> 'thin') >= 3 ORDER BY 4 DESC NULLS LAST, 2 DESC;
\echo ##WORST
SELECT strategy_id, ticks_n, bars_n, round(ticks_net::numeric), round(bars_net::numeric), round(day_corr::numeric, 2), reason
FROM v WHERE reason <> 'thin' ORDER BY abs(bars_net - ticks_net) DESC LIMIT 15;
SQL
{
  echo "# Bars vs ticks — can a strategy be judged on the 2010–2023 bar history?"; echo
  echo "_Generated $(date -Iseconds) by scripts/bars_vs_ticks.sh over nqbcheck months: ${months}._"; echo
  echo "Same months replayed from MBO ticks (\`nq_hist\`) and from 1-minute bars (\`nq_barcheck\`). **Reliable** = ≥ $MIN tick"
  echo "trades, trade count within ±$(awk "BEGIN{print $CNT*100}")%, net per trade within ±\$$PT (per MNQ), daily-P&L correlation ≥ $CORR."
  echo "Only reliable strategies' \`nq_bars\` results are used by walk_forward.sh / payout_sim.sh."; echo
  awk -F'|' '/^##SUMMARY/{m=1;next} /^##FAMILY/{m=2; print "| family | compared | reliable | % | median daily corr | median Δ $/trade (bars − ticks) |"; print "|---|---|---|---|---|---|"; next} /^##WORST/{m=3; print ""; print "### Largest disagreements"; print ""; print "| strategy | tick trades | bar trades | tick net | bar net | daily corr | why |"; print "|---|---|---|---|---|---|---|"; next}
    m==1 {print "Strategies: " $1 " · **reliable " $2 "** · too few trades " $3 " · trade count off " $4 " · per-trade P&L off " $5 " · daily P&L uncorrelated " $6; print ""; print "### By family"; print ""; next}
    m>=2 {printf "|"; for (i=1;i<=NF;i++) printf " %s |", $i; print ""}' "$OUT.tmp"
} > "$OUT"; rm -f "$OUT.tmp"
grep -m1 '^Strategies' "$OUT" | sed 's/^/[bars_vs_ticks] /'
