-- 015_hedge_pairs.sql
-- Cross-account hedging: bind an ANCHOR account to a HEDGE account so the
-- hedge_coordinator keeps the hedge account's net position at
-- round(-anchor_net * ratio), clamped to ±max_qty.
--
-- hedge_pairs     — the bindings (managed from the dashboard Hedges page)
-- hedge_status    — per-pair live state written by hedge_coordinator
-- live_account_net — latest exchange-reported net qty / balance / day P&L per
--                    account, upserted by nq_executor on every tid=451 update;
--                    this is the coordinator's anchor feed.
--
-- Run once on the Oracle VM PostgreSQL database.
-- Safe to run multiple times (IF NOT EXISTS guards each statement).

CREATE TABLE IF NOT EXISTS hedge_pairs (
    id              SERIAL PRIMARY KEY,
    anchor_label    TEXT NOT NULL,
    hedge_label     TEXT NOT NULL,
    symbol          TEXT NOT NULL DEFAULT 'MNQ',
    ratio           DOUBLE PRECISION NOT NULL DEFAULT 1.0,
    max_qty         INT NOT NULL DEFAULT 10,
    enabled         BOOLEAN NOT NULL DEFAULT FALSE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (anchor_label, hedge_label, symbol),
    CHECK (anchor_label <> hedge_label),
    CHECK (ratio > 0),
    CHECK (max_qty > 0)
);

CREATE TABLE IF NOT EXISTS hedge_status (
    pair_id             INT PRIMARY KEY REFERENCES hedge_pairs(id) ON DELETE CASCADE,
    anchor_net          INT NOT NULL DEFAULT 0,
    hedge_net           INT NOT NULL DEFAULT 0,
    target_net          INT NOT NULL DEFAULT 0,
    anchor_feed_age_ms  INT,
    last_action         TEXT,
    last_error          TEXT,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS live_account_net (
    account_label   TEXT PRIMARY KEY,
    net_qty         INT NOT NULL DEFAULT 0,
    balance         DOUBLE PRECISION,
    day_pnl         DOUBLE PRECISION,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
