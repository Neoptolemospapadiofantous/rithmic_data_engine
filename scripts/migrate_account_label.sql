-- Migration: add account_label to all 3 live tables
-- Run once on Oracle: PGPASSWORD=testpass123 psql -h 127.0.0.1 -U rithmic_user -d rithmic -f scripts/migrate_account_label.sql

ALTER TABLE live_trades   ADD COLUMN IF NOT EXISTS account_label TEXT NOT NULL DEFAULT 'legends';
ALTER TABLE live_sessions ADD COLUMN IF NOT EXISTS account_label TEXT NOT NULL DEFAULT 'legends';
ALTER TABLE live_position ADD COLUMN IF NOT EXISTS account_label TEXT NOT NULL DEFAULT 'legends';

-- live_sessions: drop old PK and add new composite PK with account_label
ALTER TABLE live_sessions DROP CONSTRAINT IF EXISTS live_sessions_pkey;
ALTER TABLE live_sessions ADD PRIMARY KEY (session_date, account_label, instrument, strategy);

-- live_position: drop old unique index, add new one with account_label
DROP INDEX IF EXISTS live_position_inst_strat_idx;
CREATE UNIQUE INDEX IF NOT EXISTS live_position_acct_inst_strat_idx
  ON live_position(session_date, account_label, instrument, strategy);
