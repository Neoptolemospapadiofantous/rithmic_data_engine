#!/usr/bin/env bash
# execution_costs.sh — what live execution really costs, SIGNED (positive = worse for us), from live_trades.
#
#   scripts/execution_costs.sh [account_label] [since YYYY-MM-DD]     (default: tradeify, 2026-09-01)
#
# live_trades.entry_slippage_ticks / exit_slippage_ticks are |fill − signal| (unsigned, latency_logger.hpp) — a fill 4
# ticks better counts like one 4 ticks worse. This report uses the stored prices instead:
#   entry: fill_price vs trigger_price (the price when the signal fired), by side
#   stop exits: exit_price vs sl_price, by side (only exits whose reason is a stop)
# and turns them into a round trip per contract: commission (from config/<account>_config.json, else 1.82 $) +
# entry slippage + stop slippage × the share of trades that end on a stop. Read-only.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
acct="${1:-tradeify}"; since="${2:-2026-09-01}"
[[ "$acct" =~ ^[A-Za-z0-9_]+$ && "$since" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || { sed -n 2,4p "$0"; exit 2; }
export PGPASSWORD="$(grep -E '^PG_PASSWORD=' .env | head -1 | cut -d= -f2- | tr -d "'\"")"
PSQL=(psql -h 127.0.0.1 -U rithmic_user -d rithmic -v ON_ERROR_STOP=1)
comm=$(jq -r '.commission_rt // 1.82' "config/${acct}_config.json" 2>/dev/null || echo 1.82)
"${PSQL[@]}" -c "
WITH t AS (
  SELECT instrument, direction, exit_reason,
         CASE WHEN direction = 'LONG' THEN 1 ELSE -1 END AS s,
         trigger_price, fill_price, sl_price, exit_price,
         CASE WHEN instrument LIKE 'M%' THEN 2.0 ELSE 20.0 END AS pv
  FROM live_trades
  WHERE account_label = '$acct' AND entry_time >= '$since'::date AND exit_time IS NOT NULL),
e AS (
  SELECT instrument, max(pv) AS pv, count(*) AS n,
         avg(s * (fill_price - trigger_price)) FILTER (WHERE trigger_price > 0) AS entry_pts,
         percentile_cont(0.5) WITHIN GROUP (ORDER BY s * (fill_price - trigger_price)) FILTER (WHERE trigger_price > 0) AS entry_med_pts,
         avg(s * (sl_price - exit_price)) FILTER (WHERE sl_price > 0 AND exit_reason IN ('exchange_stop', 'stop_loss')) AS stop_pts,
         count(*) FILTER (WHERE sl_price > 0 AND exit_reason IN ('exchange_stop', 'stop_loss'))::float / count(*) AS stop_share
  FROM t GROUP BY instrument)
SELECT instrument, n AS trades,
       round((entry_pts / 0.25)::numeric, 2) AS entry_ticks_avg, round((entry_med_pts / 0.25)::numeric, 2) AS entry_ticks_median,
       round((stop_pts / 0.25)::numeric, 2) AS stop_exit_ticks_avg, round((100 * stop_share)::numeric, 0) AS pct_trades_stopped,
       round(($comm / pv)::numeric, 2) AS commission_pts,
       round(($comm / pv + coalesce(entry_pts, 0) + coalesce(stop_pts, 0) * stop_share)::numeric, 2) AS round_trip_pts
FROM e ORDER BY instrument;"
echo "positive = worse for us; round_trip_pts = commission + average entry slippage + stop slippage × share stopped (per contract, in points)"
