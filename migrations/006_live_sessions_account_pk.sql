-- 006_live_sessions_account_pk.sql
-- Promote live_sessions primary key to include account_label so two accounts
-- trading the same instrument/strategy on the same day no longer collide.
-- Old PK:  (session_date, instrument, strategy)
-- New PK:  (session_date, account_label, instrument, strategy)
-- Run once on Oracle VM PostgreSQL. Safe to run multiple times (DO $$ guard).
-- Online-safe: reuses the existing unique index when present, so no table
-- rewrite and only a brief lock; live_writes from the collector are unaffected.

DO $$
BEGIN
    -- Only act if the current PK does not already include account_label.
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint c
        JOIN pg_attribute a ON a.attrelid = c.conrelid AND a.attnum = ANY(c.conkey)
        WHERE c.conrelid = 'live_sessions'::regclass
          AND c.contype = 'p'
          AND a.attname = 'account_label'
    ) THEN
        -- Ensure account_label exists and is NOT NULL before it joins the PK.
        ALTER TABLE live_sessions ADD COLUMN IF NOT EXISTS account_label TEXT NOT NULL DEFAULT 'legends';

        -- Drop the old PK if present (any definition).
        IF EXISTS (
            SELECT 1 FROM pg_constraint
            WHERE conrelid = 'live_sessions'::regclass AND contype = 'p'
        ) THEN
            ALTER TABLE live_sessions DROP CONSTRAINT live_sessions_pkey;
        END IF;

        -- Reuse the pre-existing unique index if it covers the new key columns;
        -- otherwise build the PK index directly.
        IF EXISTS (
            SELECT 1 FROM pg_indexes
            WHERE schemaname = 'public'
              AND tablename = 'live_sessions'
              AND indexname = 'live_sessions_acct_inst_strat_idx'
        ) THEN
            ALTER TABLE live_sessions
                ADD PRIMARY KEY USING INDEX live_sessions_acct_inst_strat_idx;
        ELSE
            ALTER TABLE live_sessions
                ADD PRIMARY KEY (session_date, account_label, instrument, strategy);
        END IF;
    END IF;
END
$$;
