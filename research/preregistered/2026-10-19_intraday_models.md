# Pre-registration — a size model at 09:30 and a direction model after the open (written 2026-10-19, BEFORE any run)

Founder: "do all. test more" (after the session model found direction unforecastable at the open, size slightly forecastable).
Program `research/programs/intraday_models.cpp`; walk-forward by year 2014–2024 (fit on earlier years), 2025–26 one look; boosted
depth-2 trees (lab Gbm) + ridge cross-check; NQ RTH days, roll windows excluded.

**A. Size model (09:30).** Target: NY RTH range / ATR. Features: the 28 pre-session features of 2026-10-19_session_model (NY row).
Measured: corr predicted vs actual range; actual range by predicted fifth. Then the book (rebuilt cascade + gap retest + 10:00 gap
rule trades, 0.35 ATR stop, 2R) net in R by predicted-range fifth. SIZE rule: if the top fifth earns ≥ 1.25 × the average R and the
bottom fifth ≤ 0.75 × in BOTH 2014–18 and 2019–24, test "1.5× top fifth / 0.5× bottom fifth" on the Vest operation (both eras, 2 and 6
pts) and keep it only if the mean net per year rises in all four.

**B. Direction after the open (09:50 and 10:30).** Target: (price 15:55 − price at T) / ATR. Features at T: move from the open, path
straightness |px−open|/(high−low so far), range so far, last 15-min move, 20-min efficiency, distance to VWAP, ES move from its open,
gap, overnight range, open/price vs yesterday's and the overnight high/low, prior NY move, VIX, VIX 1d change, VIX/VIX3M, e20, r20,
weekday, FOMC / CPI / NFP. Measured: hit rate, corr, and three trades held to 15:55 (1 pt cost): (i) baseline "go with the move" when
|move| > 0.25 ATR (the cascade direction); (ii) the model's direction when |pred| ≥ the training median; (iii) the baseline only when the
model agrees. Pass for (ii) or (iii): net/trade > baseline (i) in BOTH 2014–18 and 2019–24 AND the ridge version agrees; then judged on
the Vest operation as an add / filter the same way.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (`research/programs/out/intraday_models.txt`; predictions in out/intraday_*.csv).
A. Range at 09:30: corr predicted vs actual +0.449 / +0.417 (ridge +0.502 / +0.416), holdout +0.524; actual range by predicted fifth
   0.74 → 1.40 ATR (2014–18), 0.81 → 1.30 (2019–24), 0.71 → 1.54 (holdout). **The size of the NY session IS forecastable** — London range
   (26 % of gain), the open vs yesterday's high (21 %), Asia range (12 %), r5 (10 %).
   Book (cascade + retest + gap rule) net R/trade by predicted-range fifth, narrow → wild: 2014–18 −0.06 / +0.16 / +0.18 / +0.02 / +0.13
   (avg +0.09); 2019–24 +0.28 / +0.18 / +0.13 / +0.25 / +0.19 (avg +0.20); holdout +0.32 / +0.29 / −0.16 / +0.05 / +0.02. No consistent
   slope → **size rule not met** (the stops and targets are already ATR-scaled, so the R a trade earns does not depend on how wide the day is).
B. Direction after the open (1 pt cost, ATR/trade; 2014–18 / 2019–24):
   09:50 — hit 49.9 / 53.6 %, corr +0.03 / +0.06; (i) go with the move +0.045 / +0.037; (ii) model +0.019 / +0.042; (iii) move + model agrees
   +0.042 / +0.070 (ridge +0.011 / +0.065) → (iii) loses to (i) in 2014–18: **fail**.
   10:30 — hit 51.8 / 51.3 %, corr 0.00 / +0.03; (i) −0.005 / +0.024; (ii) −0.008 / +0.022; (iii) +0.031 / +0.037 but ridge −0.015 / +0.022 →
   ridge does not agree: **fail**. Holdout (one look): 09:50 (iii) +0.205 on 58 (t 2.1), 10:30 (iii) +0.074.
**Verdict:** size forecastable, direction after the open not improved by a model beyond "go with the move" (the 09:50 agree-filter is
the nearest miss — better in 2019–24 and the holdout, worse 2014–18). Nothing changes the plan.
