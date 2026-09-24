-- 013_paper_book.sql — top-of-book on paper trades (mirrors paper_db.cpp kSchemaSQL; idempotent).
-- Shadow fill of every paper trade at bid/ask (buy at ask, sell at bid, stops no better than the
-- touch) regardless of the fill model the strategy ran with, plus the spread at entry/exit.
ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS entry_bbo          DOUBLE PRECISION;
ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS exit_bbo           DOUBLE PRECISION;
ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS pnl_bbo_usd        DOUBLE PRECISION;
ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS spread_entry_ticks DOUBLE PRECISION;
ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS spread_exit_ticks  DOUBLE PRECISION;
ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS fill_model         TEXT;       -- last_slip | bbo
-- Every entry signal a broker saw, with the book at that moment and the gate decision
-- ("taken" or "blocked:<reason>"). Joining a sibling variant's rows to its base's shows
-- exactly which signals a gate removed and what those trades made.
CREATE TABLE IF NOT EXISTS paper_signals (
    id BIGSERIAL PRIMARY KEY, strategy_id TEXT NOT NULL, account_label TEXT NOT NULL,
    ts TIMESTAMPTZ NOT NULL, direction TEXT, price DOUBLE PRECISION,
    spread_ticks DOUBLE PRECISION, imbalance DOUBLE PRECISION, microprice_dev_ticks DOUBLE PRECISION,
    spread_rel DOUBLE PRECISION, decision TEXT NOT NULL, reason TEXT);
CREATE INDEX IF NOT EXISTS idx_paper_signals_strat_ts ON paper_signals (strategy_id, ts);
