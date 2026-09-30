-- 2026-09-30 — loss_limits drift: the dashboard's Live page reads max_daily_trades from
-- loss_limits (ui/routers/cpp_engine.py _fetch_loss_limits), which still says 3 for
-- tradeify while config/tradeify_config.json (the value nq_executor enforces) says 5.
-- The executor never reads loss_limits, so this only corrects what the UI displays.
-- Apply by hand:  psql -h $PG_HOST -U $PG_USER -d $PG_DB -f migrations/20260930_loss_limits_max_daily_trades.sql
UPDATE loss_limits
   SET max_daily_trades = 5,
       updated_at       = NOW()
 WHERE active
   AND (account_label = 'tradeify' OR account_label IS NULL)
   AND max_daily_trades IS DISTINCT FROM 5;
