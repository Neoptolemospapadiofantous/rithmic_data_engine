-- bars_5m / bars_15m / bars_1h (2026-09-26, TODO.md §3): higher timeframes derived EXACTLY from
-- bars_1m — no page or model aggregates ticks again. Views, not tables: bars_1m is ~11k rows
-- and grows 780/day per symbol, so aggregating on read is ~1 ms and never stale. Buckets are
-- aligned to the clock (xx:00/05/10…, xx:00/15/30/45, hour), the same alignment the trend
-- engine's tf aggregation uses, so a strategy's "5-minute bar" and the chart's agree.

CREATE OR REPLACE VIEW bars_5m AS
SELECT symbol,
       to_timestamp(floor(extract(epoch FROM minute) / 300) * 300) AS bucket,
       (array_agg(open  ORDER BY minute))[1]      AS open,
       max(high)                                  AS high,
       min(low)                                   AS low,
       (array_agg(close ORDER BY minute DESC))[1] AS close,
       sum(volume)                                AS volume,
       sum(buy_volume)                            AS buy_volume,
       sum(ticks)                                 AS ticks,
       count(*)                                   AS minutes
FROM bars_1m GROUP BY symbol, 2;

CREATE OR REPLACE VIEW bars_15m AS
SELECT symbol,
       to_timestamp(floor(extract(epoch FROM minute) / 900) * 900) AS bucket,
       (array_agg(open  ORDER BY minute))[1]      AS open,
       max(high)                                  AS high,
       min(low)                                   AS low,
       (array_agg(close ORDER BY minute DESC))[1] AS close,
       sum(volume)                                AS volume,
       sum(buy_volume)                            AS buy_volume,
       sum(ticks)                                 AS ticks,
       count(*)                                   AS minutes
FROM bars_1m GROUP BY symbol, 2;

CREATE OR REPLACE VIEW bars_1h AS
SELECT symbol,
       date_trunc('hour', minute)                 AS bucket,
       (array_agg(open  ORDER BY minute))[1]      AS open,
       max(high)                                  AS high,
       min(low)                                   AS low,
       (array_agg(close ORDER BY minute DESC))[1] AS close,
       sum(volume)                                AS volume,
       sum(buy_volume)                            AS buy_volume,
       sum(ticks)                                 AS ticks,
       count(*)                                   AS minutes
FROM bars_1m GROUP BY symbol, 2;
