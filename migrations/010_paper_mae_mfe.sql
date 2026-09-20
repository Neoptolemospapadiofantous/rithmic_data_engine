-- 010_paper_mae_mfe.sql — per-trade excursion columns on paper_trades
-- MAE/MFE (max adverse / favorable excursion, points) are tracked per leg by
-- the paper brokers and persisted at trade close. Idempotent; also applied at
-- paper_engine startup via PaperDb::ensure_schema().

ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS mae_pts DOUBLE PRECISION;
ALTER TABLE paper_trades ADD COLUMN IF NOT EXISTS mfe_pts DOUBLE PRECISION;
