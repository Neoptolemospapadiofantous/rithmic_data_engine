# Reversal plan — NQ / ES intraday (written 2026-10-22, BEFORE any run)

Founder: "and reversals. I think if we make a good plan for reversals we can identify them." Earlier tests called any flag AGAINST the
5-minute opening-range break a "reversal" (orb_flag_rev.cpp, orb_rev_opt.cpp, flag_adapt.cpp): +0.02R overall, only bull reversals
+0.2R, and a 1,152-cell optimisation did not survive walk-forward. Those flags were trend flags that happened to point the other way.
This plan defines a reversal by what makes it one: the market was trending one way, the trend broke, and we trade the first pullback in
the new direction. Everything below is fixed before the first run. Bull versions are written out; bears are the exact mirror.

## Structure (all on 5-minute bars on the 09:30 grid, overnight bars from 18:00 included, closed bars only)
- Swing high / low: a 5m bar whose high (low) is beyond the 2 bars on each side; known at the close of the 2nd bar after it.
- Downtrend: the last two confirmed swing highs AND the last two confirmed swing lows are falling (LH/LL).

## Setups (NY, the reversal signal at 09:45–14:00, entries before 15:00, flat 15:55, one position at a time per setup per market)
R1 CHoCH PULLBACK. In a 5m downtrend, a 5m close above the most recent swing high = change of character. Reversal low = the lowest low
   between that swing high and the CHoCH bar. Leg = reversal low → the highest high up to the CHoCH close. Entry: buy limit at the middle
   of the leg, valid 60 minutes; cancelled if the reversal low trades first. Stop: reversal low − 0.1 ATR.
R2 SWEEP AND RECLAIM. A level known before the day — yesterday's RTH low, the overnight low (18:00–09:30), the last week's low, and (only
   after 09:35) the 09:30–09:35 range low — is traded through by at least 0.05 ATR, then a 5m bar closes back above the level within 3
   bars (15 minutes) of the first bar through it. Entry: market at that close. Stop: the lowest low since the sweep − 0.1 ATR.
   One trade per level per day.
R3 SWEEP + CHoCH. R1 where the reversal low traded through one of the R2 levels by at least 0.05 ATR.
R4 EXHAUSTED CHoCH. R1 where the drop into the reversal low was at least 1.0 ATR from the session high so far AND the reversal low was at
   least 0.5 ATR below VWAP.

## Exits (each setup scored with every exit; none is picked after the run)
X1 2R target · X2 the leg's measured move (leg top + leg; R2: the reclaim close + 2 × (close − sweep low)) · X3 VWAP at entry, else 2R if
VWAP is not above the entry by 0.5R (R2, R4 only) · X4 hold to 15:55.

## Costs, data, controls
- Cost per trade 1.5 pts NQ / 0.5 pts ES; also reported at +1× cost. 1-minute bars from data/edge_lab/cache, 2014-01 → 2026-10; ATR = the
  day's ATR known before the open.
- Other-day twin: the same entry minute, side, stop and target distance in ATR on 5 random other sessions — what time of day and
  direction alone earn.
- Slices reported, not used to pick: registration time (before 10:30 / 10:30–12:00 / 12:00–14:00), bull vs bear, gap with / against,
  5m opening range broken the other way earlier (the old "reversal"), order flow at the reversal low (2023-03+ only: the 5m bar's buy
  volume share ≥ 0.55).

## Pass (a setup × exit passes only if ALL hold, on NQ)
1. ≥ 150 trades 2014-26 · 2. avg ≥ +0.15R after cost and PF ≥ 1.20 · 3. ≥ 9 of 13 years positive · 4. 2025-26 > 0 ·
5. still > 0 at +1× cost · 6. beats its other-day twin by ≥ +0.15R · 7. ES the same setup and exit ≥ +0.10R.
A pass earns a paper test, nothing more. With 16 cells tested, expect about one false pass at these thresholds by chance; ES confirmation
(7) is the main guard.

## Results (2026-10-22; research/programs/reversal_plan.cpp, out/reversal_plan.txt, ES out/reversal_plan_es.txt)
Implementation note made with the code: a sweep needs the 09:30 open above the level (a gap through a level is not a sweep). Slices use X1.
**Nothing passes. Every setup loses on NQ under every exit, and on ES too except R4.**
- R1 CHoCH pullback, NQ: ~155 trades a year, −0.11 to −0.13R, PF 0.79-0.83, 2-3 of 13 years positive; twin −0.05 to −0.08R (the setup is
  WORSE than a random entry at the same minute by 0.05-0.07R). ES −0.07 to −0.10R.
- R2 sweep and reclaim, NQ: ~160 a year, −0.05 to −0.06R, PF 0.90-0.93 (win 48 % at X2 — wins often, small); 2025-26 slightly positive
  (+0.03 to +0.07R) but 3-6 of 13 years. ES −0.08 to −0.10R.
