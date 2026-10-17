# Direction + permission for hand-traded EMA-14 pullbacks (pre-registered 2026-10-17, before any run)

Founder: "what about which direction to trade and if I can trade. I will trade the EMA myself."
The trade (the founder's): 5-minute bars, EMA(14) of closes; a bar that touches the EMA and closes back on the trade side → enter at the
close; stop 0.25 ATR, target 2R, flat 15:55; one position at a time. Cost 1.0 NQ pt. NQ 2010-07 → 2024-12 (2025+ untouched).
Every EMA touch from 09:45 to 15:00 is a candidate in BOTH directions; the question is which ones to take.

Direction sources (each gives LONG / SHORT / none at the moment of the bar):
- D0 none: take the EMA side (close above EMA → long) — the baseline "trade the EMA without a plan"
- D1 the plan: the first cascade check reached or gap-retest trigger fixes the day's direction (none before it)
- D2 price vs the 09:30 open at that moment · D3 price vs VWAP at that moment · D4 the gap's side (|gap| ≥ 0.1 ATR)
Permission conditions, each measured on top of D1 (the plan's direction):
- P1 price still on D's side of the open · P2 still on D's side of VWAP · P3 time bucket (to 11:30 / to 13:55 / to 15:00)
- P4 trade number of the day (1st, 2nd, 3rd+) and the previous trade's result (win / loss)
Judged per trade and per year-era (2010-18 / 2019-22 / 2023-24). A direction source or permission is adopted only if it beats the
alternative per trade in all three eras. Same-day random-time twins (same direction, random 5-min bar 09:45–15:00) for D1.

Extended the same day, before the run (founder: "or mtf_london_v5__sg and ORB"): the same question for the live ORB rules
(5-min range, stop orders 2 pts beyond, 15-pt stop, BE +1 after 5 min once +3, trail 10, ≤3 trades, entries to 10:30) on 2010-24,
split by gap / overnight / yesterday / 50-day agreement, range width, VIX, trade number; and for mtf_london_v5(__sg)'s own trades
(2022-23 bar replay, 2025-26 history, forward) split by overnight move / move since 18:00 / VWAP side / yesterday / 50-day / time / trade number.

## Results (2026-10-17; scratchpad perm.cpp) — NQ pts per trade after 1.0 cost, eras 2010-18 / 2019-22 / 2023-24
EMA-14 pullbacks: no plan +0.30 (−0.72 / +1.57 / +1.92) · plan direction +0.78 (−0.75 / +3.03 / +2.73) · open side +0.70 · VWAP side
+0.82 · gap side +0.46 · random 5-min bar in the plan direction +1.01 (−0.04 / +2.61 / +2.25) → the EMA touch adds nothing over a random
moment; the DIRECTION does the work. Inside the plan direction, better in all three eras: on D's side of VWAP +1.72 vs −1.11, of the
open +1.31 vs −1.68, after a win +1.96 vs after a loss −0.42; entries after 13:55 −1.28 (2019-24). 2010-18 negative whatever the filter.
ADOPTED as the Plan page's EMA permission (direction = first plan trigger; allowed on that side of VWAP and the open, before 13:55).
Live ORB rules (stop orders fill at the market when price is already beyond, caught on the first run: +3.22 → −1.23):
−1.23 (−0.31 / −1.91 / −2.81); best split gap agrees −0.59 (+0.02 / −0.72 / −2.22). No condition makes it positive.
mtf_london_v5: negative in every period and every split (≈ −3.3 to −4.7 after commission; the commission conversion may overstate
cost, gross was −1.3), forward 133 trades −1.48. No condition makes it positive.
Check of the EXACT panel rule (all conditions together: plan direction + VWAP side + open side + entry ≤ 13:55 + max 3 + stop after
a loss), asked by the founder the same day: +1.62 pts/trade (n 2,621), eras −0.52 / +5.49 / +2.72; by year negative 2010-16 and 2023
(−0.1), 2022 alone +11.8. Stop 0.25 ATR averaged 12 / 49 / 56 pts by era (≈ 82 pts at the October 2026 ATR) → the edge is ≈ 0.03 R
per trade: thin, era-dependent, mostly the 2019-22 volatility.
