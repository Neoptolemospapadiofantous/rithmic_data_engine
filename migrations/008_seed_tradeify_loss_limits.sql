-- 008_seed_tradeify_loss_limits.sql — seed the loss_limits row the /live
-- compliance panel (and the executor's risk gate) reads for the Tradeify
-- 25K Growth account RTU989361488.
--
-- Without this row the backend returns NULL limits and the dashboard shows
-- placeholders. Values mirror config/tradeify_config.json — the platform
-- account's risk profile is the source of truth; update both together.
--
-- Idempotent: inserts only when no active tradeify row exists. Safe to
-- re-run; existing (possibly operator-tuned) rows are never overwritten.

-- account_label column is added by db.cpp ensure_schema on newer installs;
-- guarantee it here so the seed works on fresh databases too.
ALTER TABLE loss_limits ADD COLUMN IF NOT EXISTS account_label TEXT;

INSERT INTO loss_limits
  (symbol, account_label, daily_loss_limit, weekly_loss_limit,
   max_drawdown, max_daily_trades, active, notes)
SELECT 'MNQ', 'tradeify', -1000.0, NULL, -2000.0, 3, true,
       'Tradeify 25K Growth — config/tradeify_config.json (seeded by 008)'
WHERE NOT EXISTS (
  SELECT 1 FROM loss_limits WHERE account_label = 'tradeify' AND active
);
