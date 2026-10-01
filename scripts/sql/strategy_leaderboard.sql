-- strategy_leaderboard.sql — ONE ranking for every strategy in the database.
--
-- Consumers: the dashboard's /strategies board (bot: ui/routers/cpp_engine.py,
-- /api/cpp/strategies/leaderboard) and scripts/rotate_handoff.sh (weekly rotation of the
-- ORB → hand-off engine). Both call this function so the board always shows exactly what
-- the rotation decides on. Apply (idempotent — the DROP is needed when the row type changes):
--   psql -h $PG_HOST -U $PG_USER -d $PG_DB -f scripts/sql/strategy_leaderboard.sql
--
-- Rows: every paper strategy (paper_trades, golden replays excluded) and every live
-- strategy tag ('<account>:<tag>' from live_trades, rehearsal *_dry* labels excluded) with
-- closed trades in the period. Period = last p_days days, or since p_since when given.
-- p_handoff_only restricts to trades ENTERED inside the hand-off slot (ET, [start, end)),
-- so candidates are ranked on the slot they would actually trade.
--
-- sharpe / sortino (2026-09-30): on the strategy's DAILY P&L series over the CALENDAR trading
-- sessions of the period (every session on which anything in the fleet traded; a day the strategy
-- sat out counts as 0), annualised by sqrt(252); NULL under 10 sessions, no variance, or (Sortino)
-- no losing day. Until 2026-09-30 only the strategy's own trading days counted, so a 3-day
-- strategy read Sharpe 17. Still noisy at 10 sessions — read next to `thin` / `positive_halves`.
--
-- 2026-09-30 additions (appended columns, nothing renamed — both consumers read by name):
--   restart_sessions  sessions in the period on which the paper engine (paper_engine_runs, this
--                     label) started between 03:00 ET and the slot end (p_handoff_only) / 16:00 ET.
--                     Bar-history engines began those sessions cold (no warm-up before 2026-09-30)
--                     and entries are suppressed during the warm-up since — the day is not a clean
--                     forward test. Live rows: 0.
--   trade_set_id      md5 of the strategy's (entry_time, direction) list over the period — variants
--                     that took exactly the same trades share one id (on 2026-09-29, 1,684 traders
--                     held only 1,003 distinct sets); trade_set_size = how many share it.
--   positive_halves   0/1/2: net > 0 in the first and/or second half of the period (split at the
--                     midpoint in time) — the cheapest persistence check.
--   scratch_rate      share of exits with reason 'breakeven' (49 % fleet-wide) — `wins` counts them.
-- 2026-10-01: instrument (appended column) — the contract the trades were on (paper_trades.symbol / live_trades.instrument:
--   MNQ, NQ, MES, …). Rows are now one per (source, strategy_id, instrument), so a strategy that traded two contracts
--   (e.g. live ORB: MNQ until 2026-09-29, NQ after) shows separate stats per contract instead of a blend of $2 and
--   $20-a-point trades. A paper strategy trades one symbol, so its row count is unchanged. Consumers that look a
--   strategy up by id take the first row (rotate_handoff.sh: LIMIT 1).
-- qualifies now ALSO requires profit_factor IS NOT NULL (a row with no losing trade has no PF and
-- used to pass the PF guardrail) and restart_sessions = 0.
--
-- live_runnable: the live executor can run this paper strategy in the slot on the pg feed —
-- engine trend (not rs_continuation, which needs the ES reference feed) with a window covering
-- the slot, or engine mtf_scalper (not smt / no reference feed) with a session_window covering
-- the slot. Book overlays are runnable since 2026-09-25 (the executor reads the collector's
-- bbo and applies the entry gates + book exits) EXCEPT the paper-fill ones: __sz (size on
-- agreement), __wait (fill timing), __bbe (BE on flip). ORB variants are never hand-off
-- candidates (ORB owns the open).
-- qualifies: live_runnable AND the guardrails (min trades / sessions / profit factor, net > 0).
-- thin: below the project's validation rule (30 trades, 2 sessions).
-- p_paper_label: which paper_trades label counts as "the fleet" (2026-09-26). The running fleet
-- writes 'tradeify'; replays write their own label ('audit' from the dashboard, 'backfill' for
-- the recorded-history replay, 'golden' for the regression). Until this parameter existed only
-- 'golden' was excluded, so an audit replay of a day DOUBLE-COUNTED that day for every strategy.
DROP FUNCTION IF EXISTS strategy_leaderboard(integer, boolean, date, time, time, integer, integer, double precision);
DROP FUNCTION IF EXISTS strategy_leaderboard(integer, boolean, date, time, time, integer, integer, double precision, text);
CREATE OR REPLACE FUNCTION strategy_leaderboard(
    p_days          integer DEFAULT 7,
    p_handoff_only  boolean DEFAULT true,
    p_since         date    DEFAULT NULL,
    p_slot_start    time    DEFAULT '10:00',
    p_slot_end      time    DEFAULT '12:00',
    p_min_trades    integer DEFAULT 20,
    p_min_sessions  integer DEFAULT 3,
    p_min_pf        double precision DEFAULT 1.0,
    p_paper_label   text    DEFAULT 'tradeify'
) RETURNS TABLE (
    source          text,
    strategy_id     text,
    engine          text,
    mode            text,
    base_id         text,
    overlay_tag     text,      -- "overlay" is reserved in PostgreSQL
    enabled         boolean,
    live_runnable   boolean,
    qualifies       boolean,
    trades          bigint,
    sessions        bigint,
    wins            bigint,
    net_pnl         double precision,
    gross_win       double precision,
    gross_loss      double precision,
    profit_factor   double precision,
    win_rate        double precision,
    avg_pnl         double precision,
    avg_qty         double precision,   -- contracts per trade (paper mtf variants size 3–16, ORB/trend 1)
    net_per_contract double precision,  -- net_pnl / Σqty — the size-neutral figure the ranking uses
    sharpe          double precision,
    sortino         double precision,
    thin            boolean,
    first_trade     timestamptz,
    last_trade      timestamptz,
    restart_sessions bigint,
    trade_set_id    text,
    trade_set_size  bigint,
    positive_halves integer,
    scratch_rate    double precision,
    instrument      text                -- contract traded: MNQ / NQ / MES / … (2026-10-01)
) LANGUAGE sql STABLE AS $$
WITH bounds AS (
    SELECT COALESCE(p_since::timestamptz, now() - make_interval(days => p_days)) AS t0,
           COALESCE(p_since::timestamptz, now() - make_interval(days => p_days))
             + (now() - COALESCE(p_since::timestamptz, now() - make_interval(days => p_days))) / 2 AS t_mid,
           (extract(hour FROM p_slot_start) * 100 + extract(minute FROM p_slot_start))::int AS slot_s,
           (extract(hour FROM p_slot_end)   * 100 + extract(minute FROM p_slot_end))::int   AS slot_e,
           CASE WHEN p_handoff_only THEN p_slot_end ELSE '16:00'::time END AS restart_edge
), pt AS (
    SELECT 'paper'::text AS source, t.strategy_id, t.entry_time, t.pnl_usd, t.qty, t.direction, t.exit_reason,
           t.symbol AS instrument
    FROM paper_trades t, bounds b
    WHERE t.account_label = p_paper_label AND t.exit_time IS NOT NULL AND t.entry_time >= b.t0
      AND (NOT p_handoff_only OR (
            (t.entry_time AT TIME ZONE 'America/New_York')::time >= p_slot_start AND
            (t.entry_time AT TIME ZONE 'America/New_York')::time <  p_slot_end))
), lt AS (
    SELECT 'live'::text AS source, t.account_label || ':' || t.strategy AS strategy_id,
           t.entry_time, t.pnl_usd, t.qty, t.direction, t.exit_reason, t.instrument AS instrument
    FROM live_trades t, bounds b
    WHERE t.exit_time IS NOT NULL AND t.entry_time >= b.t0
      AND t.account_label NOT LIKE '%\_dry%' AND t.account_label <> 'golden'
      AND (NOT p_handoff_only OR (
            (t.entry_time AT TIME ZONE 'America/New_York')::time >= p_slot_start AND
            (t.entry_time AT TIME ZONE 'America/New_York')::time <  p_slot_end))
), agg AS (
    SELECT * FROM pt UNION ALL SELECT * FROM lt
), fleet_sessions AS (      -- calendar trading sessions of the period: every ET date with any closed trade
    SELECT count(DISTINCT (a.entry_time AT TIME ZONE 'America/New_York')::date) AS n FROM agg a
), runs AS (                -- engine starts of this label that make a session a restart session
    SELECT (r.started_at AT TIME ZONE 'America/New_York')::date AS d
    FROM paper_engine_runs r, bounds b
    WHERE r.account_label = p_paper_label AND r.started_at >= b.t0 - interval '1 day'
      AND (r.started_at AT TIME ZONE 'America/New_York')::time >= '03:00'
      AND (r.started_at AT TIME ZONE 'America/New_York')::time <  b.restart_edge
    GROUP BY 1
), st AS (
    SELECT a.source, a.strategy_id, a.instrument,
           count(*)                                                              AS trades,
           count(DISTINCT (a.entry_time AT TIME ZONE 'America/New_York')::date)  AS sessions,
           count(DISTINCT (a.entry_time AT TIME ZONE 'America/New_York')::date)
               FILTER (WHERE a.source = 'paper'
                         AND (a.entry_time AT TIME ZONE 'America/New_York')::date IN (SELECT d FROM runs)) AS restart_sessions,
           md5(string_agg(a.entry_time::text || a.direction, ',' ORDER BY a.entry_time, a.direction)) AS trade_set_id,
           (COALESCE(sum(a.pnl_usd) FILTER (WHERE a.entry_time <  (SELECT t_mid FROM bounds)), 0) > 0)::int
         + (COALESCE(sum(a.pnl_usd) FILTER (WHERE a.entry_time >= (SELECT t_mid FROM bounds)), 0) > 0)::int AS positive_halves,
           count(*) FILTER (WHERE a.exit_reason = 'breakeven')::double precision / count(*)          AS scratch_rate,
           count(*) FILTER (WHERE a.pnl_usd > 0)                                 AS wins,
           avg(a.qty)                                                            AS avg_qty,
           sum(a.qty)                                                            AS contracts,
           sum(a.pnl_usd)                                                        AS net_pnl,
           COALESCE(sum(a.pnl_usd)  FILTER (WHERE a.pnl_usd > 0), 0)             AS gross_win,
           COALESCE(-sum(a.pnl_usd) FILTER (WHERE a.pnl_usd < 0), 0)             AS gross_loss,
           min(a.entry_time) AS first_trade, max(a.entry_time) AS last_trade
    FROM agg a GROUP BY a.source, a.strategy_id, a.instrument
), daily AS (
    SELECT a.source, a.strategy_id, a.instrument,
           (a.entry_time AT TIME ZONE 'America/New_York')::date AS d,
           sum(a.pnl_usd) AS p
    FROM agg a GROUP BY 1, 2, 3, 4
), risk AS (
    -- Daily series over the period's calendar sessions (fleet_sessions.n), flat days = 0:
    -- mean = Σp/N, sample variance = (Σp² − N·mean²)/(N−1), downside = sqrt(Σmin(p,0)²/N).
    SELECT d.source, d.strategy_id, d.instrument,
           fs.n                                                        AS n_days,
           sum(d.p) / fs.n                                             AS mean_day,
           CASE WHEN fs.n >= 2 AND (sum(d.p * d.p) - fs.n * power(sum(d.p) / fs.n, 2)) > 0
                THEN sqrt((sum(d.p * d.p) - fs.n * power(sum(d.p) / fs.n, 2)) / (fs.n - 1)) END AS sd_day,
           sqrt(sum(power(least(d.p, 0), 2)) / fs.n)                   AS downside_day
    FROM daily d CROSS JOIN fleet_sessions fs GROUP BY 1, 2, 3, fs.n
), joined AS (
    SELECT st.*, r.n_days, r.mean_day, r.sd_day, r.downside_day,
           s.engine AS s_engine, s.params_json AS p, s.enabled AS s_enabled, b.slot_s, b.slot_e,
           count(*) OVER (PARTITION BY st.source, st.trade_set_id) AS trade_set_size
    FROM st
    LEFT JOIN risk r ON r.source = st.source AND r.strategy_id = st.strategy_id AND r.instrument IS NOT DISTINCT FROM st.instrument
    LEFT JOIN paper_strategies s ON st.source = 'paper' AND s.strategy_id = st.strategy_id
    CROSS JOIN bounds b
), scored AS (
    SELECT j.source, j.strategy_id,
           COALESCE(j.s_engine, 'nq_executor') AS engine,
           COALESCE(j.p->>'mode', j.p->>'trigger_mode') AS mode,
           j.p->>'base_id' AS base_id,
           j.p->>'overlay' AS overlay_tag,
           COALESCE(j.s_enabled, j.source = 'live') AS enabled,
           (j.source = 'paper'
            AND (j.p->>'base_id' IS NULL OR COALESCE(j.p->>'overlay', '') NOT IN ('sz', 'wait', 'bbe'))
            AND (
               (j.s_engine = 'trend'
                  AND COALESCE(j.p->>'mode', '') <> 'rs_continuation'
                  AND COALESCE((j.p->>'win_start')::int, 930) <= COALESCE((j.p->>'win_end')::int, 1555)
                  AND COALESCE((j.p->>'win_start')::int, 930) <= j.slot_s
                  AND COALESCE((j.p->>'win_end')::int, 1555)  >= j.slot_e)
            OR (j.s_engine = 'mtf_scalper'
                  AND COALESCE(j.p->>'trigger_mode', 'auto') <> 'smt'
                  AND COALESCE(j.p->>'reference_symbol', '') = ''
                  AND COALESCE((j.p->>'use_im_filter')::boolean, false) = false
                  AND substr(COALESCE(j.p->>'session_window', '0900-1200'), 1, 4)::int <= j.slot_s
                  AND substr(COALESCE(j.p->>'session_window', '0900-1200'), 6, 4)::int >= j.slot_e)
           )) AS live_runnable,
           j.trades, j.sessions, j.wins, j.net_pnl, j.gross_win, j.gross_loss,
           CASE WHEN j.gross_loss > 0 THEN j.gross_win / j.gross_loss END AS profit_factor,
           CASE WHEN j.trades > 0 THEN 100.0 * j.wins / j.trades END        AS win_rate,
           CASE WHEN j.trades > 0 THEN j.net_pnl / j.trades END             AS avg_pnl,
           j.avg_qty,
           CASE WHEN j.contracts > 0 THEN j.net_pnl / j.contracts END       AS net_per_contract,
           CASE WHEN j.n_days >= 10 AND j.sd_day > 0
                THEN j.mean_day / j.sd_day * sqrt(252.0) END                 AS sharpe,
           CASE WHEN j.n_days >= 10 AND j.downside_day > 0
                THEN j.mean_day / j.downside_day * sqrt(252.0) END           AS sortino,
           (j.trades < 30 OR j.sessions < 2) AS thin,
           j.first_trade, j.last_trade,
           j.restart_sessions, j.trade_set_id, j.trade_set_size, j.positive_halves, j.scratch_rate,
           j.instrument
    FROM joined j
)
SELECT s.source, s.strategy_id, s.engine, s.mode, s.base_id, s.overlay_tag, s.enabled, s.live_runnable,
       (s.live_runnable AND s.enabled AND s.net_pnl > 0
        AND s.trades >= p_min_trades AND s.sessions >= p_min_sessions
        AND s.profit_factor IS NOT NULL AND s.profit_factor >= p_min_pf
        AND s.restart_sessions = 0) AS qualifies,
       s.trades, s.sessions, s.wins, s.net_pnl, s.gross_win, s.gross_loss,
       s.profit_factor, s.win_rate, s.avg_pnl, s.avg_qty, s.net_per_contract,
       s.sharpe, s.sortino, s.thin, s.first_trade, s.last_trade,
       s.restart_sessions, s.trade_set_id, s.trade_set_size, s.positive_halves, s.scratch_rate,
       s.instrument
FROM scored s
ORDER BY s.net_per_contract DESC NULLS LAST, s.trades DESC;
$$;
