-- 009_paper_control.sql — manual control channel for the paper fleet
-- Idempotent: safe to re-run. Also applied at paper_engine startup via
-- PaperDb::ensure_schema() (same CREATE IF NOT EXISTS style as 007).
--
-- The dashboard INSERTs rows (action: 'disable' | 'enable' | 'flatten');
-- paper_engine polls unconsumed rows once per second, applies each action
-- to the matching strategy, then stamps consumed_at.

CREATE TABLE IF NOT EXISTS paper_control (
  id BIGSERIAL PRIMARY KEY,
  strategy_id TEXT NOT NULL,
  action TEXT NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  consumed_at TIMESTAMPTZ);

CREATE INDEX IF NOT EXISTS idx_paper_control_pending
  ON paper_control(consumed_at);
