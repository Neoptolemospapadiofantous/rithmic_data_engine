-- 011_trade_context.sql — market context captured for every closed trade
-- (live_trades and paper_trades), computed by the dashboard backend from the
-- collector's `ticks` table. This is the "what did good trades have in common"
-- dataset: opening-range size, entry timing, pre-entry volatility/volume,
-- overnight range and gap, plus the trade's own MAE/MFE outcome.
-- Idempotent. Rows are (re)computed by GET /api/cpp/insights when missing.
CREATE TABLE IF NOT EXISTS trade_context (
  source                 TEXT        NOT NULL,          -- 'live' | 'paper'
  trade_id               BIGINT      NOT NULL,
  strategy_id            TEXT,
  account_label          TEXT,
  engine                 TEXT,                          -- 'orb' | 'mtf_scalper'
  direction              TEXT,
  entry_time             TIMESTAMPTZ,
  exit_time              TIMESTAMPTZ,
  trade_date             DATE,                          -- New York trading date
  dow                    SMALLINT,                      -- 0=Mon … 6=Sun (ET)
  entry_min_after_open   REAL,                          -- minutes after 09:30 ET
  hold_secs              REAL,
  orb_minutes            SMALLINT,
  orb_high               DOUBLE PRECISION,
  orb_low                DOUBLE PRECISION,
  orb_range_pts          REAL,
  dist_from_range_pts    REAL,                          -- entry beyond the breakout level
  pre5_range_pts         REAL,                          -- high-low of the 5 min before entry
  pre5_volume            BIGINT,
  pre5_ticks             INTEGER,
  pre15_range_pts        REAL,
  overnight_range_pts    REAL,                          -- 18:00 ET prev day → 09:30
  open_gap_pts           REAL,                          -- 09:30 first print − prior 16:00 last print
  day_range_at_entry_pts REAL,                          -- 09:30 → entry
  pnl_usd                REAL,
  pnl_pts                REAL,
  mae_pts                REAL,
  mfe_pts                REAL,
  mfe_capture            REAL,                          -- pnl_pts / mfe_pts
  exit_reason            TEXT,
  computed_at            TIMESTAMPTZ DEFAULT now(),
  PRIMARY KEY (source, trade_id)
);
CREATE INDEX IF NOT EXISTS idx_trade_context_strategy ON trade_context (strategy_id, trade_date);
