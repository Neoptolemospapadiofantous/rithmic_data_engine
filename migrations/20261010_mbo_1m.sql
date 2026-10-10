-- 20261010_mbo_1m.sql — per-minute market-by-order summary written by crypto_collector
-- (src/crypto/crypto_collector_main.cpp creates the same table IF NOT EXISTS on connect).
--
-- One row per venue × symbol × UTC minute, computed from the collector's in-memory order
-- book (REST snapshot + live_orders events). The raw events are archived forever in
-- data/mbo/<venue>/<pair>/<YYYY-MM-DD>.jsonl.zst — this table can be rebuilt from them.
--
-- venue                  'BITSTAMP'
-- symbol                 'BTCUSD' / 'ETHUSD'
-- ts_minute              start of the minute (UTC)
-- adds                   order_created events in the minute
-- changes                order_changed events (partial fills / amendments)
-- deletes                order_deleted events (cancels + fills)
-- executed_deletes       deletes of fully filled orders (event amount 0; amount_traded = amount_at_create)
-- add_qty                base-currency quantity added (sum of order_created amounts)
-- delete_qty             base-currency quantity removed by deletes (remaining size held in the book)
-- bid_orders/ask_orders  resting orders per side at the end of the minute
-- bid_qty_top10bps       resting bid quantity priced within 10 bps (0.1 %) below mid, end of minute
-- ask_qty_top10bps       resting ask quantity priced within 10 bps above mid, end of minute
-- best_bid/best_ask      top of book at the end of the minute (NULL = side empty)
-- order_count_imbalance  (bid_orders − ask_orders) / (bid_orders + ask_orders), −1..1
-- qty_imbalance          (bid_qty_top10bps − ask_qty_top10bps) / their sum, −1..1
-- cancel_to_add_ratio    (deletes − executed_deletes) / adds; NULL when adds = 0
-- avg_order_qty          add_qty / adds; NULL when adds = 0
-- complete               false when the book was not synced for the whole minute
--                        (first minute after start, or a reconnect/resync inside it)

CREATE TABLE IF NOT EXISTS mbo_1m (
    venue                 VARCHAR(16)      NOT NULL,
    symbol                VARCHAR(16)      NOT NULL,
    ts_minute             TIMESTAMPTZ      NOT NULL,
    adds                  INTEGER          NOT NULL,
    changes               INTEGER          NOT NULL,
    deletes               INTEGER          NOT NULL,
    executed_deletes      INTEGER          NOT NULL,
    add_qty               DOUBLE PRECISION NOT NULL,
    delete_qty            DOUBLE PRECISION NOT NULL,
    bid_orders            INTEGER          NOT NULL,
    ask_orders            INTEGER          NOT NULL,
    bid_qty_top10bps      DOUBLE PRECISION NOT NULL,
    ask_qty_top10bps      DOUBLE PRECISION NOT NULL,
    best_bid              DOUBLE PRECISION,
    best_ask              DOUBLE PRECISION,
    order_count_imbalance DOUBLE PRECISION NOT NULL,
    qty_imbalance         DOUBLE PRECISION NOT NULL,
    cancel_to_add_ratio   DOUBLE PRECISION,
    avg_order_qty         DOUBLE PRECISION,
    complete              BOOLEAN          NOT NULL DEFAULT TRUE,
    PRIMARY KEY (venue, symbol, ts_minute)
);
