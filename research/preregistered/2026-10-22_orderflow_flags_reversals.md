# Order flow on flags and reversals — NQ (written 2026-10-22, BEFORE any run)

Founder: "test with order flow data as well", "for both reversal and flags". Data: NQ 1-minute buy (aggressor) volume where present —
2023-03 → 2026-03 (Databento MBO) and 2026-09 → 2026-10 (the collector) — and the resting-book imbalance per minute (qimb = (bid − ask) /
(bid + ask) within 10 bps, 2023-03 → 2026-03-18). Only sessions with buy volume are used ("flow days"). ES has no aggressor data before
2026-09, so there is NO second-market check this time; that is why the thresholds below are stricter than usual.
Everything is mirrored for shorts: "buy share" is the share of volume on the trade's side, delta = side volume − other-side volume.

## Signals (unchanged from their own programs, traded exactly as there)
- FLAGS (flag_hhhl.cpp export, 3-5m, flag-mid limit, stop flag low − 0.1 ATR, measured move): sets "every flag" and "A-list + 5m HH/HL +
  with the 5m opening-range break" (both sides).
- REVERSALS (preregistered/2026-10-22_reversal_plan.md): R1 CHoCH pullback, R2 sweep and reclaim, D1 double bottom close above the neck;
  exit X2 measured move (also reported at X1 2R, X4 hold).

## Order-flow features, fixed
Flags:  FF1 the pole was bought: buy share over the pole (pole start bar → pole high bar, on the flag's chart) >= 0.55
        FF2 the pullback was not sold: buy share inside the flag (after the pole high → registration) >= 0.50
        FF3 the session's cumulative delta (09:30 → registration) on the trade's side > 0
        FF4 bid-heavy book: mean qimb (trade's side) over the 10 minutes before registration > 0
        FF5 buyers defend the mid: delta over the 5 minutes before the entry fill >= 0
Reversals: RF1 cumulative delta from the low bar's start to the signal > 0 (buyers took over since the low)
        RF2 absorption at the low: the low's 5m bar volume >= 1.5 × the session's average 5m volume so far AND its delta < 0 (sellers hit
            hard, price held)
        RF3 delta divergence: delta of the low bar > delta of the previous 5m bar (doubles: the second low bar's delta > the first low's)
        RF4 bid-heavy book: mean qimb (trade's side) over the signal bar's 5 minutes > 0
        RF5 the signal bar was bought: buy share of the signal 5m bar >= 0.55
Each feature alone (true and false), and the pre-set combos ">= 3 of the five" and "all of 1, 3, 5" (flags: FF1 + FF3 + FF5; reversals:
RF1 + RF3 + RF5 — the three that do not need the book, so they also cover 2026-09+).

## Pass (judged on: each feature true, the two combos; one position at a time per set)
1. >= 60 trades · 2. avg >= +0.20R after cost and PF >= 1.30 · 3. both halves > 0 (2023-03 → 2024-12, 2025-01 → 2026-10) ·
4. > 0 at +1× cost · 5. beats the same set's trades WITHOUT the feature by >= +0.20R.
About 40 judged cells and no second market: expect two or three false passes. A pass earns a paper test only.

## Results (2026-10-22; research/programs/orderflow_check.cpp, out/orderflow_check.txt; 659 flow days)
**0 of 35 judged cells pass.**
- Flags, every flag (524 flow-day trades, +0.238R): no feature helps in the planned direction. FF1 (pole share >= 0.55) is almost never met
  (4 flags — aggressor share over a 10-30 minute pole sits near 0.50, so the threshold was set too high), FF2 +0.026R, FF5 −0.063R, >= 3 of
  five −0.025R.
- Flags, A-list + 5m HH/HL + break (144 trades, +0.379R): FF2 +0.197R (24 trades, just under the bar), FF4 +0.109R; nothing passes.
- **Seen, not planned — the opposite direction:** on every flag, the flags whose session delta was AGAINST the trade (price above the open and
  VWAP while aggressive volume net sold) made +0.642R, PF 2.04, 70 trades, both halves (+0.675 / +0.563), vs +0.212R with the delta on our
  side; the book pointing against us likewise +0.297R vs +0.162R. Same theme as the paper fleet's only working book overlay (fade the stacked
  side, imbalance_max). It was not the planned direction, it is one market and 70 trades: a hypothesis for its own pre-registered test, not
  a result.
- Reversals: R1 −0.063R, R2 −0.019R, D1 −0.039R on flow days; the best cells R1 ">= 3 of five" +0.047R and R2 RF1 +0.034R are far below
  +0.20R. Absorption at the low (RF2) is rare (3-39). Order flow does not turn the reversal setups into winners.
