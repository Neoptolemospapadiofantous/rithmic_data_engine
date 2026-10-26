-- 20261025_mm_paper_label.sql — a `label` per paper-engine instance so several can share the mm_paper_* tables
-- (official 'paper' at 300 ms; shadow 'paper_220' at the delay a server in Lighter's region would have).
-- mm_paper adds the column IF NOT EXISTS on connect; the primary-key change below is applied once by hand.
ALTER TABLE mm_paper_fills  ADD COLUMN IF NOT EXISTS label TEXT NOT NULL DEFAULT 'paper';
ALTER TABLE mm_paper_equity ADD COLUMN IF NOT EXISTS label TEXT NOT NULL DEFAULT 'paper';
ALTER TABLE mm_paper_daily  ADD COLUMN IF NOT EXISTS label TEXT NOT NULL DEFAULT 'paper';
ALTER TABLE mm_paper_events ADD COLUMN IF NOT EXISTS label TEXT NOT NULL DEFAULT 'paper';
ALTER TABLE mm_paper_equity DROP CONSTRAINT IF EXISTS mm_paper_equity_pkey;
ALTER TABLE mm_paper_equity ADD PRIMARY KEY (label, ts, market);
ALTER TABLE mm_paper_daily  DROP CONSTRAINT IF EXISTS mm_paper_daily_pkey;
ALTER TABLE mm_paper_daily  ADD PRIMARY KEY (label, day, market);
