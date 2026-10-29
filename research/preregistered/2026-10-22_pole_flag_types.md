# Pole types × flag types, order flow, shape models — pre-registered 2026-10-22

Founder: "do more tests on how we can identify better flags. on any of the 5 timeframes. we have the pole and we have the flag.
split the research to see what types of poles and what type of flags are the best … Possible models can identify them?" → "do all".

Written BEFORE any of the runs below. Program: `research/programs/flag_types.cpp`. Data: the two wide15 exports
(`flag_hhhl NQ|ES export FILE wide15` — every 1-5m flag, pole >= 2× the average bar, pullback 5-70 %, flag-mid limit, stop flag low
− 0.1 ATR, measured-move target, flat 15:55, net R after cost NQ 1.5 / ES 0.5 pt) and the 1m cache `data/edge_lab/cache` (read-only).
Bears are mirrored (prices negated) so every measurement reads the same for both sides.

## The new control: a flag must beat its own day

Earlier work found the A grade picks DAYS, not flags (its same-day random entries earned the same). Every result here is shown as
**edge = flag R − twin R**, twin = 5 random entry minutes in the 60 minutes after the flag registers (post-registration, so no
look-ahead — the 09:45-14:00 whole-day twin is look-ahead and is not used), same side, same stop and target distances in points, same
cost, simulated on 1m bars to 15:55 (stop first if both in one bar). A better flag type = a higher edge, not just a higher R.

## Measurements, all known at the fill minute (1m bars that closed before the entry minute)

Pole (pole low → pole top, located on 1m bars from the exported pole low / high):
- P-SIZE pole / ATR · P-SPEED pole minutes · P-STRAIGHT (pole / sum of 1m ranges along it) · P-SPIKE largest 1m bar's range / pole
- P-ORIGIN the pole crossed a level from below on its way up: overnight high, prior RTH high, 09:30-09:35 high, or session VWAP
- P-TIME pole top 09:30-10:00 / 10-11 / 11-12 / 12-14 / 14-16 · P-GAP pole with / against the 09:30 gap
- Pole archetype: SPIKE (largest 1m bar >= 40 % of the pole) · FAST (not spike, pole minutes <= 2010-13 median for its chart) · SLOW

Flag (pole top → last closed 1m bar before the fill):
- F-DEPTH (top − lowest low) / pole · F-DRIFT slope of the 1m closes × bars / pole (total drift as a share of the pole)
- F-BARS average 1m range in the flag / average 1m range in the pole · F-DOWN share of down 1m bars · F-LEN flag minutes / pole minutes
- F-VOL flag / pole volume per minute
- Flag archetype: TIGHT (depth <= 0.30 and bars <= 0.6) · LOOSE (depth <= 0.30, bars > 0.6) · MEDIUM (0.30-0.50) · DEEP (> 0.50);
  each split CHANNEL (drift < −0.25) or not
- Charts: 1-2m · 3m · 4-5m

Continuous measurements are cut at their 2010-13 terciles (no later data used for cuts).

## T1 — pole type × flag type

