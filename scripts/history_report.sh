#!/usr/bin/env bash
# history_report.sh — data/history/report.md: how every paper strategy did over the replayed history.
#
#   usage: scripts/history_report.sh [--min-trades 30]
#
# Markets: NQ (label nq_hist: Databento MBO 2023-03 … 2026-03 + the collector's own ticks from
# 2026-09-19 — no data 2026-03-20 … 2026-09-18), BTC and ETH (label crypto_hist, ids btc_*/eth_*:
# Binance aggTrades). All money is NET per contract (pnl_usd / qty, costs included as the fleet
# configs set them), so the fleet's 1-lot and 3-lot variants compare.
# Per market: replay coverage, the overall ranking (trades, PF, net, worst day, winning years / years
# traded), the strategies that hold up in EVERY year, a per-year table for the top names, and the
# overlay study behind the `adjust` proposal (which suffix knobs help their base in which years).
# Read-only: never touches config/paper_fleet.json or any live config.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
MIN=30
while [[ $# -gt 0 ]]; do case "$1" in --min-trades) MIN="$2"; shift;; *) echo "unknown arg $1"; exit 2;; esac; shift; done
OUT="data/history/report.md"; mkdir -p data/history
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
Q() { psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -F '|' -v ON_ERROR_STOP=1 -c "SET work_mem='512MB'; $1" | sed '/^SET$/d'; }
md() { sed 's/|/ | /g; s/^/| /; s/$/ |/'; }

section() {   # $1 title  $2 label  $3 id filter (SQL)
  local title="$1" label="$2" idf="$3"
  local base="SELECT strategy_id, (entry_time AT TIME ZONE 'America/New_York') AS et, pnl_usd / GREATEST(qty,1) AS p
              FROM paper_trades WHERE account_label = '$label' $idf"
  echo "## $title"; echo
  echo "Coverage (trades per month replayed):"; echo
  echo "| month | trades | strategies | net/ct \$ |"; echo "|---|---|---|---|"
  Q "WITH t AS ($base) SELECT to_char(et,'YYYY-MM'), count(*), count(DISTINCT strategy_id), round(sum(p)::numeric,0) FROM t GROUP BY 1 ORDER BY 1" | md
  echo
  local yrs; yrs=$(Q "WITH t AS ($base) SELECT count(DISTINCT extract(year FROM et)) FROM t")
  [[ "${yrs:-0}" -gt 0 ]] || { echo "_no rows yet_"; echo; return; }
  local agg="
    WITH t AS ($base),
    d AS (SELECT strategy_id, et::date AS d, sum(p) AS dp FROM t GROUP BY 1,2),
    y AS (SELECT strategy_id, extract(year FROM et)::int AS yr, count(*) n, sum(p) net FROM t GROUP BY 1,2),
    o AS (SELECT strategy_id, count(*) n, sum(p) net,
                 sum(p) FILTER (WHERE p > 0) / NULLIF(-sum(p) FILTER (WHERE p < 0), 0) pf,
                 avg((p > 0)::int) win FROM t GROUP BY 1),
    w AS (SELECT strategy_id, min(dp) worst_day, count(*) days FROM d GROUP BY 1),
    c AS (SELECT strategy_id, count(*) yrs, count(*) FILTER (WHERE net > 0) pos_yrs FROM y GROUP BY 1)
    SELECT o.strategy_id, o.n, round(o.pf::numeric,2), round(o.net::numeric,0), round((o.net/o.n)::numeric,2),
           round(w.worst_day::numeric,0), c.pos_yrs || '/' || c.yrs, round((100*o.win)::numeric,0)
    FROM o JOIN w USING (strategy_id) JOIN c USING (strategy_id)
    WHERE o.n >= $MIN"
  echo "### Overall ranking (net per contract, ≥ $MIN trades; top 40)"; echo
  echo "| strategy | trades | PF | net \$ | \$/trade | worst day \$ | winning years | win % |"; echo "|---|---|---|---|---|---|---|---|"
  Q "$agg ORDER BY o.net DESC LIMIT 40" | md; echo
  echo "### Holds up in every year (net > 0 in each of the $yrs year(s) with data, ≥ $MIN trades, PF ≥ 1.2)"; echo
  echo "| strategy | trades | PF | net \$ | \$/trade | worst day \$ | winning years | win % |"; echo "|---|---|---|---|---|---|---|---|"
  local every; every=$(Q "$agg AND c.pos_yrs = $yrs AND c.yrs = $yrs AND o.pf >= 1.2 ORDER BY o.net DESC LIMIT 60")
  if [[ -n "$every" ]]; then echo "$every" | md; else echo "| _none_ | | | | | | | |"; fi; echo
  echo "### Per year — top 25 overall"; echo
  local ycols; ycols=$(Q "WITH t AS ($base) SELECT string_agg(DISTINCT extract(year FROM et)::int::text, ' ' ORDER BY extract(year FROM et)::int::text) FROM t")
  local hdr="| strategy |" sep="|---|"; for yy in $ycols; do hdr+=" $yy net (n) |"; sep+="---|"; done
  echo "$hdr"; echo "$sep"
  local top; top=$(Q "$agg ORDER BY o.net DESC LIMIT 25" | cut -d'|' -f1 | paste -sd, | sed "s/[^,]*/'&'/g")
  if [[ -n "$top" ]]; then
    local cols=""; for yy in $ycols; do cols+=", coalesce(max(CASE WHEN yr=$yy THEN round(net::numeric,0) || ' (' || n || ')' END), '–')"; done
    Q "WITH t AS ($base), y AS (SELECT strategy_id, extract(year FROM et)::int yr, count(*) n, sum(p) net FROM t GROUP BY 1,2)
       SELECT strategy_id $cols FROM y WHERE strategy_id IN ($top) GROUP BY strategy_id ORDER BY sum(net) DESC" | md
  fi
  echo
  echo "### Overlay study — does the suffix knob beat its own base? (variants named <base>__<overlay>)"; echo
  echo "| overlay | variant/base pairs | pairs better overall | mean Δ net \$ | years where Δ>0 on average |"; echo "|---|---|---|---|---|"
  Q "WITH t AS ($base),
     y AS (SELECT strategy_id, extract(year FROM et)::int yr, sum(p) net FROM t GROUP BY 1,2),
     v AS (SELECT strategy_id, split_part(strategy_id,'__',1) AS b, substr(strategy_id, length(split_part(strategy_id,'__',1)) + 3) AS ov
           FROM (SELECT DISTINCT strategy_id FROM y) s WHERE strategy_id LIKE '%\_\_%'),
     pr AS (SELECT v.ov, v.strategy_id, v.b, yv.yr, yv.net - coalesce(yb.net,0) AS dlt
            FROM v JOIN y yv ON yv.strategy_id = v.strategy_id LEFT JOIN y yb ON yb.strategy_id = v.b AND yb.yr = yv.yr
            WHERE EXISTS (SELECT 1 FROM y WHERE strategy_id = v.b)),
     pv AS (SELECT ov, strategy_id, sum(dlt) d FROM pr GROUP BY 1,2),
     py AS (SELECT ov, yr, avg(dlt) d FROM pr GROUP BY 1,2)
     SELECT pv.ov, count(*), count(*) FILTER (WHERE pv.d > 0), round(avg(pv.d)::numeric,0),
            (SELECT string_agg(yr::text, ' ' ORDER BY yr) FROM py WHERE py.ov = pv.ov AND py.d > 0)
     FROM pv GROUP BY pv.ov HAVING count(*) >= 3 ORDER BY avg(pv.d) DESC LIMIT 40" | md
  echo
}

{
  echo "# History replay report"; echo
  echo "_Generated $(date -Is) by scripts/history_report.sh. Net per contract after the fleet's configured costs._"; echo
  echo "Data: NQ = Databento GLBX MBO (NQ.c.0, 2023-03 … 2026-03-19) + the collector's own ticks (2026-09-19 …);"
  echo "**no NQ data 2026-03-20 … 2026-09-18** (not faked). Crypto = Binance USDⓈ-M aggTrades (2020-01 …), no quotes —"
  echo "book-gated variants are blocked there (no_quote). Crypto order-by-order (Bitstamp MBO) only exists from 2026-10-10."
  echo "Each month is replayed cold (strategy state does not carry over month boundaries)."; echo
  [[ -f data/history/queue_status.json ]] && { echo "Queue: $(jq -r '"\(.done) done, \(.pending) pending, ETA \(.eta_hours) h"' data/history/queue_status.json)"; echo; }
  section "NQ (label nq_hist)" nq_hist ""
  section "BTC (label crypto_hist, btc_*)" crypto_hist "AND strategy_id LIKE 'btc\\_%'"
  section "ETH (label crypto_hist, eth_*)" crypto_hist "AND strategy_id LIKE 'eth\\_%'"
  cat <<'EOF'
## Crypto cost and knob model over the years

- Costs are RELATIVE (since 2026-10-10): `commission_pct` 0.001 = 0.10 % of notional round trip (half per fill at
  its own price) and `slippage_bps` (BTC 0.49, ETH 0.60 = NQ's 3 ticks × the range ratio, at the build price), so a
  2020 trade at $7k BTC pays 2020's fee, not 2026's.
- Point knobs (`sl_points`, `trail_step`, `trail_be_trigger`, `trail_be_offset`, `breakout_buffer`, `max_entry_offset`,
  `tp_points`, `trail_step_trend`, `trail_step_range`, `gap_min_pts`, `gf_min_pts`) are rescaled per replayed MONTH by
  that month's mean close / the build price (`crypto_fleet.sh month-config`), i.e. constant in % of price.
  ATR-, range- and percent-relative knobs (`regime_*_atr`, `regime_*_eff`, atr_break k, chandelier, rvol) need no
  rescaling.
- NOT rescaled (fixed NQ-sized values remain in these families — read their crypto rows with care):
  `absorption_rev_*` (26 ids per coin: `az_stop_ticks` / `az_div_ticks` in NQ ticks), `news_break_*` (24:
  `nb_range_min`), MTF scalpers whose regime mode is fixed (`min_atr_ticks`), and `gap_fade`/`gap_fill` defaults
  (`gap_min_pts`/`gf_min_pts` = 20 when a variant does not set them; the 64 gap_fade ids that set them are rescaled).

## `adjust` — proposal (nothing is changed automatically)

Re-tune by WALK-FORWARD, never on the whole sample:

1. **Split.** NQ: tune on 2023-03 … 2024-12, validate on 2025, final test on 2026 (MBO 2026-01 … 03 + collector
   2026-09 …). Crypto: tune 2020 … 2023, validate 2024, test 2025 … 2026. A knob value is kept only if it wins
   on the tune years AND does not lose on the validation year; the test years are read once.
2. **Knobs to re-tune** (one at a time per base, on the shortlist above — "holds up every year" first):
   - exit side: `trail_step` (the 09-28 finding: 25 beat 15 on 7 of 8 bases — re-check per year), `trail_be_trigger`,
     `sl_points`, `tp_r` (fixed targets lost on average in 09-28; confirm on years);
   - entry filters: the regime presets (`__rg_*`: `regime_min/max_eff`, `regime_max_move_atr`), `rvol_min`
     (`__rvol15`), the time window (`__am`/`__open`), the book fade `imbalance_max` (`__inv`) — NQ only, MBO
     years have a book, crypto history does not;
   - crypto: every point knob was scaled from NQ by the coin's range ratio (crypto_fleet.sh); re-derive the
     ratio per YEAR (BTC's range vs NQ changed several-fold since 2020) before tuning anything else.
   The overlay study above already shows, per overlay suffix, in which years it beat its base — an overlay that
   helps in every year is a candidate to fold into the base; one that helps in a single year is noise.
3. **Promotion stays manual:** a re-tuned variant goes into the paper fleet as a NEW id (never edit a base), is
   replayed over the same history (scripts/history_month.sh), and only the founder moves anything to live.
EOF
} > "$OUT.tmp" && mv "$OUT.tmp" "$OUT"
echo "wrote $OUT ($(wc -l < "$OUT") lines)"
