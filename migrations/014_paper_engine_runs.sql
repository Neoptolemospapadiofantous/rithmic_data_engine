-- 014_paper_engine_runs.sql — one row per paper_engine process start (2026-09-30).
--
-- Written by paper_main on "Fleet ready" (paper_db.cpp record_engine_run, never throws; also
-- created by PaperDb::ensure_schema so a fresh box needs no manual step). strategy_leaderboard()
-- reads it for `restart_sessions`: a session on which the engine started between 03:00 ET and the
-- end of the ranked slot began cold for bar-history engines (mtf_scalper needs 1500 completed
-- 1m bars) — until the start-up warm-up shipped the same day, and still a flag afterwards
-- because entries are suppressed while the engine catches up.
-- History before the table existed is seeded from data/logs/paper_engine*.log "Fleet ready" lines
-- (scripts: see CHANGES.md 2026-09-30); warmup_from is NULL for those rows.
CREATE TABLE IF NOT EXISTS paper_engine_runs (
  started_at    TIMESTAMPTZ NOT NULL,
  account_label TEXT        NOT NULL,
  strategies    INTEGER     NOT NULL DEFAULT 0,
  warmup_from   TIMESTAMPTZ,
  UNIQUE (started_at, account_label)
);
CREATE INDEX IF NOT EXISTS idx_paper_engine_runs_label_started ON paper_engine_runs (account_label, started_at);
