# Pre-registration — free macro / options data as day filters (written 2026-10-16, BEFORE any run)

Founder: "test all" (no expenses). Free daily data: FRED 10-year (DGS10) and 2-year (DGS2) Treasury yields, broad dollar
index (DTWEXBGS, 2006+); CBOE VVIX (2006+) and SKEW. (FRED's high-yield spread only goes back to 2023 — not used.)
Every value is the last observation dated BEFORE the trading day. NQ 2010–26 (dollar / VVIX 2010+), 1.0 pt cost.

## Family: 4 filter tests, controls at 99 %, plus each filter's complement run for the adoption rule
1. `cascade_vvix_high` — the cascade only when VVIX ≥ 100. Why: high vol-of-vol means option dealers are short gamma
   and hedge WITH the move, so the open's direction carries further.
2. `cascade_rates_move` — the cascade only when yesterday's 10-year yield changed ≥ 7 bp. Why: a macro repricing day
   leaves rebalancing to finish the next session, so the open's direction is real flow.
3. `weak_day_flight` — the weak-day long only when yesterday's 10-year yield fell (bonds bid while stocks sold = flight
   to safety, a forced de-risking that reverts). VIX ≥ 20 rule kept; IBS ≤ x, x ∈ {0.3, 0.5}.
4. `weak_day_usd` — the weak-day long only when the dollar index rose ≥ 0.3 % on its last observation (dollar funding
   stress = forced selling). VIX ≥ 20 kept; x ∈ {0.3, 0.5}.
## Adoption (all must hold)
The filtered spec passes every edge_lab gate AND its complement (the trades the filter removes) is net NEGATIVE on
walk-forward — a filter that only removes winners just trades less. Otherwise the fleet keeps its rules.

## Results (appended after the runs — rules unchanged)
| filter | filtered: trades, net pts/trade | complement (removed trades) | gates | adopted? |
|---|---|---|---|---|
| cascade, VVIX ≥ 100 | 816, **+9.35** (eras +3.7 / +10.0 / +16.6) | 1,505, +2.51 | FAIL controls (random time 60 %, day 92 %) | no |
| cascade, ≥ 7 bp 10-year move | 371, +5.56 | 1,964, +4.91 | FAIL controls | no |
| weak-day, 10-year yield fell | 209, **+29.80** (PF 1.74, eras +7.9 / +30.2 / +71.9) | 134, +27.49 | PASS | no — the complement is just as profitable |
| weak-day, dollar ≥ +0.3 % | 65, +16.25 | 296, +22.36 | FAIL controls | no |
No filter meets the adoption rule (every complement is net positive: the filters remove winners). One hypothesis for the
forward record, NOT adopted: the cascade earns ~3.7× more per trade when VVIX ≥ 100, in every era including 2010–18 —
a sizing idea (like the weak-day's +1 MNQ at VIX ≥ 25), to be pre-registered as such if it is ever used.