- R3 sweep + CHoCH: worse than R1 on NQ (−0.16 to −0.18R); ES −0.05 to −0.11R.
- R4 exhausted CHoCH, NQ: 8 a year, −0.25 to −0.32R. ES +0.06R X1 / +0.06R X2 on 96 trades, 8/13 years, below every threshold.
- Slices (X1): no slice of R1-R3 is positive on NQ; bull vs bear, time of day, gap and the opening range breaking the other way first all
  lose. Order flow at the low (2023+): too few with buyers (8-41 trades).
**Conclusion:** catching the turn itself — the change of character, the swept low, the exhausted low — loses after costs on both markets.
The reversals that earned money earlier were FLAGS that formed after the market had already turned (orb_flag_rev.cpp: bull reversal flag
+0.24R; A-list + 5m structure +0.32R NQ / +0.24R ES). So a reversal is identified by waiting for the new direction's first A-grade flag,
not by trading the turn.

## Part 2 — double bottom / double top with confluences (written 2026-10-22 after Part 1's results, BEFORE its own run)
Founder: "so a reversal would look like a double bottom. or something along those line with other confluences (test)". Same 5m swings,
session, costs, controls and 7-point pass as Part 1. Bull written out, double top mirrored.
- PATTERN: the last two confirmed 5m swing lows L1 (earlier) and L2 are within 0.10 ATR of each other, 3 to 24 bars apart (15 min - 2 h);
  the neck (highest high between them) is >= 0.15 ATR above the higher low; the drop into L1 was >= 0.5 ATR (highest high of the 12 bars
  before L1). Bottom = the lower of L1, L2. Invalid once price trades below bottom − 0.1 ATR.
- ENTRIES (signal 09:45-14:00, entry before 15:00):
  D0 at L2's confirmation (market at the close of the bar 2 after L2), if the neck is not yet broken;
  D1 the first 5m close above the neck within 24 bars of the confirmation (market at that close);
  D2 after D1's close, a buy limit at the neck (the retest), valid 60 minutes, cancelled if the stop trades first.
  Stop: bottom − 0.1 ATR. Exits: X1 2R · X2 measured move (neck + (neck − bottom)) · X4 hold to 15:55.
