# Order flow for 5+ trades a day (written 2026-10-19, BEFORE any run)

Founder: "we need at least 5 a day" → "test the tick order flow for 5 a day". Data: aggressor buy / sell volume of every NQ trade summed per
minute (2023-03 → 2026-03-19, from the MBO feed) and the resting book per minute (imbalance (bid − ask)/(bid + ask) within 10 bp); tick-level
trades with side from the live collector (2026-09-18 → today) for a check. Program `research/programs/orderflow_hf.cpp`. NY 09:35–15:30.
Events (5-min refractory per event type), thresholds set so they fire ≥ ~5 a day:
- A delta burst: the minute's delta (buy − sell) ≥ Z × the sd of the last 30 minutes' deltas and the minute moved the same way → follow. Z ∈ {1.5, 2, 3}.
- B absorption: volume ≥ 3 × the 30-min average, |move| ≤ 0.01 ATR, |delta| ≥ 0.3 × volume → fade the aggressor.
- C book imbalance: mean imbalance of the last 3 minutes ≥ q (q ∈ {0.2, 0.3}) → follow the heavy side.
- D delta divergence: a new 15-min high (low) while the 15-min delta is negative (positive) → fade.
- E = A (Z 2) only with the day (price on that side of the 09:30 open and of VWAP).
Trade: entry at the minute's close; stop 0.05 ATR; target 1R or 2R; exit at 15 min; 1 pt cost. Measured: events a day, win %, net pts and
net R per trade, two halves (2023-03 → 2024-08 / 2024-09 → 2026-03). A candidate needs ≥ 5 a day, net > 0 in both halves at 1 pt and t ≥ 2;
it then goes through edge_lab (random direction / time / day controls) and the live-tick month as a check.

## Results (2026-10-19, research/programs/out/orderflow_hf.txt)
Minute-level flow, 641 NY days 2023-03 → 2026-03 (net of 1 pt; halves): delta burst follow z 1.5 / 2 / 3 — 26 / 16 / 6 a day, win 48–49 % at 1R,
−1.08 / −1.33 / −1.50 pts (both halves negative); delta divergence fade 6.9 a day −0.97; delta burst with the day 9.1 a day −1.38; book
imbalance ≥ 0.2 / 0.3 — 1.4 / 0.5 a day, −0.85 / −0.27; absorption ≈ 0 a day. Every cell ≈ −1 pt = the cost: before cost a coin flip.
Tick level (live collector, every NQ trade with its side, 21 NY days 2026-09-18 → 10-16, 10-second bars): delta burst z ≥ 2 / 3 / 4, 15-pt stop,
1R — 50 / 36 / 23 a day, −0.98 / −0.39 / +0.56 pts (z 4: 55 % win on 489 trades, t ≈ 0.8 on one month — noise; z 4 with 2R −0.65).
**Verdict: no order-flow event that fires ≥ 5 times a day carries an edge, at minute or tick resolution. Closed.** (Same as the 2026-10-11
MBO order-flow ML and the 2026-10-19 behaviour-part-3 flow tests: aggressor flow mostly agrees with price — it describes the move, it
does not predict the next one.)
