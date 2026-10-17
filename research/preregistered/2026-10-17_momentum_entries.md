# Momentum direction: other momentum definitions × other entries (pre-registered 2026-10-17, before any run)

Founder: "optimize more or test other strategies for momentum, not just EMA, with the current set-up."
Fixed: NQ New York, entries at 5-min closes 09:45–13:55 (or ≤ 11:30 where stated), price on the direction's side of VWAP and
the 09:30 open, ≤ 3 a day, done after a loss, cost 1.6 pts; exits X1 = stop 0.25 ATR, break-even +1R, hold (current) and
X2 = stop 0.15 ATR, no break-even, hold (the optimiser's choice). 2010-07 → 2024-12.
MOMENTUM definitions (direction at the 5-min close):
- M-EFF30 / M-EFF45  last 30 / 45 one-minute moves, efficiency ≥ 0.3 (current / optimiser)
- M-ROC     |close − close 30 min ago| ≥ 0.15 ATR (size, not straightness)
- M-STAIR   the last 3 five-min closes each beyond the one before (a staircase)
- M-SLOPE   1-min EMA-21 now vs 15 minutes ago, change ≥ 0.05 ATR
ENTRIES on the 5-min bar that just closed, in the direction:
- E-ANY     any close (the twin)                          - E-EMA9 / E-EMA20  within 0.06 / 0.03 ATR of that EMA, closing back
- E-VWAP    within 0.05 ATR of VWAP, closing back          - E-BRK     close beyond the previous 5-min bar's high / low
- E-HOD     close at a new session high / low              - E-OR      close beyond the 15-min opening range
- E-DIP     a pullback of ≥ 0.1 ATR from the last 30 minutes' extreme, then a close back in the direction
- E-INSIDE  the previous bar was inside the one before it, and this bar closes beyond it
5 × 9 × 2 = 90 combinations. Scored: R/trade by era; walk-forward pick by year; ES with the walk-forward's final pick;
2025-26 printed (not clean for M-EFF30 / E-EMA20, clean-ish for the rest). Anything adopted must beat the current rule
(M-EFF30 × E-EMA20 × X1) in the walk-forward AND be positive in all three eras AND not lose on ES.

## Results (2026-10-17; research/programs/momx.cpp → out/momx.txt) — R/trade 2010-24 (eras), ES, 2025-26
- MOMENTUM DEFINITION is what matters: straightness (efficiency) works, size does not. M-EFF45 > M-EFF30 (any-bar +0.061 vs
  +0.037, both all eras +); M-ROC (size of the move), M-STAIR (3 higher closes) and M-SLOPE (EMA slope) are ≈ 0 or negative with
  every entry (2010-18 negative almost everywhere).
- ENTRIES (with M-EFF30 / M-EFF45): pullbacks beat breakouts. VWAP pullback is the standout — M-EFF45 E-VWAP +0.118 (BE 1R;
  +0.070 / +0.192 / +0.120) and the ONLY cell also non-negative on ES (+0.003); with the tight stop / no BE +0.189 (+0.130 / +0.300 /
  +0.147; ES −0.034). M-EFF30 E-VWAP +0.069 / +0.111. Dip-and-close-back (E-DIP) +0.048 … +0.107, all eras +, ES ≈ 0. EMA-20 +0.079
  (EFF30) / +0.159 on only 179 trades (EFF45). Breakouts (E-BRK, E-HOD, E-OR) ≈ +0.02–0.07 with weak 2010-18; inside-bar mixed.
- WALK-FORWARD over all 90: +0.008 R/trade — re-picking the best cell each year chases noise and does WORSE than simply keeping
  the current rule (+0.079 on the same years). ES: nothing is clearly positive (best +0.003).
VERDICT: no replacement adopted (walk-forward fails). The consistent pattern across both windows and both exits: STRAIGHT 45-min
momentum + a PULLBACK entry (VWAP, then dip, then EMA-20), all eras positive. Forward-paper candidates: M-EFF45 × E-VWAP and
M-EFF45 × E-DIP next to the two running ids — judged on new days only.
