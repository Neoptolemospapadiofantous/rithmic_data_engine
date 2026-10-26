-- 20261026_mm_paper_uid.sql — an idempotency key on the paper engine's fills and events (audit 2026-10-26).
-- The writer retries a statement whose connection died mid-flight; that statement may already have committed, so
-- fills and events carry `uid` = '<label>:<run_id>:f<n>' / ':e<n>' and insert ON CONFLICT (uid) DO NOTHING.
-- Rows written before this change keep uid NULL (a unique index allows any number of NULLs).
-- mm_paper applies the same statements IF NOT EXISTS on connect; this file is the record.
ALTER TABLE mm_paper_fills  ADD COLUMN IF NOT EXISTS uid TEXT;
ALTER TABLE mm_paper_events ADD COLUMN IF NOT EXISTS uid TEXT;
CREATE UNIQUE INDEX IF NOT EXISTS mm_paper_fills_uid  ON mm_paper_fills (uid);
CREATE UNIQUE INDEX IF NOT EXISTS mm_paper_events_uid ON mm_paper_events (uid);
