-- book_1m (2026-09-26, TODO.md §3): the quote stream (bbo) rolled up per symbol per minute,
-- the way bars_1m rolls up ticks. Makes the order-book overlays' inputs chartable and testable
-- by time of day — the `__inv` gate (imbalance_max, fade the stacked side) is the fleet's one
-- book edge and until now nothing could show WHEN the book is stacked.
--
-- imbalance = bid_size / (bid_size + ask_size) per quote; spread in points.
-- bid_heavy_pct / ask_heavy_pct = share of the minute's quotes with imbalance ≥ 0.65 / ≤ 0.35
-- (the gate's thresholds live in the strategies; these are descriptive).
--
-- refresh_book_1m(symbol) is incremental like refresh_bars_1m(): it recomputes from the last
-- stored minute (inclusive, in case that minute was partial) up to the last COMPLETED minute.

CREATE TABLE IF NOT EXISTS book_1m (
    symbol          text NOT NULL,
    minute          timestamptz NOT NULL,
    quotes          int NOT NULL,
    avg_spread_pts  double precision,
    max_spread_pts  double precision,
    avg_imbalance   double precision,
    bid_heavy_pct   double precision,
    ask_heavy_pct   double precision,
    avg_bid_size    double precision,
    avg_ask_size    double precision,
    mid_open        double precision,
    mid_close       double precision,
    PRIMARY KEY (symbol, minute)
);

CREATE OR REPLACE FUNCTION refresh_book_1m(p_symbol text) RETURNS int
LANGUAGE plpgsql AS $$
DECLARE
  v_from timestamptz;
  v_to   timestamptz := date_trunc('minute', now());   -- current minute is still open
  n int;
BEGIN
  SELECT coalesce(max(minute), '-infinity') INTO v_from FROM book_1m WHERE symbol = p_symbol;
  IF v_from = '-infinity' THEN
    SELECT date_trunc('minute', min(ts_event)) INTO v_from FROM bbo WHERE symbol = p_symbol;
    IF v_from IS NULL THEN RETURN 0; END IF;
  END IF;

  WITH q AS (
    SELECT date_trunc('minute', ts_event) AS m, ts_event,
           bid_price, ask_price, bid_size, ask_size,
           ask_price - bid_price AS spread,
           CASE WHEN bid_size + ask_size > 0 THEN bid_size::double precision / (bid_size + ask_size) END AS imb,
           (bid_price + ask_price) / 2.0 AS mid
    FROM bbo
    WHERE symbol = p_symbol AND ts_event >= v_from AND ts_event < v_to
      AND bid_price > 0 AND ask_price > 0
  ), agg AS (
    SELECT m, count(*) AS quotes, avg(spread), max(spread), avg(imb),
           avg(CASE WHEN imb >= 0.65 THEN 1.0 ELSE 0.0 END),
           avg(CASE WHEN imb <= 0.35 THEN 1.0 ELSE 0.0 END),
           avg(bid_size), avg(ask_size),
           (array_agg(mid ORDER BY ts_event))[1], (array_agg(mid ORDER BY ts_event DESC))[1]
    FROM q GROUP BY m
  ), ins AS (
    INSERT INTO book_1m (symbol, minute, quotes, avg_spread_pts, max_spread_pts, avg_imbalance,
                         bid_heavy_pct, ask_heavy_pct, avg_bid_size, avg_ask_size, mid_open, mid_close)
    SELECT p_symbol, * FROM agg
    ON CONFLICT (symbol, minute) DO UPDATE SET
      quotes = EXCLUDED.quotes, avg_spread_pts = EXCLUDED.avg_spread_pts,
      max_spread_pts = EXCLUDED.max_spread_pts, avg_imbalance = EXCLUDED.avg_imbalance,
      bid_heavy_pct = EXCLUDED.bid_heavy_pct, ask_heavy_pct = EXCLUDED.ask_heavy_pct,
      avg_bid_size = EXCLUDED.avg_bid_size, avg_ask_size = EXCLUDED.avg_ask_size,
      mid_open = EXCLUDED.mid_open, mid_close = EXCLUDED.mid_close
    RETURNING 1
  )
  SELECT count(*) INTO n FROM ins;
  RETURN n;
END $$;