Marginal tables (each measurement's buckets) and the grid pole archetype (3) × origin (2) × flag archetype (8) × chart group (3),
per market: n, R, twin R, edge, 2014-21 and 2022-26 separately.
Selection on 2014-21 only: cells with n >= 60 on BOTH markets in 2014-21, ranked by the average of the two markets' edge; top 10.
**PASS** (a cell, or the union of the top 10): in 2022-26 its edge beats the whole universe's edge by >= +0.10R on BOTH markets with
n >= 30 on each, AND used on BOOK2 (keep BOOK2 trades in passing cells / drop BOOK2 trades in "bad" cells = 2014-21 edge <= −0.15R on
both markets) Select Daily 50K payouts per eval (`flag_prop_ev A B prob`) are not lower than BOOK2 unchanged.

## T2 — order flow (NQ only)

The one unplanned hint of orderflow_check: session delta (aggressor buy − sell volume 09:30 → the fill) AGAINST the trade +0.64R on
70 flags. Re-tested on every wide15 flag on NQ flow days (2023-03 → 2026-03, 2026-09+), with vs against, R and edge.
**PASS**: against beats with by >= +0.15R edge in BOTH halves of the flow period (split at 2024-10-01) with n >= 50 each.
Not an independent test — same months as the hint (only 2026-09+ is fresh and it is tiny); a PASS only earns a paper tag.

## T3 — models that learn shape, not the day

Walk-forward by year (trained on every earlier year from 2010, scored on 2014-2026), per side, target net R clipped [−1.5, 5]:
- **SHAPE-GBM** (flag_model's GBM: 100 depth-2 trees, lr 0.05, leaves >= 60) on pole + flag measurements only (the list above + chart
  minutes, pole / avg bar, pullback depth, pole bars, flag / pole volume) — no gap, VIX, ATR trend, last sessions, time of day, day range
- **PATH-kNN**: each flag's normalised path = the 1m closes from pole low to the last closed bar before the fill resampled to 16 points
  (0 = pole low, 1 = pole top) + chart minutes; score = mean net R of the 50 nearest earlier-year flags of the same side
- Founder eye marks as labels: only if >= 30 marks exist (today there are 2 lines in data/flag_eye.jsonl → reported, not run)
Selection = test-year flags above the training scores' top third.
**PASS**: in 2022-26 the model's top third beats the universe edge by >= +0.10R on BOTH markets AND beats the A-list's edge
(grade 1) on both markets, AND BOOK2 with the model's top two thirds in place of grade A/B (4-5m rows) keeps Select Daily 50K payouts
per eval >= BOOK2 unchanged.

## Not changed by this test
BOOK2, the /flags page, the paper fleet. Anything that passes is a candidate for a paper tag, nothing more.

## Results (2026-10-22, run after this file was written; out/flag_types.txt, out/flag_types_ev.txt)

47,857 trades measured (1 skipped). Universe edge (flag R − twin R) 2014-26: NQ +0.047, ES +0.036; BOOK2 +0.136 / +0.122
(BOOK2's twins are positive, +0.045 NQ: part of BOOK2's R is the day, as with the A grade).

**T1 FAIL.** None of the top 10 grid cells (picked on 2014-21) passes in 2022-26; their union drops from +0.151 / +0.081 edge
(2014-21) to −0.003 / +0.020 (2022-26) — the same shrinkage every grid search here has shown. Only 1 cell met the "bad" rule.
Account: T1keep $1,411 per eval (Select Daily 50K) and T1skip $4,268 vs BOOK2 $4,593 — both lower.
Measurements that hold on BOTH markets in BOTH periods (descriptive, not adopted):
- steady pole beats a pole made of one big 1m bar: low spike NQ +0.095 / ES +0.089 edge vs high spike +0.004 / −0.015
- big pole (vs ATR and vs the average bar): top tercile +0.12-0.13 NQ / +0.09 ES vs bottom ≈ 0 (BOOK2 already needs pole >= 3×)
- slower poles beat fast ones (NQ +0.084 vs +0.020, ES +0.078 vs −0.009)
- with the gap beats against it (NQ +0.071 vs +0.026, ES +0.066 vs +0.009)
- 4-5m flags beat 1-2m (NQ +0.086 vs +0.022, ES +0.069 vs +0.018)
Flag measurements at the fill (depth so far, drift, bar size, down bars, length, volume) show no consistent ordering on both markets.
Afternoon flags have lower R but the same edge — the afternoon loses because of the day, not the flag.

**T2 FAIL.** Every wide15 flag: against − with = −0.037 edge (−0.053 / −0.018 by half). BOOK2: −0.111 / +0.321 by half (n 63 / 49).
The +0.64R hint does not replicate on the wider set.

**T3 FAIL.** SHAPE-GBM does rank flags out of sample (top vs bottom third edge NQ +0.099 vs +0.034, ES +0.112 vs −0.024; 2022-26
top third +0.078 / +0.149, above the A-list's +0.055 / +0.085) but misses the +0.10 over the universe on NQ (+0.078 vs +0.142 needed).
It leans on pole spike 14 %, pole / ATR 9 %, pole / avg bar 8 %, pole straightness 7 %. PATH-kNN is ≈ the universe (+0.065 / +0.055).
Account: BOOK2 regraded by SHAPE-GBM $4,097, by PATH-kNN $3,290 vs $4,593 — fewer trades (2.06 / 2.00 a day vs 2.44), same R.
Founder eye marks: 2 lines — not enough to train on.

**Verdict:** nothing changes. The flag's own shape adds a little (steady, big, slow poles), but every way of using it removes
trades, and payouts per eval follow trades a day.
