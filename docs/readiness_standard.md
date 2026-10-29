# Strategy readiness standard (v1, 2026-10-28)

Founder, 2026-10-28: "test every single one of these. Set a standard of how we test and make sure these are ready."

One standard for every setup we might trade by hand or let the engine trade. The thresholds below were fixed **before** the
first scorecard was produced; changing one is a new version of this file (v2, v3 …) with the date and the reason, never an
edit in place after seeing results.

Run it: `scripts/readiness.sh` → `data/readiness/scorecard.md` + `scorecard.csv` (research/programs/readiness.cpp does the
numbers; edge_lab supplies the screen, controls and robustness gates).

## Inputs

- **Walk-forward trades** from `edge_lab run --trades` on the FULL history (`reports/<id>__full.wf_trades.csv`): each month is
  traded with the variant that was best on the months before it, so no gate below reads a hand-picked parameter set.
- Costs: the real round trip edge_lab uses (NQ 1.5 pts, ES 0.5 pts per contract) unless a gate says otherwise.
- **R** = net points ÷ the trade's stop in points. Setups without a stop (day holds) use 1 R = 0.5 × that day's ATR and are
  marked "no stop".
- Today's size: every dollar figure scales the trade's points by today's ATR ÷ the ATR on the trade's day.
- PF, years positive and the t-stat are measured on ATR-scaled points (each trade's net ÷ that day's ATR), so a 2011 trade weighs
  as much as a 2025 one. edge_lab's own PF is on raw points and therefore reads higher for setups whose edge sits in the
  high-priced recent years.

## The gates

| # | Gate | PASS | WARN | FAIL |
|---|---|---|---|---|
| R1 | **Screen** (edge_lab) | best variant nets > 0 after cost | — | otherwise |
| R2 | **Walk-forward**, 2011-07 → 2024-12 | n ≥ 150, PF ≥ 1.15, expectancy ≥ +0.08 R | — | any of them missed |
| R3 | **Controls** (edge_lab, 400 seeds) | beats random direction AND random entry time | beats random direction and random day only — the timing adds nothing, the edge is the direction / the day | loses to random direction |
| R4 | **Stability** | expectancy > 0 in 2016-20 AND 2021-24, > −0.05 R in 2011-15, ≥ 60 % of years 2011-24 positive | — | otherwise |
| R5 | **Live cost stress** | net > 0 and PF ≥ 1.05 with the round trip raised to the live-measured cost: NQ 3.5 pts, ES 1.0 pt (live entry slippage measured 4.6-5.7 ticks vs the 1 tick assumed, 2026-09-30) | — | otherwise |
| R6 | **Second market** | the same rule on the other index (NQ ↔ ES) nets > 0 (edge_lab "confirm") | — | negative or not run |
| R7 | **Robustness** (edge_lab) | its robustness gate passes, entry one bar late still > 0, most neighbouring variants > 0 | one of the last two missed | the gate fails |
| R8 | **Holdout** 2025-01 → 2026-10-23 | ≥ 30 trades, net > 0. Marked *sealed* when the spec was cut at 2024-12 before it was ever run on these months, *seen* when it was designed with them visible (weaker evidence) | fewer than 30 trades | net ≤ 0 |
| R9 | **Strength** (multiple testing: 467 ideas were tried) | t-stat of the walk-forward trades ≥ 3.0 | 2.5 – 3.0 | < 2.5 |
| R10 | **Built** | runs on paper / the Plan page AND its signals were matched against the research trades (parity) | built, parity not yet checked | not built |
| R11 | **Risk fit** per account | the stop on 1 micro ≤ 20 % of the account's trailing allowance AND the 95th-percentile one-year drawdown (bootstrap of the setup's own days, 1 micro, today's size) ≤ 50 % of it; the scorecard gives the micros that fit a 50K ($2,000) and a 150K ($4,500) | — | not even 1 micro fits |
| R12 | **Forward** (paper, since the rule was frozen) | ≥ 40 forward trades, average after cost > 0, PF ≥ 1.1 | — PENDING while < 40 | the 40 are in and the average ≤ 0 or PF < 1.1 |

## Levels

- **RESEARCH-READY** — no FAIL in R1-R9.
- **BUILD-READY** — research-ready and R10 PASS.
- **LIVE-READY** — build-ready, R11 PASS for the account it will trade, and R12 PASS. Only a live-ready setup is traded with
  money, and going live stays the founder's decision.

A setup that FAILS any of R1-R9 is not traded and not tuned until it passes: tuning a setup on the data that failed it is
how a fake edge is made. It may be re-specified as a NEW idea (new id) and must then pass from R1 again.

## What this standard cannot promise

- The 2025-26 holdout is clean only for specs cut at 2024-12 before they were run. Everything else has been seen; only
  future sessions (R12) are fully clean.
- Fills: R5 stresses cost, but a limit-order entry's fill rate and the exact stop fill are only proven on paper / live (R10,
  R12).
- A PF of 1.2-1.4 means long losing streaks (8-10 losses in a row happen). The standard says an edge exists; it does not
  say any given week makes money.