- CONFLUENCES, fixed: C1 a level at the bottom — yesterday's / overnight / last week's / 09:30-09:35 low within 0.05 ATR above the
  bottom or traded through by it; C2 higher second low (L2 >= L1); C3 volume divergence (5m volume of the L2 bar < the L1 bar's);
  C4 stretched (the bottom >= 0.5 ATR under VWAP). Scored: every pattern, each C alone, and the pre-set combos ">= 2 of C1-C4" and
  ">= 3 of C1-C4". The pass is judged on those two combos and the plain pattern only (3 sets × 3 entries × 3 exits = 27 cells; expect
  one to two false passes by chance, ES confirmation is the guard).

### Part 2 results (2026-10-22; research/programs/reversal_double.cpp, out/reversal_double.txt, ES out/reversal_double_es.txt)
**Nothing passes; every judged cell loses on NQ and on ES.** NQ: 2,342 patterns in 2,581 days.
- Every double bottom / top: D0 (at the second low) −0.04 to −0.09R, ~100 a year; D1 (close above the neck) −0.05 to −0.06R, ~65 a year,
  win 59 % at the measured move and still PF 0.82; D2 (neck retest) −0.05 to −0.09R. 2-6 of 13 years positive; the other-day twin is about
  as bad (−0.03 to −0.07R), so the pattern adds nothing over a random entry at the same minute.
- Confluences make it WORSE, not better: >= 2 −0.03 to −0.11R, >= 3 −0.06 to −0.20R on NQ; ES the same. Each confluence alone loses
  (C4 stretched under VWAP worst, −0.10 to −0.21R). C1 (a level at the bottom) held on ~94 % of patterns — almost every intraday low is
  near one of those levels, so it filters nothing.
- ES: −0.05 to −0.18R; 2025-26 D1 is positive on ES (+0.14 to +0.24R on ~70) but 3-4 of 13 years — the latest period, not a pattern.
Conclusion stands from Part 1: trading the bottom loses; the reversal that pays is the new direction's first A-grade flag after the turn.

## Part 3 — reversals combined with flags (written 2026-10-22, BEFORE its own run)
Founder: "and combine reversal with flags? test ideas in that as well". Signals and trades exactly as already built: flags = flag_hhhl
export (3-5m, flag-mid · measured move); reversal signals = Part 1 R1 / R2 and Part 2 D1 (exit X2). Same side means the same direction.
- C1 FLAG AFTER A REVERSAL: a flag registered within 120 minutes after a same-side reversal signal (R1, R2 or D1) that day; vs flags with no
  such signal. Sets: every flag, A-list, A-list + 5m HH/HL; also split into flags WITH the 5m opening-range break vs AGAINST it (the
  "reversal flags" of orb_flag_rev.cpp).
- C2 REVERSAL AFTER A FAILED FLAG: a reversal signal fired within 60 minutes after an opposite-side flag trade was stopped out; vs reversal
  signals without one. Per setup R1, R2, D1.
Pass (each C1 set / C2 setup): >= 100 trades, the "after" group >= +0.15R better than the "without" group on NQ in both 2014-19 and
2020-26, and >= +0.10R better on ES. One position at a time within each group.

### Part 3 results (2026-10-22; research/programs/reversal_flag_combo.cpp, out/reversal_flag_combo.txt)
**0 of 9 combinations pass.**
- C1, the opposite of the idea: a same-side reversal signal in the 2 hours before a flag makes the flag WORSE, on both markets. Every flag
  after a signal −0.070R NQ / −0.071R ES vs +0.176 / +0.115R without; A-list −0.080 / −0.009 vs +0.316 / +0.200; flags with the 5m break
  −0.198 / −0.123 vs +0.239 / +0.141. A recent reversal signal marks a two-way, choppy day. Reversal flags (against the break) after a
  signal +0.131R NQ vs −0.127R without, but the 2020-26 half flips (−0.103) and ES +0.023 vs −0.001 — no.
- C2: a failed opposite flag before a reversal signal changes nothing (R1 −0.128 vs −0.128 NQ; R2 after a failed flag −0.410 on 28; D1
  −0.001 vs −0.077, ES −0.080 vs −0.065).
Seen, not planned: "skip a flag when a same-side reversal signal fired in the 2 hours before" holds on both markets for every flag, A-list
and with-break flags (−0.19 to −0.44R gap) — a candidate filter for its own pre-registered walk-forward test.

## Part 4 — the skip filter, tested on its own (written 2026-10-22 after Part 3, BEFORE its own run)
Founder: "test". Filter: skip a flag registered within W minutes after a same-side reversal signal (R1, R2 or D1) that day. Found in Part 3
on all years at W = 120, so every check below is built to catch a fluke:
- T1 WALK-FORWARD: for each test year 2017-2026, use the filter that year only if, over all earlier years, flags after a signal averaged
  >= 0.10R worse than flags without (else take every flag). Compared with no filter, out of sample, per set (every flag; A-list + 5m
  HH/HL + with the break, both sides).
- T2 TIME OF DAY: the after-vs-without gap inside each registration bucket (before 10:30 / 10:30-12:00 / 12:00-14:00).
- T3 WINDOWS: W = 60 / 90 / 120 / 180.
- T4 NEW FLAGS: the 1-2 minute flags (flag_hhhl `export tf15`, charts 1-2 only — never used to find the filter).
Pass: T1 filtered >= +0.05R per trade better than unfiltered on BOTH markets for every flag, and not worse for the main rule; T2 the gap is
negative in >= 2 of 3 buckets on both markets; T3 negative at every W on both; T4 negative on both. All four, or the filter is dropped.

### Part 4 results (2026-10-22; research/programs/skip_filter.cpp, out/skip_filter.txt)
**By the rule written above, the filter is DROPPED: T1 fails; T2, T3 and T4 pass.**
- T1 walk-forward 2017-26: every flag NQ +0.200R vs +0.165R unfiltered (+0.035R a trade, total +278R vs +259R), ES +0.125 vs +0.095
  (+0.030R) — both under the +0.05R bar; the main rule (A-list + HH/HL + break) NQ +0.479 vs +0.432 (+0.047R) but ES +0.287 vs +0.301
  (−0.013R). The main rule rarely meets a prior reversal signal (~27 of 362 trades), so there is little for the filter to remove.
- T2: the gap is strongly negative before 10:30 (NQ −0.537 vs +0.380R, ES −0.435 vs +0.254) and 10:30-12:00, but REVERSES after 12:00 (NQ
  +0.182 vs −0.051, ES +0.166 vs −0.084).
- T3: negative at every window on both markets (W 60 −0.104 vs +0.147 NQ, −0.128 vs +0.113 ES … W 180 −0.104 vs +0.218, −0.073 vs +0.121).
- T4 fresh flags (1-2m, never used to find it): every flag NQ −0.279R vs +0.103R (−0.38R), ES −0.210 vs +0.019 (−0.23R).
Reading: the effect is real for the broad flag sets (fresh data agrees on both markets) and concentrated before 12:00; it adds almost nothing
to the A-list + HH/HL + break rule, which already avoids those flags. Usable as a skip rule only if the broader / 1-2m flag sets are traded.
