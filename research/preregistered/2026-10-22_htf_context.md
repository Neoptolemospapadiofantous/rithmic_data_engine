# Higher-timeframe context done properly — nesting, structure, trend at the pole's start; filter / size / direction — pre-registered 2026-10-22

Founder: "create a plan on how to test this I think you are making a mistake" → plan (four possible mistakes: trend instead of nested
flags, circular timing, a context-filtered flag set, sizing only) → "do all". Written BEFORE any run. Program: new
`research/programs/htf_context.cpp`; account = `flag_prop_ev … mtm`.

## Higher-timeframe (HTF) picture, from HTF bars that CLOSED BEFORE THE 5m FLAG'S POLE STARTED
Timeframes: 15m, 30m, 1h, 4h (whole-session buckets from 18:00, chained across days) and daily (completed RTH sessions before today).
Pole start = the 1m bar of the 5m flag's pole low (as flag_types.cpp locates it). Mirrored to the 5m flag's side.
- FLAG_T (nesting): on the last 20 closed HTF bars, top = the highest high of the last 12, pole low = the lowest low in the 12 bars up to
  the top, pole = top − pole low; an HTF flag is "in progress" when pole >= 2.5 × the average HTF bar range, 1-8 bars have closed after the
  top, the pullback since the top is 5-62 % of the pole and the last close is still below the top. AGREE = such a flag in the 5m flag's
  direction; DISAGREE = one in the opposite direction (and none in ours); NONE otherwise.
- TREND_T: the last closed HTF bar's close above the EMA20 of HTF closes (below for bears) — at the pole's start, not at entry.
- ROOM_T: the 5m flag's measured-move target below (bull) the highest high of the last 20 closed HTF bars, i.e. the target sits inside the
  HTF range rather than beyond its recent extreme. Favourable side chosen on 2014-19.

## Two flag sets
- A: every wide15 flag on the 4-5m charts with a trade (grades A/B/C, pole >= 2×, pullback 5-70 %), trade level only — R and edge over
  post-registration twins, by market and half (2014-19 / 2020-26). The wide15 detection still requires the flag above the open and VWAP; a
  fully unfiltered detector is not built here (stated limit).
- B: BOOK2 — account tests.

## Three uses (account, set B), each per feature (FLAG_T agree, TREND_T favourable, ROOM_T favourable; 15 features)
- SIZE: 2 micros when the feature is favourable, else 1 (replaces gap sizing) — judged against the candidate (gap sizing) and against flat
- FILTER: the candidate (gap sizing) without trades where the feature is unfavourable (for FLAG: without DISAGREE trades)
- STRICT (direction rule): the candidate keeping only trades where the feature is favourable (for FLAG: only AGREE)

## Honest out-of-sample rule
Everything that is chosen (favourable sides) uses 2014-19 only. **PASS** = on 2020-25 (unseen by the choice), conservative marking, EV >=
the candidate on at least two of topstep_50k / lucid_flex_50k / select_daily_50k, AND trade-level R of the favourable group above the rest
in 2020-26 on BOTH markets, AND still >= the candidate with +1 NQ point extra cost on those two plans. 45 account variants + 15 trade-level
tables are tried; that count is reported with any pass.

## Shuffle control
For every feature: favourable-vs-rest difference in mean net R (set A, 2014-26) against 1,000 shuffles of the feature within year × market.
A pass must also have a shuffle p < 0.05 — otherwise the context picks days, not information.

## Results (2026-10-22; out/htf_context.txt, htf_context_mtm.txt)
Measured at the pole's START, the higher timeframe DOES carry information (my earlier at-entry trend test was circular): set A shuffle
p — TREND 15m 0.002, 30m 0.002, 1h 0.008, 4h 0.062, daily 0.618; FLAG (nesting) 1h 0.004, daily 0.005, 15m 0.239, 30m 0.096, 4h 1.000
(4h nesting is worse); ROOM (target beyond the HTF range is the better side) 1h 0.000, 4h 0.000. Set A cells in out/htf_context.txt.

Account (vs the candidate, gap sizing; Topstep | Lucid | Select Daily 2014-19 → 2020-25; CAND $1,502 → $1,540 | $1,007 → $1,336 |
$1,237 → $1,204). For the binary features FILTER and STRICT are the same variant (≈ 35 distinct variants tried).
**PASS: skip flags whose 15m trend at the pole's start is against them** (FILT/STRICT_TREND_15m): $1,543 → $1,648 | $1,084 → $1,479 |
$1,251 → $1,320 — above the candidate in BOTH halves on all three plans and with +1 pt cost; drops ~2,500 of 6,320 BOOK2 trades.
**PASS by the letter only: skip flags whose target sits inside the last 20 1h bars' range** (FILT/STRICT_ROOM_1h): $1,254 → $1,833 |
$888 → $1,611 | $686 → $1,376 — big in 2020-25 but BELOW the candidate in 2014-19 on all three plans; not trusted as a rule.
Near misses (fail the shuffle test): FILT_FLAG_30m (beats the candidate on all three, +1 pt too; p 0.096), FILT_ROOM_30m ($1,490 → $1,931;
p 0.112). SIZE variants (2 micros instead of gap sizing) never beat the candidate in 2020-25 except SIZE_FLAG_30m on two plans (p 0.096).
STRICT FLAG (only nested flags) has too few trades ($0-494). Daily trend / daily room fail.
