-- bars_1m.sql — 1-minute OHLCV rollup of the collector's `ticks`, per symbol.
--
-- Why: the dashboard's /chart, /models and /insights pages re-aggregated raw ticks into bars
-- on every request (350k rows per RTH day; 2.1M rows for a 30-day models view). Reading this
-- table instead costs ~390 rows per day. Any timeframe (5m, 15m, 1h, …) aggregates from these
-- bars exactly (open = first bar's open, close = last bar's close, high/low/volume = max/min/sum).
--
-- Maintenance: `SELECT refresh_bars_1m();` upserts every minute from the last stored minute
-- (recomputed, so late ticks in it are folded in) up to the last COMPLETED minute. Idempotent,
-- incremental, a few ms per call once backfilled. Run every minute by bars-1m.timer
-- (deploy/bars-1m.{service,timer} → scripts/refresh_bars.sh). The in-progress minute is NOT
-- here — readers that need it take it from `ticks` (WHERE ts_event >= date_trunc('minute', now())).
-- Apply (idempotent): psql -f scripts/sql/bars_1m.sql
CREATE TABLE IF NOT EXISTS bars_1m (
    symbol      text        NOT NULL,
    minute      timestamptz NOT NULL,          -- bar open time, UTC minute
    open        double precision NOT NULL,
    high        double precision NOT NULL,
    low         double precision NOT NULL,
    close       double precision NOT NULL,
    volume      bigint      NOT NULL,
    buy_volume  bigint      NOT NULL,          -- aggressor-buy volume (ticks.is_buy) → delta = 2·buy − volume
    ticks       integer     NOT NULL,
    PRIMARY KEY (symbol, minute)
);
CREATE INDEX IF NOT EXISTS idx_bars_1m_minute ON bars_1m (minute DESC);

CREATE OR REPLACE FUNCTION refresh_bars_1m(p_symbol text DEFAULT NULL) RETURNS integer
LANGUAGE plpgsql AS $$
DECLARE
    s        record;
    m0       timestamptz;
    m_end    timestamptz := date_trunc('minute', now());   -- exclusive: the in-progress minute is left out
    n        integer := 0;
    cnt      integer;
BEGIN
    -- Distinct symbols via a loose index scan on idx_ticks_symbol_ts (O(#symbols) index probes);
    -- a plain DISTINCT over ticks cost ~450 ms of every refresh.
    FOR s IN WITH RECURSIVE syms AS (
                 (SELECT t.symbol FROM ticks t ORDER BY t.symbol LIMIT 1)
                 UNION ALL
                 SELECT (SELECT t.symbol FROM ticks t WHERE t.symbol > y.symbol ORDER BY t.symbol LIMIT 1)
                 FROM syms y WHERE y.symbol IS NOT NULL)
             SELECT y.symbol FROM syms y WHERE y.symbol IS NOT NULL AND (p_symbol IS NULL OR y.symbol = p_symbol) LOOP
        SELECT COALESCE((SELECT max(b.minute) FROM bars_1m b WHERE b.symbol = s.symbol),
                        (SELECT date_trunc('minute', min(t.ts_event)) FROM ticks t WHERE t.symbol = s.symbol))
          INTO m0;
        IF m0 IS NULL OR m0 >= m_end THEN CONTINUE; END IF;
        INSERT INTO bars_1m (symbol, minute, open, high, low, close, volume, buy_volume, ticks)
        SELECT s.symbol, date_trunc('minute', t.ts_event),
               (array_agg(t.price ORDER BY t.ts_event, t.seq))[1],
               max(t.price), min(t.price),
               (array_agg(t.price ORDER BY t.ts_event DESC, t.seq DESC))[1],
               sum(t.size)::bigint,
               COALESCE(sum(t.size) FILTER (WHERE t.is_buy), 0)::bigint,
               count(*)::integer
        FROM ticks t
        WHERE t.symbol = s.symbol AND t.ts_event >= m0 AND t.ts_event < m_end
        GROUP BY 2
        ON CONFLICT (symbol, minute) DO UPDATE SET
            open = EXCLUDED.open, high = EXCLUDED.high, low = EXCLUDED.low, close = EXCLUDED.close,
            volume = EXCLUDED.volume, buy_volume = EXCLUDED.buy_volume, ticks = EXCLUDED.ticks;
        GET DIAGNOSTICS cnt = ROW_COUNT;
        n := n + cnt;
    END LOOP;
    RETURN n;
END $$;
