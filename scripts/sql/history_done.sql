-- history_done.sql — which history replays are COMPLETE, and the trades of only those.
--
-- The history queue writes trades while a month replays (6 h for an NQ MBO month); a report that reads
-- paper_trades directly counts half-replayed months as if they were whole. scripts/history_done_sync.sh
-- mirrors the queue's done markers (data/history/done/<market>/<M>.done) into history_done; hist_trades
-- returns the history rows whose (strategy, UTC month) belongs to a finished job:
--   nq_hist        main fleet ids (registered under 'tradeify', not __lv_) need market nq,
--                  everything else (night grid, staged winners/new modes, __lv_ variants) needs nqx
--   nq_bars        nqbars        nq_barcheck  nqbcheck      nq_combo_hist  nqcombo
--   es_hist        es            es_bars      esbars
--   crypto_hist    btc_* / eth_*: market btc / eth; a __lv_ variant needs btclv / ethlv, or a btc / eth
--                  job that finished after the variant was registered
-- Apply: psql -f scripts/sql/history_done.sql (idempotent).
CREATE TABLE IF NOT EXISTS history_done (
    market   text        NOT NULL,
    month    text        NOT NULL,          -- YYYY-MM, UTC month window
    trades   bigint,
    finished timestamptz,
    PRIMARY KEY (market, month)
);

CREATE OR REPLACE VIEW hist_trades AS
SELECT p.*
FROM paper_trades p
JOIN paper_strategies s USING (strategy_id)
WHERE p.account_label IN ('nq_hist', 'nq_bars', 'nq_barcheck', 'nq_combo_hist', 'es_hist', 'es_bars', 'crypto_hist')
  AND EXISTS (
    SELECT 1 FROM history_done d
    WHERE d.month = to_char(p.entry_time AT TIME ZONE 'UTC', 'YYYY-MM')
      AND CASE p.account_label
            WHEN 'nq_hist' THEN d.market = CASE WHEN s.account_label = 'tradeify'
                                                 AND p.strategy_id NOT LIKE '%\_\_lv\_%' THEN 'nq' ELSE 'nqx' END
            WHEN 'nq_bars'       THEN d.market = 'nqbars'
            WHEN 'nq_barcheck'   THEN d.market = 'nqbcheck'
            WHEN 'nq_combo_hist' THEN d.market = 'nqcombo'
            WHEN 'es_hist'       THEN d.market = 'es'
            WHEN 'es_bars'       THEN d.market = 'esbars'
            WHEN 'crypto_hist'   THEN
                 CASE WHEN p.strategy_id LIKE '%\_\_lv\_%'
                      THEN d.market = split_part(p.strategy_id, '_', 1) || 'lv'
                        OR (d.market = split_part(p.strategy_id, '_', 1) AND d.finished > s.created_at)
                      ELSE d.market = split_part(p.strategy_id, '_', 1) END
          END
  );

-- session_trend — per session, is the RTH close above the average of the last 50 RTH closes (incl. this one)?
-- Refreshed by history_done_sync.sh (one window pass over session_stats). Read through hist_trades_regime.trend_b,
-- which takes the latest session CLOSED before the entry (no look-ahead).
CREATE TABLE IF NOT EXISTS session_trend (
    symbol       text NOT NULL,
    session_date date NOT NULL,
    above_sma50  boolean,
    PRIMARY KEY (symbol, session_date)
);

-- hist_trades_regime — hist_trades plus the regime KNOWN AT ENTRY (no look-ahead) and the trading day.
--   trade_day = the CME / prop-firm trading day (18:00 ET → 17:00 ET), i.e. ET date of entry + 6 h
--   gap_b     = opening gap vs the prior-14-session ATR of the latest session whose 09:30 open is ≤ entry
--               (a 03:00 London entry gets YESTERDAY's gap, not today's)
--   prev_b    = 'prev_' || day_type of the latest session whose 16:00 close is ≤ entry
--   trend_b   = trend_up50 / trend_dn50: that same session's close above / below its 50-session average (session_trend)
-- session_stats symbol: NQ for nq_*, ES for es_*, BTCUSDT / ETHUSDT for crypto_hist.
CREATE OR REPLACE VIEW hist_trades_regime AS
SELECT t.*,
       ((t.entry_time AT TIME ZONE 'America/New_York') + interval '6 hours')::date AS trade_day,
       g.gap_b, pv.prev_b, tr.trend_b
FROM hist_trades t
CROSS JOIN LATERAL (SELECT CASE WHEN t.account_label LIKE 'es%' THEN 'ES'
                                WHEN t.account_label = 'crypto_hist' THEN upper(split_part(t.strategy_id, '_', 1)) || 'USDT'
                                ELSE 'NQ' END AS sym) y
LEFT JOIN LATERAL (
    SELECT CASE WHEN s.atr14_pts IS NULL OR s.atr14_pts = 0 OR s.gap_pts IS NULL THEN NULL
                WHEN abs(s.gap_pts) / s.atr14_pts < 0.1 THEN 'gap_small'
                WHEN abs(s.gap_pts) / s.atr14_pts < 0.3 THEN 'gap_mid' ELSE 'gap_big' END AS gap_b
    FROM session_stats s
    WHERE s.symbol = y.sym
      AND s.session_date <= ((t.entry_time AT TIME ZONE 'America/New_York') - interval '9 hours 30 minutes')::date
    ORDER BY s.session_date DESC LIMIT 1) g ON true
LEFT JOIN LATERAL (
    SELECT 'prev_' || s.day_type AS prev_b
    FROM session_stats s
    WHERE s.symbol = y.sym AND s.day_type IS NOT NULL
      AND s.session_date <= ((t.entry_time AT TIME ZONE 'America/New_York') - interval '16 hours')::date
    ORDER BY s.session_date DESC LIMIT 1) pv ON true
LEFT JOIN LATERAL (
    SELECT CASE WHEN st.above_sma50 THEN 'trend_up50' WHEN NOT st.above_sma50 THEN 'trend_dn50' END AS trend_b
    FROM session_trend st
    WHERE st.symbol = y.sym
      AND st.session_date <= ((t.entry_time AT TIME ZONE 'America/New_York') - interval '16 hours')::date
    ORDER BY st.session_date DESC LIMIT 1) tr ON true;

-- history_bar_reliability — which strategies replay the same from 1-minute bars as from MBO ticks
-- (scripts/bars_vs_ticks.sh, from the nqbcheck months). Results on the 2010–2023 bars (label nq_bars) are used
-- only for strategies marked reliable here; with no rows, no bars result is used.
CREATE TABLE IF NOT EXISTS history_bar_reliability (
    strategy_id text PRIMARY KEY,
    months      int,
    ticks_n     int,
    bars_n      int,
    ticks_net   double precision,
    bars_net    double precision,
    day_corr    double precision,
    reliable    boolean NOT NULL,
    reason      text,
    updated     timestamptz NOT NULL DEFAULT now()
);
