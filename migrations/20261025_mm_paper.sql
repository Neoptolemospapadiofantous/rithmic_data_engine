-- 20261025_mm_paper.sql — results of the 24/7 paper market maker on Lighter (src/dex/mm_paper_main.cpp,
-- which also creates these tables IF NOT EXISTS on connect). Paper only: no order ever reaches an exchange;
-- fills come from the queue model in src/dex/hl_mm.hpp.
--
-- mm_paper_fills    every simulated fill; taker = a flatten (day roll, loss stop, kill switch), not a quote fill
-- mm_paper_equity   once a minute per market (and market '_total'): equity since 00:00 UTC, inventory, fills
-- mm_paper_daily    one row per UTC day × market after the day roll (flat-to-flat, like the backtests)
-- mm_paper_events   risk and lifecycle events: start, stale, fresh, loss_stop, kill, resume, day_roll, gap

CREATE TABLE IF NOT EXISTS mm_paper_fills (
    id         BIGSERIAL PRIMARY KEY,
    ts         TIMESTAMPTZ NOT NULL,
    market     TEXT NOT NULL,
    side       SMALLINT NOT NULL,          -- +1 bought, -1 sold
    price      DOUBLE PRECISION NOT NULL,
    qty        DOUBLE PRECISION NOT NULL,
    mid        DOUBLE PRECISION NOT NULL,
    taker      BOOLEAN NOT NULL,
    inv_after  DOUBLE PRECISION NOT NULL,
    run_id     TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS mm_paper_fills_ts ON mm_paper_fills (ts);

CREATE TABLE IF NOT EXISTS mm_paper_equity (
    ts      TIMESTAMPTZ NOT NULL,
    market  TEXT NOT NULL,
    equity  DOUBLE PRECISION NOT NULL,
    inv     DOUBLE PRECISION NOT NULL,
    fills   INTEGER NOT NULL,
    halted  BOOLEAN NOT NULL,
    PRIMARY KEY (ts, market)
);

CREATE TABLE IF NOT EXISTS mm_paper_daily (
    day          DATE NOT NULL,
    market       TEXT NOT NULL,
    pnl          DOUBLE PRECISION NOT NULL,
    volume       DOUBLE PRECISION NOT NULL,
    fills        INTEGER NOT NULL,
    flattens     INTEGER NOT NULL,
    capture_bps  DOUBLE PRECISION NOT NULL,
    mk10_bps     DOUBLE PRECISION NOT NULL,
    max_dd       DOUBLE PRECISION NOT NULL,
    PRIMARY KEY (day, market)
);

CREATE TABLE IF NOT EXISTS mm_paper_events (
    id      BIGSERIAL PRIMARY KEY,
    ts      TIMESTAMPTZ NOT NULL,
    kind    TEXT NOT NULL,
    market  TEXT NOT NULL,
    detail  TEXT NOT NULL
);
