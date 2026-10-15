#!/usr/bin/env bash
# research_refresh.sh — refresh the research ledger tables (scripts/sql/research_ledger.sql) that the dashboard's
# Strategies → Research tab reads: history coverage per job type, and results per market × family × variant
# (finished history months via hist_trades + the last 30 days of the forward fleets). Also loads the direct-test
# ledger files scripts/sql/research_tests_*.sql (idempotent). Run by history_report.sh after every replayed month;
# safe to run by hand. Read-only on everything else.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(env PGOPTIONS="-c client_min_messages=warning" psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}"
      -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
T0=$(date +%s)
scripts/history_done_sync.sh >/dev/null || { echo "[research_refresh] history_done_sync failed"; exit 1; }
"${PSQL[@]}" -f scripts/sql/research_ledger.sql >/dev/null || exit 1
for f in scripts/sql/research_tests_*.sql; do [[ -f "$f" ]] && { "${PSQL[@]}" -f "$f" >/dev/null || exit 1; }; done

# Coverage per job type, straight from the queue's own job list (done / pending / nodata) + failed markers.
declare -A WHAT=(
  [flow]="NQ order flow per minute (mbo_1m)" [nq]="NQ main fleet on MBO ticks (+ collector months)"
  [nqx]="NQ night grid, staged winners / new modes, level variants on MBO ticks" [nqbars]="NQ all sets on 1m bars 2010-07 … 2023-02"
  [nqbcheck]="NQ all sets on 1m bars over the MBO months (bars vs ticks check)" [es]="ES mirror fleet on collector ticks"
  [esbars]="ES mirror fleet on 1m bars 2010-07 … 2026-03" [btc]="BTC mirrors on Binance aggTrades" [eth]="ETH mirrors on Binance aggTrades"
  [btclv]="BTC level variants (month replayed before they existed)" [ethlv]="ETH level variants" [nqcombo]="NQ combo grid (12,031) on MBO ticks"
  [crypto_bars]="crypto 1m bars for ATR warm-up")
declare -A LBL=([nq]=nq_hist [nqx]=nq_hist [nqbars]=nq_bars [nqbcheck]=nq_barcheck [es]=es_hist [esbars]=es_bars
                [btc]=crypto_hist [eth]=crypto_hist [btclv]=crypto_hist [ethlv]=crypto_hist [nqcombo]=nq_combo_hist)
rows=""
while read -r job done pending nodata first last; do
  failed=$(ls data/history/done/failed_"${job}"_* 2>/dev/null | wc -l)
  rows+="('$job','${LBL[$job]:-}','${WHAT[$job]:-$job}',$done,$pending,$nodata,$failed,NULLIF('$first','-'),NULLIF('$last','-')),"
done < <(scripts/history_queue.sh list 2>/dev/null | awk '
  { j=$1; m=$2; s=$3; n[j]++; if (s=="done") { d[j]++; if (!(j in f) || m<f[j]) f[j]=m; if (!(j in l) || m>l[j]) l[j]=m }
    else if (s=="nodata") nd[j]++; else p[j]++ }
  END { for (j in n) printf "%s %d %d %d %s %s\n", j, d[j]+0, p[j]+0, nd[j]+0, (j in f ? f[j] : "-"), (j in l ? l[j] : "-") }')
[[ -n "$rows" ]] && "${PSQL[@]}" -c "BEGIN; DELETE FROM research_coverage;
  INSERT INTO research_coverage (job, label, what, done, pending, nodata, failed, first_done, last_done) VALUES ${rows%,}; COMMIT;" >/dev/null

# Results per market × family × variant.
"${PSQL[@]}" <<'SQL' >/dev/null || exit 1
SET work_mem = '1GB';
CREATE TEMP TABLE s AS
SELECT strategy_id,
       CASE WHEN account_label = 'crypto' THEN 'crypto'
            WHEN account_label = 'es' OR strategy_id LIKE 'es\_%' THEN 'ES'
            WHEN strategy_id ~ '^(ym|rty|gc|cl)_' THEN 'other'
            WHEN account_label IN ('tradeify', 'staging') THEN 'NQ' ELSE 'other' END AS market,
       COALESCE(NULLIF(params_json->>'mode', ''), engine, '?') AS family,
       COALESCE(NULLIF(params_json->>'overlay', ''),
                CASE WHEN strategy_id LIKE '%\_\_%' THEN split_part(strategy_id, '__', 2) ELSE 'base' END) AS variant
FROM paper_strategies WHERE account_label <> 'desk';
CREATE TEMP TABLE h AS
SELECT strategy_id, count(*) n, sum(pnl_usd / GREATEST(qty, 1)) net,
       sum(pnl_usd / GREATEST(qty, 1)) FILTER (WHERE pnl_usd > 0) gw, -sum(pnl_usd / GREATEST(qty, 1)) FILTER (WHERE pnl_usd < 0) gl,
       count(DISTINCT to_char(entry_time AT TIME ZONE 'UTC', 'YYYY-MM')) months
FROM hist_trades WHERE account_label IN ('nq_hist', 'crypto_hist', 'es_hist', 'es_bars') GROUP BY 1;
CREATE TEMP TABLE f AS
SELECT strategy_id, count(*) n, sum(pnl_usd / GREATEST(qty, 1)) net FROM paper_trades
WHERE account_label IN ('tradeify', 'crypto') AND entry_time >= now() - interval '30 days' GROUP BY 1;
BEGIN;
DELETE FROM research_variants;
INSERT INTO research_variants (market, family, variant, strategies, with_history, hist_trades, hist_net, hist_per_trade, hist_pf,
                               hist_positive, hist_months, fwd30_trades, fwd30_net, fwd30_per_trade)
SELECT s.market, s.family, s.variant, count(*), count(h.strategy_id), COALESCE(sum(h.n), 0), sum(h.net),
       sum(h.net) / NULLIF(sum(h.n), 0), sum(h.gw) / NULLIF(sum(h.gl), 0),
       count(*) FILTER (WHERE h.n >= 100 AND h.net > 0), max(h.months),
       COALESCE(sum(f.n), 0), sum(f.net), sum(f.net) / NULLIF(sum(f.n), 0)
FROM s LEFT JOIN h USING (strategy_id) LEFT JOIN f USING (strategy_id)
GROUP BY s.market, s.family, s.variant;
COMMIT;
SQL
echo "[research_refresh] $("${PSQL[@]}" -c "SELECT count(*) || ' variant rows, ' || (SELECT count(*) FROM research_coverage) || ' job types, ' || (SELECT count(*) FROM research_tests) || ' tests' FROM research_variants") ($(( $(date +%s) - T0 ))s)"
