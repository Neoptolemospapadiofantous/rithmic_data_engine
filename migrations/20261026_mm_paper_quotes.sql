-- 20261026_mm_paper_quotes.sql — what the paper market maker is quoting on each market, and why (observability,
-- 2026-10-26). Written by src/dex/mm_paper_main.cpp (which also creates the table IF NOT EXISTS on connect, under the
-- writer's 2 s lock_timeout + reconnect backoff): a row when (bid_px, ask_px, bid_reason, ask_reason) changes, at most
-- one row per second per market, plus a keepalive row every 60 s (mmpaper::QuoteTracker in src/dex/mm_paper.hpp).
-- Quote rows have their OWN bounded writer queue (50,000 rows, oldest shed first) and their OWN drop ledger
-- (data/mm_paper/db_quote_drops_<label>.json, status.json db.quote_*), which scripts/mm_gates.sh does not read: an
-- outage that sheds quote rows never costs a fill/event row or a G2 day. Inserts that hit an existing (label, market, ts)
-- are counted as db.quote_conflicts. No pruning (founder: keep data).
--
-- ts          exchange time of the event that produced the decision (1 s tick rows: wall clock − the re-order hold)
-- label       engine instance ('paper' 300 ms official, 'paper_220' shadow)
-- bid_px/ask_px  the live paper order's price; NULL = no order on that side
-- bid_reason/ask_reason  hl::reason_name: quoting | no_book | halted | spread_narrow | jump_pause | imbalance_pull |
--             inv_cap | kept (kept = an order kept at its old price by the requote hysteresis)
-- inv         position in coins; inv_usd at the mid; equity = the market's P&L since 00:00 UTC (or the engine start)
CREATE TABLE IF NOT EXISTS mm_paper_quotes (
    ts          TIMESTAMPTZ NOT NULL,
    label       TEXT NOT NULL,
    market      TEXT NOT NULL,
    mid         DOUBLE PRECISION NOT NULL,
    best_bid    DOUBLE PRECISION NOT NULL,
    best_ask    DOUBLE PRECISION NOT NULL,
    bid_px      DOUBLE PRECISION NULL,
    ask_px      DOUBLE PRECISION NULL,
    bid_reason  TEXT NOT NULL,
    ask_reason  TEXT NOT NULL,
    inv         DOUBLE PRECISION NOT NULL,
    inv_usd     DOUBLE PRECISION NOT NULL,
    equity      DOUBLE PRECISION NOT NULL,
    PRIMARY KEY (label, market, ts)
);
