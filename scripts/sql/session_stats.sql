-- session_stats (2026-09-26, TODO.md §3): one row per symbol per New York trading day, built
-- from bars_1m. What a day looked like, so the models and the rotation can condition on it
-- (e.g. refuse to promote a strategy that only won on trend days).
--
-- Columns are RTH (09:30–16:00 ET) unless prefixed. gap_pts = today's 09:30 open minus the
-- previous session's 16:00 close (from bars_1m too, so the first stored day has NULL gap).
-- atr14_pts = mean RTH range of up to the prior 14 sessions (NULL on the first stored day;
-- read it as provisional until 14 sessions exist). vol_vs_20d = today's RTH volume / mean of
-- up to the prior 20 sessions, same caveat.
-- day_type is a label, not a model: close position in the range (>0.7 trend_up, <0.3 trend_down,
-- else range) with the body/range ratio as the second check; 'short' when the RTH had < 300
-- one-minute bars (holiday, feed gap) so a partial day never reads as a quiet day.
--
-- refresh_session_stats(p_days) recomputes the last p_days sessions (default 3) — cheap on
-- bars_1m (390 rows a day) and idempotent; run it from the same timer as refresh_bars_1m().

CREATE TABLE IF NOT EXISTS session_stats (
    symbol          text NOT NULL,
    session_date    date NOT NULL,           -- New York date
    bars            int,                     -- one-minute bars seen 09:30–16:00
    rth_open        double precision,
    rth_high        double precision,
    rth_low         double precision,
    rth_close       double precision,
    range_pts       double precision,
    body_pts        double precision,        -- close - open (signed)
    gap_pts         double precision,        -- open - prior close (signed)
    orb5_high       double precision,        -- 09:30–09:34 bars
    orb5_low        double precision,
    orb5_range_pts  double precision,
    first_hour_range_pts double precision,   -- 09:30–10:29
    volume          bigint,
    buy_volume      bigint,
    delta_share     double precision,        -- buy_volume / volume
    atr14_pts       double precision,        -- prior 14 sessions' mean range
    vol_vs_20d      double precision,
    range_vs_atr    double precision,        -- range_pts / atr14_pts
    day_type        text,                    -- trend_up | trend_down | range | short
    refreshed_at    timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (symbol, session_date)
);

CREATE OR REPLACE FUNCTION refresh_session_stats(p_days int DEFAULT 3) RETURNS int
LANGUAGE plpgsql AS $$
DECLARE n int;
BEGIN
  WITH b AS (
    SELECT symbol,
           (minute AT TIME ZONE 'America/New_York')::date AS d,
           (minute AT TIME ZONE 'America/New_York')::time AS t,
           minute, open, high, low, close, volume, buy_volume
    FROM bars_1m
    WHERE minute >= (current_date - p_days - 2)::timestamp AT TIME ZONE 'America/New_York'
      AND (minute AT TIME ZONE 'America/New_York')::time >= '09:30'
      AND (minute AT TIME ZONE 'America/New_York')::time <  '16:00'
  ), day AS (
    SELECT symbol, d,
           count(*)                                            AS bars,
           (array_agg(open  ORDER BY minute))[1]               AS rth_open,
           max(high)                                           AS rth_high,
           min(low)                                            AS rth_low,
           (array_agg(close ORDER BY minute DESC))[1]          AS rth_close,
           max(high) FILTER (WHERE t < '09:35')                AS orb5_high,
           min(low)  FILTER (WHERE t < '09:35')                AS orb5_low,
           max(high) FILTER (WHERE t < '10:30') - min(low) FILTER (WHERE t < '10:30') AS fh_range,
           sum(volume)                                         AS volume,
           sum(buy_volume)                                     AS buy_volume
    FROM b GROUP BY symbol, d
  ), hist AS (   -- prior sessions already stored, for ATR / volume baselines and the gap
    SELECT symbol, session_date, range_pts, volume, rth_close FROM session_stats
    UNION
    SELECT symbol, d, rth_high - rth_low, volume, rth_close FROM day
  ), enriched AS (
    SELECT d.*,
           (SELECT avg(h.range_pts) FROM (SELECT range_pts FROM hist h WHERE h.symbol = d.symbol AND h.session_date < d.d ORDER BY h.session_date DESC LIMIT 14) h) AS atr14,
           (SELECT avg(h.volume)    FROM (SELECT volume    FROM hist h WHERE h.symbol = d.symbol AND h.session_date < d.d ORDER BY h.session_date DESC LIMIT 20) h) AS vol20,
           (SELECT h.rth_close FROM hist h WHERE h.symbol = d.symbol AND h.session_date < d.d ORDER BY h.session_date DESC LIMIT 1) AS prior_close
    FROM day d
  ), ins AS (
    INSERT INTO session_stats AS s (symbol, session_date, bars, rth_open, rth_high, rth_low, rth_close,
        range_pts, body_pts, gap_pts, orb5_high, orb5_low, orb5_range_pts, first_hour_range_pts,
        volume, buy_volume, delta_share, atr14_pts, vol_vs_20d, range_vs_atr, day_type, refreshed_at)
    SELECT symbol, d, bars, rth_open, rth_high, rth_low, rth_close,
           rth_high - rth_low,
           rth_close - rth_open,
           rth_open - prior_close,
           orb5_high, orb5_low, orb5_high - orb5_low, fh_range,
           volume, buy_volume,
           CASE WHEN volume > 0 THEN buy_volume::double precision / volume END,
           atr14,
           CASE WHEN vol20 > 0 THEN volume / vol20 END,
           CASE WHEN atr14 > 0 THEN (rth_high - rth_low) / atr14 END,
           CASE WHEN bars < 300 THEN 'short'
                WHEN rth_high - rth_low <= 0 THEN 'range'
                WHEN (rth_close - rth_low) / (rth_high - rth_low) > 0.7 AND abs(rth_close - rth_open) / (rth_high - rth_low) > 0.4 THEN 'trend_up'
                WHEN (rth_close - rth_low) / (rth_high - rth_low) < 0.3 AND abs(rth_close - rth_open) / (rth_high - rth_low) > 0.4 THEN 'trend_down'
                ELSE 'range' END,
           now()
    FROM enriched
    ON CONFLICT (symbol, session_date) DO UPDATE SET
        bars = EXCLUDED.bars, rth_open = EXCLUDED.rth_open, rth_high = EXCLUDED.rth_high,
        rth_low = EXCLUDED.rth_low, rth_close = EXCLUDED.rth_close, range_pts = EXCLUDED.range_pts,
        body_pts = EXCLUDED.body_pts, gap_pts = EXCLUDED.gap_pts, orb5_high = EXCLUDED.orb5_high,
        orb5_low = EXCLUDED.orb5_low, orb5_range_pts = EXCLUDED.orb5_range_pts,
        first_hour_range_pts = EXCLUDED.first_hour_range_pts, volume = EXCLUDED.volume,
        buy_volume = EXCLUDED.buy_volume, delta_share = EXCLUDED.delta_share,
        atr14_pts = EXCLUDED.atr14_pts, vol_vs_20d = EXCLUDED.vol_vs_20d,
        range_vs_atr = EXCLUDED.range_vs_atr, day_type = EXCLUDED.day_type, refreshed_at = now()
    RETURNING 1
  )
  SELECT count(*) INTO n FROM ins;
  RETURN n;
END $$;
