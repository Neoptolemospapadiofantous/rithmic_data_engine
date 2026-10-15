-- research_ledger.sql — what has been tested, how much of it, and what it showed (2026-10-11, founder: "a dedicated
-- page to store and monitor all the different variations"). Read by the dashboard's Strategies → Research tab.
--   research_coverage  per history job type: months done / pending / no data (from history_queue.sh list)
--   research_variants  per market × family × variant: strategies, how many have finished-month history, trades,
--                      net per trade, PF, how many are positive; plus the last 30 days of the forward fleet
--   research_tests     one row per direct test (scripts in this repo / the session that ran it), with a verdict
-- research_coverage / research_variants are rewritten by scripts/research_refresh.sh; research_tests is appended to.
-- Money: $ per MNQ contract for NQ (paper fleet point_value 2), per 0.1 coin for crypto, after the fleet's costs.
CREATE TABLE IF NOT EXISTS research_coverage (
    job        text PRIMARY KEY,           -- history_queue.sh job type (nq, nqx, nqbars, …)
    label      text,
    what       text,
    done       int NOT NULL,
    pending    int NOT NULL,
    nodata     int NOT NULL,
    failed     int NOT NULL,
    first_done text,
    last_done  text,
    updated    timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS research_variants (
    market          text NOT NULL,         -- NQ | crypto | ES | other
    family          text NOT NULL,         -- engine mode (trend modes) or engine (orb, mtf_scalper)
    variant         text NOT NULL,         -- overlay suffix, or 'base'
    strategies      int NOT NULL,
    with_history    int NOT NULL,
    hist_trades     bigint NOT NULL,
    hist_net        double precision,
    hist_per_trade  double precision,
    hist_pf         double precision,
    hist_positive   int NOT NULL,          -- strategies with ≥ 100 history trades and net > 0
    hist_months     int,
    fwd30_trades    bigint NOT NULL,
    fwd30_net       double precision,
    fwd30_per_trade double precision,
    updated         timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (market, family, variant)
);

CREATE TABLE IF NOT EXISTS research_tests (
    id         text PRIMARY KEY,           -- e.g. N5-ib-break-nq
    grp        text NOT NULL,              -- R7 / N2 … (TODO.md section)
    name       text NOT NULL,
    symbol     text NOT NULL,
    period     text NOT NULL,
    trades     int,
    pf         double precision,
    t_stat     double precision,
    pos_years  text,
    verdict    text NOT NULL CHECK (verdict IN ('edge', 'weak', 'no edge', 'invalid', 'pending')),
    notes      text,
    tested_on  date NOT NULL DEFAULT current_date
);
