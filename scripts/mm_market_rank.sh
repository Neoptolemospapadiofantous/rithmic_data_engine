#!/usr/bin/env bash
# mm_market_rank.sh — which Lighter markets should the market maker trade tomorrow? (2026-10-29, founder: "do all")
#
#   scripts/mm_market_rank.sh                 rank on label paper_opt, write data/mm_gates/markets.{json,md}
#   MM_RANK_LABEL=paper_bn scripts/mm_market_rank.sh
#
# Why: half of the paper loss comes from a few markets whose 10 s markout is far worse than the spread earned (BERA
# −10 bps, GMX −8, PYTH −7, POL −7 on paper_opt 10-27/28). A market is BLOCKED for the next UTC day when, over the
# trailing LOOKBACK complete days, it had ≥ MIN_FILLS fills AND net P&L < 0 AND volume-weighted 10 s markout <
# MK10_MAX bps. Too few fills = ALLOWED (no evidence either way). The rule is fixed here, before its data.
#
# Walk-forward check of the rule itself, from rows that already exist (paper markets are tracked independently, so
# "trade only the allowed markets" is the same rows minus the blocked ones): for every day d with LOOKBACK earlier
# days, the list is built from days < d only and scored on day d. The report shows all-markets vs allowed-markets $
# per day. Adopting the list in the live/paper engines is a separate decision (pre-registered, founder's call):
#   ADOPT when, over ≥ 7 scored days, allowed-markets net $ > all-markets net $ on ≥ 5 days.
#
# Read-only on Postgres; writes data/mm_gates/markets.json + markets.md. Exit 0 always (called from mm_gates.sh).
# Env (defaults): MM_RANK_LABEL (paper_opt), MM_RANK_LOOKBACK (3), MM_RANK_MIN_FILLS (20), MM_RANK_MK10_MAX (-2).

set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 0
LABEL="${MM_RANK_LABEL:-paper_opt}"
LOOKBACK="${MM_RANK_LOOKBACK:-3}"
MIN_FILLS="${MM_RANK_MIN_FILLS:-20}"
MK10_MAX="${MM_RANK_MK10_MAX:--2}"
OUT_DIR="${MM_RANK_OUT:-$ROOT/data/mm_gates}"
mkdir -p "$OUT_DIR"

PW=$(grep -E '^PG_PASSWORD=' .env 2>/dev/null | head -1 | cut -d= -f2- | tr -d "'\"")
[ -n "$PW" ] || { echo "mm_market_rank: no PG_PASSWORD in .env — nothing ranked"; exit 0; }
q() { PGPASSWORD="$PW" psql -h localhost -U rithmic_user -d rithmic -At -v ON_ERROR_STOP=1 -c "$1"; }

# Complete UTC days only: today is still running.
TODAY=$(date -u +%F)

# One SQL pass: per (scored day d, market): trailing stats over the LOOKBACK days before d, the market's own P&L on d.
# The last row set (d = today) is tomorrow's list; earlier d are the walk-forward check.
JSON=$(q "
with days as (select distinct day from mm_paper_daily where label = '$LABEL' order by day),
     d_all as (select day as d from days union select '$TODAY'::date),
     win as (
       select d.d, m.market,
              sum(m.fills) as fills, sum(m.pnl) as pnl, sum(m.volume) as vol,
              sum(m.mk10_bps * m.volume) / nullif(sum(m.volume), 0) as mk10,
              count(distinct m.day) as ndays
       from d_all d
       join mm_paper_daily m on m.label = '$LABEL' and m.day < d.d
            and m.day >= (select coalesce(min(day), d.d) from (select day from days where day < d.d order by day desc limit $LOOKBACK) x)
       group by d.d, m.market),
     verdict as (
       select d, market, fills, pnl, vol, mk10, ndays,
              (ndays >= $LOOKBACK and fills >= $MIN_FILLS and pnl < 0 and coalesce(mk10, 0) < $MK10_MAX) as blocked
       from win)
select coalesce(json_agg(json_build_object('d', v.d, 'market', v.market, 'fills', v.fills, 'pnl', round(v.pnl::numeric, 4),
       'mk10', round(v.mk10::numeric, 2), 'ndays', v.ndays, 'blocked', v.blocked, 'pnl_d', round(t.pnl::numeric, 4))
       order by v.d, v.market), '[]')
from verdict v left join mm_paper_daily t on t.label = '$LABEL' and t.day = v.d and t.market = v.market;" 2>&1) || {
    echo "mm_market_rank: query failed: $JSON"; exit 0; }

