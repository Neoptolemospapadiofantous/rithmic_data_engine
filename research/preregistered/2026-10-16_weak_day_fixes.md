# Pre-registration — making the weak-day long prop-safe (written 2026-10-16, BEFORE the runs)

`weak_day_long` passes every edge_lab gate (+13.2 pts/trade) but carries no stop and loses up to ~550 pts on a day, so
it blows prop accounts (alone: payouts in ~14 % of months). Two fixed tests, nothing tuned afterwards:
1. **Thesis exits** (`edge_lab exits`): the 9 standard rules (vwap, open, stall, vwap+stall, runners, 15m trail, flow).
   For a no-stop trade the stall rule's "0.5 R" uses 0.25 × ATR as R. Pass bar unchanged: better per trade, random-timing
   twin ≥ 99 %, better in ≥ 2 of 3 eras incl. 2023–26, higher prop break-even fee.
2. **Meta take/skip** (`edge_lab meta`, standard inputs): result unit R = 0.25 × ATR for no-stop trades. Standard meta
   gates (random skip ≥ 95 %, shuffled labels ≥ 95 %, eras, ridge cross-check, prop).
The fleet decision for the weak-day long follows from these two results only.

## Results (appended after the runs — tests unchanged)
- Exits: every thesis exit cuts the edge (vwap +1.36, open +2.31, runners +1.4–2.3 vs base +13.20 pts/trade) — the trade
  is the full-day recovery; leaving on the first dip throws it away. Stall (+14.01) fails the timing control (97 %, needs
  99 %); flow (+13.24) fired on 17 trades. → FAIL.
- Meta: takes 496 of 562 (+20.19 vs +19.02 on 2014+), random skip beaten 62 %, shuffled labels 79 % → FAIL.
- Decision: the weak-day long stays exactly as it is; in the working fleet as a small-size core strategy (1 MNQ).
