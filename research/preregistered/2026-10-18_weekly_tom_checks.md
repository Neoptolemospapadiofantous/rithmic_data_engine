# Pre-registration — checks of the two rules from 2026-10-18_weekly_and_tom (written 2026-10-18, BEFORE any run)

Founder: "do more tests". The rules: SKIP a cascade WITH a straight week (e5 ≥ 0.661, sign(r5) = the cascade side); SIZE UP cascade
longs at the turn of the month (tdleft ≤ 1 or tdom ≤ 3). No new cuts, no new cells.
1. **ES replication** — the identical cascade and both splits on ES (symbol ES, confirm NQ, 0.35 pt cost), 2010–18 and 2019–24.
   Replicated = the same sign of effect in BOTH periods (skip cell < base; TOM longs > all longs).
2. **Other NQ trend entries** — the same two splits on the gap retest (`gap_or_retest`, stop 0.35 ATR) and on the 10:00 gap rule
   (`pdm_gap_confirm` form), both periods. Reported as same-sign or not.
3. **Book effect** (description) — the book (weakness long + cascade + retest, 1 trade a day) vs the book with the skip AND 2 units
   on turn-of-month cascade longs: pts/session at 2 pt in 2010–18 / 2019–24 / 2025–26, max drawdown 2019–24 (units counted).
The rules stay "forward record" unless 1 AND 2 both replicate.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick`; net/trade at the market's cost, n; 2010–18 | 2019–24).
| market / entry | base | skip cell (WITH a straight week) | all longs | turn-of-month longs |
|---|---|---|---|---|
| ES cascade | +0.02 (1,305) / +0.94 (898) | +0.71 (173) / −1.01 (98) | +0.19 (677) / +0.78 (470) | +0.21 (170) / +1.68 (119) |
| NQ gap retest | +1.06 (916) / +16.58 (659) | −0.30 (147) / +14.20 (81) | +1.44 (546) / +21.17 (385) | +0.61 (132) / +33.78 (112) |
| NQ 10:00 gap rule | +2.84 (279) / +22.48 (214) | +1.34 (29) / +14.01 (26) | +1.51 (173) / +22.24 (139) | +0.89 (53) / +25.58 (43) |
Skip rule: same sign on the NQ retest and the 10:00 rule (lower than base in both periods), MIXED on ES (higher in 2010–18).
Turn-of-month longs: same sign on ES (both periods, by +0.02 and +0.90), MIXED on the NQ retest and 10:00 rule (lower in 2010–18).
**Neither rule replicates on both checks → both stay forward-record only (the pre-registered condition).**
Book effect (description, `--trades`, 2 pt cost; TOM 2× = a second unit on turn-of-month cascade longs that are the day's trade):
book +0.41 / +9.23 / +21.35 pts/session (2010–18 / 2019–24 / 2025–26), max DD 2019–24 1,481 · + skip +0.48 / +9.93 / +18.35, 1,561 ·
+ skip + TOM 2× +0.68 / +10.68 / +20.24, 1,851 (the extra unit adds risk with the points).
