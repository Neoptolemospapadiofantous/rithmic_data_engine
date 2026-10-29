# 2026-10-24 — hidden-Markov REGIME filter for BOOK2 NQ (PRE-REGISTERED before any run)

Founder: "lets do the regime next" (after: BOOK2's flat years — 2016, 2019, 2025-26 — have a normal win rate but missed targets).

## Model
Gaussian hidden Markov model, diagonal covariance, K ∈ {2, 3} states, own numpy Baum-Welch (20 random starts, best likelihood),
research/programs/book2_hmm_regime.py. Daily observations from NQ 1-minute cache bars, RTH 09:30-16:00 ET:
- log return close-to-close, - log(range / ATR20), - trend efficiency |close − open| / range, - |overnight gap| / ATR20.
Standardised with 2010-2019 statistics. FIT on 2010-07 → 2019-12 only. Regime for trading day d = the PREDICTED state
P(s_d | observations up to d−1) = forward-filtered probabilities of d−1 times the transition matrix — never smoothed, never Viterbi
(both use future days).

## Test
BOOK2 NQ trades 2014-2026 (wide15 export). Per K: the WORST predicted state = the one with the lowest mean R on 2014-2019 trades
(chosen there). Validation 2020-01 → 2026-10, read once: mean R of the other states minus the worst state > 0 with a day-clustered
bootstrap 90 % CI above 0, and > 0 in BOTH 2020-22 and 2023-26. Holm over the two K values.
Only on a pass: account model (mtm, NQ, $1,000 daily stop) for (a) skip worst-state days, (b) 1 micro on worst-state days and 2
otherwise — each against 17 random controls with the same share of days / trades changed, as in the 2026-10-24 sizing test.
Also reported: what each state looks like (mean volatility, efficiency, return), how long states last, and the share of days in each.

## Results (2026-10-24; out research/programs/out/book2_hmm_regime.txt)
- K=2: state 0 = volatile days (range/ATR 1.19, gap/ATR 0.55, efficiency 0.61, 36 % of days), state 1 = calm (0.76 / 0.22 / 0.36).
  Worst on 2014-19 = volatile (0.038 vs 0.144 R). 2020-26: calm − volatile = +0.187 R, 90 % low −0.048, p 0.092; 2020-22 +0.092,
  2023-26 +0.307; Holm 0.184 → FAIL (near miss, same direction in both later halves).
- K=3: degenerate (one state ~0 % of days); worst state (8 %, big down days) −0.194 → +0.149 R later, Holm 0.309 → FAIL.

## Part 2 (founder sent a bull / side / bear transition-matrix picture) — VISIBLE Markov chain, PRE-REGISTERED before the run
States per DAY (RTH): bull if (close − open) ≥ +0.3 × ATR20, bear if ≤ −0.3 × ATR20, else side. Per WEEK: the same on the weekly
open-to-close vs the 20-week average weekly range. Transition matrices with counts for 2010-2019 and 2020-2026; persistence tested
against independence (chi-square; diagonal vs the column share). Flags: BOOK2 trades labelled WITH (long in a predicted bull / short
in a predicted bear), AGAINST, or SIDE using the PREVIOUS period's state (yesterday / last week). Choose on 2014-19 nothing (fixed
rule), read 2020-26: WITH − AGAINST > 0 in both halves with day-clustered 90 % CI > 0 → account test vs random controls.

### Part 2 results (out research/programs/out/book2_markov_matrix.txt)
NQ is NOT persistent like the picture: same-state repeat 28-40 % daily, 22-35 % weekly, close to the column shares; chi-square p
0.004-0.015 comes from mild REVERSAL (after a bear day → bull 38-41 % vs 34-36 % base; after a bear week → bull 47-51 % vs 38-42 %).
Flags WITH the previous day's state: −0.108 R vs AGAINST in 2020-26 (flips) → FAIL. WITH the previous week: +0.260 R, 90 % low −0.015,
2020-22 +0.458 / 2023-26 +0.044 → FAIL (near miss).
POST-HOC (not pre-registered): flags after a SIDEWAYS day 0.278 R (2014-19) / 0.498 R (2020-26) vs 0.026 / 0.041 (with) and −0.027 /
0.149 (against) — matches the K=2 HMM near miss (calm days better).

## Part 3 — out-of-market check of the post-hoc "after a sideways day" rule on ES (PRE-REGISTERED before the run)
Rule fixed from NQ: previous RTH day |close − open| < 0.3 × ATR20 ("side"). ES BOOK2 trades 2014-2026 (wide15 export), ES 1-minute
cache. Pass = side − not-side mean R > 0 on ES in 2014-19 AND 2020-26, day-clustered 90 % CI > 0 over 2014-2026. Only then: the
account test on NQ (+ES) — trade only after a sideways day, and 2 micros after side / 1 otherwise — against random controls.

### Part 3 results
ES out-of-market check (out book2_after_side_es.txt): after a sideways day +0.166 / +0.265 R vs otherwise −0.071 / +0.104 (2014-19 /
2020-26); diff +0.237 / +0.161; 90 % low +0.015, p 0.036 → PASS.
Account test, NQ + ES, mtm, 1 micro base, $1,000 daily stop, 10 random controls each (book2_after_side_accounts.py; out
book2_after_side_accounts.txt):
- SKIP non-side days: total $62,614 vs random $20-42 K (beats 10/10), drawdown $2,635 vs $3.3-6.0 K — the selection is REAL — but the
  Topstep eval EV is only $263 (random $17-170; base with all trades $982): too few trades for an eval. Not for evals.
- SIZE 2 micros after a sideways day, 1 otherwise: total $151,526 vs random $109.7-131.8 K (beats 10/10), EOD drawdown $7,882 vs
  $8.2-10.1 K (lower than all 10), worst month −$4,472 vs −$4.6 to −5.8 K (better than all); Topstep EV $1,188 vs random $885-1,273
  (median-ish), 2020-25 starts $1,431 vs random $791-1,312 (beats 10/10), paid 28 % vs 22-28 %.
Verdict: the first regime rule in this programme to clear an out-of-market check AND random controls. Use: size up after sideways
days for continuous / funded accounts; for evals it is level with random sizing overall but best on recent starts. Next: forward
paper tracking (tag on /flags), then a live decision by the founder.