echo "$JSON" | jq --arg label "$LABEL" --arg today "$TODAY" --argjson lb "$LOOKBACK" --argjson mf "$MIN_FILLS" --argjson mk "$MK10_MAX" '
  . as $rows
  | ($rows | map(select(.d == $today))) as $tom
  | ($rows | map(select(.d != $today and .ndays >= $lb)) | group_by(.d)
      | map({day: .[0].d,
             all_usd: (map(.pnl_d // 0) | add),
             allowed_usd: (map(select(.blocked | not) | .pnl_d // 0) | add),
             blocked: (map(select(.blocked) | .market))})) as $wf
  | {generated: (now | todate), label: $label, for_day: $today,
     rule: {lookback_days: $lb, min_fills: $mf, mk10_max_bps: $mk, block_if: "fills >= min AND net < 0 AND mk10 < max over the lookback"},
     allowed: ($tom | map(select(.blocked | not) | .market)),
     blocked: ($tom | map(select(.blocked) | {market, fills, pnl, mk10})),
     ranking: ($tom | sort_by(-.pnl) | map({market, fills, pnl, mk10, blocked})),
     walk_forward: $wf,
     walk_forward_summary: {days: ($wf | length),
                            allowed_ahead_days: ($wf | map(select(.allowed_usd > .all_usd)) | length),
                            all_usd: ($wf | map(.all_usd) | add // 0),
                            allowed_usd: ($wf | map(.allowed_usd) | add // 0)}}' > "$OUT_DIR/markets.json.tmp" \
  && mv "$OUT_DIR/markets.json.tmp" "$OUT_DIR/markets.json" || { echo "mm_market_rank: jq failed"; exit 0; }

jq -r '
  "# Market ranking — \(.label), for \(.for_day) (built \(.generated))\n",
  "Rule: block when, over the last \(.rule.lookback_days) complete days, fills ≥ \(.rule.min_fills), net < 0 and 10 s markout < \(.rule.mk10_max_bps) bps.\n",
  "**Allowed (\(.allowed | length)):** \(.allowed | join(", "))\n",
  "**Blocked (\(.blocked | length)):** \(if (.blocked | length) == 0 then "none" else (.blocked | map("\(.market) ($\(.pnl), \(.fills) fills, mk10 \(.mk10))") | join(", ")) end)\n",
  "## Ranking (trailing window)\n", "| Market | Fills | Net $ | mk10 bps | Blocked |", "|---|---|---|---|---|",
  (.ranking[] | "| \(.market) | \(.fills) | \(.pnl) | \(.mk10 // "—") | \(if .blocked then "yes" else "" end) |"),
  "\n## Walk-forward check of the rule (list built from earlier days only)\n",
  "| Day | All markets $ | Allowed only $ | Blocked |", "|---|---|---|---|",
  (.walk_forward[] | "| \(.day) | \(.all_usd * 100 | round / 100) | \(.allowed_usd * 100 | round / 100) | \(.blocked | join(", ")) |"),
  "\nScored days: \(.walk_forward_summary.days); allowed ahead on \(.walk_forward_summary.allowed_ahead_days). Adopt rule: ≥ 7 scored days and ahead on ≥ 5."
' "$OUT_DIR/markets.json" > "$OUT_DIR/markets.md"
jq -c '{label, for_day, allowed: (.allowed | length), blocked: [.blocked[].market], wf: .walk_forward_summary}' "$OUT_DIR/markets.json"
exit 0
