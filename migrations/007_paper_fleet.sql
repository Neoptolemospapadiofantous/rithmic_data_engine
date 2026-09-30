-- 007_paper_fleet.sql — paper-trading fleet engine schema
-- Idempotent: safe to re-run. Also applied at paper_engine startup via
-- PaperDb::ensure_schema() (same CREATE/ALTER IF NOT EXISTS style as db.cpp).

CREATE TABLE IF NOT EXISTS paper_strategies (
  strategy_id TEXT PRIMARY KEY,
  account_label TEXT NOT NULL,
  engine TEXT NOT NULL,
  params_json JSONB NOT NULL DEFAULT '{}',
  mode TEXT NOT NULL DEFAULT 'paper',
  enabled BOOLEAN NOT NULL DEFAULT TRUE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now());

CREATE TABLE IF NOT EXISTS paper_positions (
  strategy_id TEXT PRIMARY KEY REFERENCES paper_strategies(strategy_id),
  direction TEXT,
  qty INT NOT NULL DEFAULT 0,
  entry_price DOUBLE PRECISION,
  entry_time TIMESTAMPTZ,
  stop_price DOUBLE PRECISION,
  target_price DOUBLE PRECISION,
  unrealized_pnl DOUBLE PRECISION NOT NULL DEFAULT 0,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now());

CREATE TABLE IF NOT EXISTS paper_trades (
  id BIGSERIAL PRIMARY KEY,
  strategy_id TEXT NOT NULL REFERENCES paper_strategies(strategy_id),
  account_label TEXT NOT NULL,
  symbol TEXT NOT NULL,
  direction TEXT NOT NULL,
  qty INT NOT NULL,
  entry_time TIMESTAMPTZ NOT NULL,
  entry_price DOUBLE PRECISION NOT NULL,
  exit_time TIMESTAMPTZ,
  exit_price DOUBLE PRECISION,
  pnl_pts DOUBLE PRECISION,
  pnl_usd DOUBLE PRECISION,
  commission DOUBLE PRECISION,
  exit_reason TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now());

CREATE INDEX IF NOT EXISTS idx_paper_trades_strat_time
  ON paper_trades(strategy_id, entry_time DESC);

CREATE TABLE IF NOT EXISTS paper_daily (
  strategy_id TEXT NOT NULL REFERENCES paper_strategies(strategy_id),
  trade_date DATE NOT NULL,
  trades INT NOT NULL DEFAULT 0,
  wins INT NOT NULL DEFAULT 0,
  pnl_usd DOUBLE PRECISION NOT NULL DEFAULT 0,
  halted BOOLEAN NOT NULL DEFAULT FALSE,
  halt_reason TEXT,
  PRIMARY KEY (strategy_id, trade_date));

CREATE TABLE IF NOT EXISTS paper_account (
  account_label TEXT PRIMARY KEY,
  starting_balance DOUBLE PRECISION NOT NULL,
  equity DOUBLE PRECISION NOT NULL,
  peak_equity DOUBLE PRECISION NOT NULL,
  day_pnl DOUBLE PRECISION NOT NULL DEFAULT 0,
  day_start_equity DOUBLE PRECISION NOT NULL,
  trade_date DATE,
  halted BOOLEAN NOT NULL DEFAULT FALSE,
  halt_reason TEXT,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now());
