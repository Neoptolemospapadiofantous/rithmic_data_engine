# EMA-14 hand trade: entry types × stops × trailing exits (pre-registered 2026-10-17, before any run)

Founder: "what about entries, SL and trail". Same permission as the Plan panel (plan direction, day's side of VWAP and the 09:30 open,
entries ≤ 13:55, max 3 a day, done after a loss, one position at a time), 1-minute fills, stop first inside a bar, cost 1.6 pts, flat 15:55.
Entries (long; short mirrored):
- E0 close of the 5-min bar that touched EMA-14 and closed back above (today)
- E1 buy stop 1 tick above that bar's high, valid for the next 2 bars, else cancelled
- E2 limit AT the EMA: price above EMA, VWAP and the open at the previous 5-min close; filled when a minute trades down to the EMA
- E3 confirmation: the bar after the touch bar also closes above the EMA → enter at its close
Stops: 0.25 ATR · 0.15 ATR · swing low of the last 10 five-min bars − 2 pts (all capped at 0.25 ATR).
Exits:
- X0 target 2R (today) · X1 2R + break-even at +1R · X2 no target, break-even at +1R, hold
- X3 ATR trail: stop follows the best price at 0.25 ATR (never loosens) · X4 break-even at +1R then trail 1R behind the best
- X5 swing trail: after each 5-min close the stop moves to the lowest low of the last 3 five-min bars (never loosens)
- X6 EMA exit: out on a 5-min close below the EMA-14 · X7 VWAP exit: out on a 5-min close below VWAP, target 2R
4 entries × 3 stops × 8 exits = 96 combinations. Scored in R and pts, eras 2010-18 / 2019-22 / 2023-24; walk-forward pick (best total R of earlier years);
the walk-forward's choice on ≤ 2024 gets one look at 2025-26.

## Results (2026-10-17; scratchpad emaexit.cpp; today's rule reproduced: −0.066 R)
All 96 combinations are NEGATIVE in R over 2010-24. Best of each family (R/trade, eras 10-18 / 19-22 / 23-24, pts/trade):
- entries: limit at the EMA −0.025 (−0.105 / +0.053 / +0.156, +2.33 pts) · touch-bar close −0.048 · confirm bar −0.047 ·
  stop above the bar high −0.074 (paying up for confirmation costs more than it saves)
- exits: break-even at +1R then hold −0.025 · BE + 1R trail −0.042 · 2R+BE −0.051 · 0.25-ATR trail −0.051 · 2R (today) −0.066 ·
  VWAP exit −0.073 · EMA-close exit −0.089 · 3-bar swing trail −0.096 (tight trails cut the winners)
- stops: 0.25 ATR beats 0.15 ATR (−0.068) and the 10-bar swing − 2 (−0.074), as in the stops test
Walk-forward −0.028 R / +1.91 pts per trade (today −0.038 R / +1.48). HOLDOUT 2025-26 (one look, limit at EMA + BE hold):
−0.001 R / −1.19 pts (n 218); today's rule −0.002 R / −0.35 pts. The limit fill is optimistic (filled on a touch, no queue).
VERDICT: no entry / stop / trail makes the EMA pullback an edge. Least bad handling: 0.25-ATR stop, break-even at +1R, no fixed target.
