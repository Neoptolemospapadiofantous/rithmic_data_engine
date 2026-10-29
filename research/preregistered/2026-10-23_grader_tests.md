# Three grader tests from the literature search — pre-registered 2026-10-23 08:10 Cyprus, BEFORE the run

Base candidates: wide15 4–5m flags, pole ≥ 3× (x2), pullback 5–38 % (x4), filled (ok), NQ + ES, 2010-2026 exports (W15); the
BOOK2 grade (A/B = top two terciles of the walk-forward 14-feature score) is the baseline selection. Net R = the file's r.
Walk-forward by year: models fit on all earlier years (2010 →), scored on the test year 2014–2026; thresholds from the
training years only. Halves 2014-19 / 2020-26. Costs as exported. Twins not needed: the comparison is selection vs selection on
the same trades.
- **G1 uniqueness weighting (López de Prado):** ridge regression on the 35 features → net R clipped [−1.5, 5]; selection = test-year
  flags whose score ≥ the training top-third cut. (a) equal weights; (b) each training flag weighted 1 / (number of same-market
  flags on its day whose trade overlaps it in time); (c) training on the first filled flag of each day only. PASS = (b) or (c)
  beats (a) by ≥ +0.03R out of sample in BOTH halves at a trade count within 25 % of (a).
- **G2 early stop-out classifier:** label = stopped (r ≤ −0.9) within 15 minutes of the fill. Logistic regression on the 35
  features, walk-forward; skip test-year flags whose predicted probability is above the training 75th percentile. Applied ON TOP
  of the BOOK2 A/B selection. PASS = kept net R ≥ BOOK2 + 0.03R in both halves AND dropped net R ≤ BOOK2 − 0.03R in both halves.
- **G3 order flow in the pattern (flow days 2023-03 →):** on BOOK2 trades with flow data (x16 = 1): buyers in the pole (x15, aggressor
  buy share ≥ 0.55, mirrored for bears) and sellers in the flag (x13). Kept vs dropped net R, halves 2023-03 → 2024-10 /
  2024-10 → 2026-10. PASS = kept ≥ dropped + 0.05R in both halves with ≥ 150 trades per half per set.
Output research/programs/out/grader_tests.txt. Nothing changes any config.

## Results (2026-10-23; research/programs/out/grader_tests.txt; 8,714 candidates, mean overlap 1.84, 20 % early stop-outs)
| test | selection | n/yr | net R all | 2014-19 | 2020-26 | verdict |
|---|---|---|---|---|---|---|
| baseline | BOOK2 (A or B) | 486 | +0.138 | +0.068 | +0.207 | |
| baseline | grade A only | 311 | +0.193 | +0.114 | +0.268 | |
| G1 (a) | ridge, equal weights, top third | 193 | +0.208 | +0.116 | +0.304 | |
| **G1 (b)** | **ridge, uniqueness-weighted** | 193 | **+0.239** | **+0.160** | +0.318 | **FAIL by the rule** (+0.044 / +0.014 vs (a); needed ≥ +0.03 both halves) — but the best cell |
| G1 (c) | ridge, first flag of the day | 184 | +0.213 | +0.134 | +0.289 | fail |
| G2 | BOOK2 kept, low early-stop risk | 363 | +0.134 | +0.122 | +0.146 | **FAIL, inverted**: the dropped quarter earns +0.374 in 2020-26 |
| G2 | BOOK2 dropped, high early-stop risk | 123 | +0.151 | −0.098 | +0.374 | |
| G3 | sellers in the flag (x13) vs not | 456 vs 372 | +0.206 vs +0.154 | +0.30 vs +0.27 | +0.09 vs +0.04 | fail (+0.03 / +0.05, needed ≥ +0.05 both) |
| G3 | buyers in the pole (x15) | 5 | — | — | — | not testable: the feature is almost never true on BOOK2 flags |
| G3 | volume dry-up (x14) vs not | 410 vs 418 | +0.151 vs +0.214 | | | fail — dry-up flags are WORSE |
Read: (1) **Uniqueness weighting is the only idea with a pulse**: +0.03R over the equal-weight grader out of sample, +0.044 in
the hard half, at the same trade count — under the pre-registered line by 0.016 in 2020-26. It is also the one modelling change
with a measured reason (80 % overlap). Worth carrying into the production grader as a re-fit of the 14-feature weights with
1/overlap sample weights — a paper candidate, not a book change. (2) **Predicting the early stop-out backfires**: the flags
the model flags as risky are the high-volatility ones, and in 2020-26 those are the big winners (+0.374R). The stop-out and
the 3R winner come from the same volatility. (3) **Order-flow pattern features add nothing stable**: sellers-in-the-flag is a
+0.03–0.05R tilt, pole buyers is near-empty on BOOK2, volume dry-up (the textbook "flag on falling volume") is WORSE than
no dry-up — the textbook claim is wrong here. (4) The 35-feature ridge at top-third (+0.21–0.24R, 193/yr) now matches or
beats grade A (+0.19R, 311/yr) per trade but with 40 % fewer trades — same frontier as flag_model found. Nothing changes any config.
