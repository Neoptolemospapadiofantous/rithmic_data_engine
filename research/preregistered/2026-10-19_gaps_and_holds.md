# Pre-registration — gap size for the retest, PDH/PDL break-and-hold, small-gap fade (written 2026-10-19, BEFORE any run)

From 2026-10-19_behavior_part3: 0.10–0.25 ATR gaps fill ~75 % (median ~10 min); PDH/PDL breaks that hold 30 min close beyond 68–80 %.
1. `gr_g25`: the book's gap retest with |gap| ≥ 0.25 ATR (vs 0.1). Replaces the retest in book F only if F pts/session rises in BOTH
   2010–18 and 2019–24 AND the Vest operation (2 pts, both eras) does not fall.
2. `pd_hold`: 10:00–13:00, 5-min scan: the open was inside yesterday's range, and every 1-min low of the last 30 min is above
   yesterday's high (long) / every high below yesterday's low (short); 0.35 ATR stop, 2R, flat 15:55, one a day. edge_lab gates
   (controls 99 %), 2010-07 → 2024-12; if it passes, judged as an add to F (pts/session both eras, pts/DD) and on Vest.
3. `gap_fade`: at 09:31, 0.10 ≤ |gap| < 0.25 ATR → trade toward the prior close; stop = m × the gap distance, target = the gap
   (1/m R), flat 10:30; m ∈ {1, 2}. edge_lab gates as above. Cost 1 pt.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (build-exp/edge_lab).
1. Retest with |gap| ≥ 0.25 in book F: pts/session +0.31 / +16.11 / +12.05 → +0.40 / +14.50 / +5.04, pts/DD 0.0123 → 0.0097 → **FAIL**
   (2019–24 lower). The small-gap fill tendency does not hurt the retest: its trigger already waits for the gap side to hold.
2. pd_hold: **FAIL at CONTROLS** — +4.14 pts/trade on 465 (PF 1.21, t 1.40), random direction 97 %, time 86 %, day 96 %; 2010–18 −0.55,
   2019–22 +4.75, 2023–26 +14.59; 4 / 14 years positive; ES +17 pts; prop break-even fee $32. A held break is the day's move, not an edge
   on top of it.
3. gap_fade: m 1 **FAIL at CONTROLS** (+2.08); m 2 (stop = 2 × gap, target = the gap) **passed every gate** — +4.64 pts/trade on 343, PF 1.37,
   t 2.14, win 65 %, controls 100 / 100 / 100 %, robustness 6/7 — but 2010–18 −0.48, the money is 2020–24, it is the better of 2 variants,
   and the **holdout 2025-01 → today is −7.91 pts/trade on 70 trades (win 59 %)** → **FAIL on the holdout.** It also takes the OPPOSITE side
   of the gap retest on every one of the 524 days both trade, so it could never share an account with the book.
**Nothing adopted.**
