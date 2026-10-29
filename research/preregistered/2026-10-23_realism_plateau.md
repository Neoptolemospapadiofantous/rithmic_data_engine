# Live-realism, time of day, rule plateau — pre-registered 2026-10-23

Founder: "more tests". Written BEFORE any run. Candidate = BOOK2 + gap sizing + the 15m trend filter (htf_context
FILT_TREND_15m files). Account = `flag_prop_ev … mtm` (2 open per market, $500 daily stop), Topstep / Lucid / Select Daily 50K,
2014-19 → 2020-25 per eval, conservative marks, +1 pt cost. New program `research/programs/flag_realism.cpp` (F1, F2); F3 adds env
overrides to flag_prop_ev's BOOK2 rule. One variant file at a time (`VARIANT=`), written over the same scratch pair; nothing deleted.

## F1 — Does the limit really fill? (queue position)
The flag-mid limit is assumed filled the minute price TOUCHES it. Live, a resting limit at the touch often does not fill (we are at the
back of the queue) — and the touches that never trade through are likely the best trades (price turned at our level). Re-run every
candidate trade with the page's fill loop from registration, but filled only when the 1m low goes ≥ T ticks through the limit
(T = 1, 2; tick 0.25 on both), price min(open, limit), stop = lowest low before the fill − 0.1 ATR, target pole top + pole, exits
re-simulated (stop first in the fill bar, flat at the 15:55 open). A trade that never trades through is dropped (no backup). Check: T = 0
must reproduce the candidate's trades (fill minute, exit, R within 1 %). Report: share of trades that are touch-only and their mean R,
then the account. Read, no pass/fail: if T = 1 keeps >= 80 % of the candidate's 2020-25 EV on 2 of 3 plans, the backtest is not leaning on
touch fills; if not, live must use a marketable entry or the EV is overstated.

## F2 — Time of day and day of week (choose on 2014-19, judge on 2020-25)
Entry-time buckets 09:30-10:30, 10:30-12:00, 12:00-13:30, 13:30-15:00 and weekdays Mon-Fri. Trade level: R and edge over
post-registration twins per bucket and half. Variant F2tod = skip every time bucket whose pooled (NQ+ES) 2014-19 mean R is below 0;
F2dow = same for weekdays. PASS = >= the candidate on 2020-25 on 2 of 3 plans and with +1 pt. If no bucket is negative on 2014-19 the
variant equals the candidate and is reported as "nothing to skip".

## F3 — Rule plateau (sensitivity, descriptive)
BOOK2's thresholds were chosen once (pole >= 3x, pullback 5-38 %). Grid on the gap-sized book WITHOUT the 15m filter (the filter is only
computed for BOOK2 flags): pole >= {2.5, 3, 3.5} x pullback max {30, 38, 50} %, everything else unchanged. Read: a robust rule sits on a
plateau — neighbours within ~25 % of the centre's 2020-25 EV. A cliff around (3, 38) = the rule is tuned to noise. No rule change will be
made from this grid (that would be selection on 2020-25).

## Results (run 2026-10-23, after the text above)
Outputs: `research/programs/out/flag_realism.txt` (trade level), `out/flag_realism_mtm.txt`, `out/flag_plateau_mtm.txt`. Per eval,
2014-19 → 2020-25, conservative; % = 2020-25 vs the candidate.

**F1 — fills.** Check: T = 0 reproduces 3,837 of 3,837 candidate trades. Touch-only trades (the limit touched but never traded 1 tick
through) are rare: NQ 1 %, ES 5 % in 2014-19 / 1 % in 2020-26; they are better than average (mean R +0.29…+0.73), as expected.
| | Topstep | Lucid | Select Daily |
|---|---|---|---|
| candidate | 1543 → 1648 (+1pt 1415) | 1084 → 1479 (1281) | 1251 → 1320 (1218) |
| 1 tick through | 1254 → 1608 (1478) 98 % | 927 → 1442 (1355) 97 % | 1200 → 1351 (1231) 102 % |
| 2 ticks through | 1237 → 1482 (1412) 90 % | 903 → 1361 (1291) 92 % | 1229 → 1322 (1164) 100 % |
Read: >= 80 % kept on all 3 plans → the backtest is NOT leaning on touch fills; a resting limit is fine live. 2014-19 drops ~15-20 % at
T = 1 (ES had more touch-only winners then), so expect somewhat less than the headline.

**F2 — time / weekday.** 13:30-15:00 was the only negative time bucket on 2014-19 (pooled −0.145 R; 2020-26 it is positive); skipping it:
1603 / 1412 / 1286 (97 / 95 / 97 %) → FAIL. Wednesday and Thursday negative on 2014-19 (both strongly positive on NQ 2020-26); skipping them:
274 / 259 / 135 → FAIL (fewer trading days, the target takes too long). Keep trading every hour and every weekday.

**F3 — plateau (gap-sized book, no 15m filter; centre = 1540 / 1336 / 1204).** 2020-25 per eval (2014-19):
| pole \ pullback max | < 30 % | < 38 % | < 50 % |
|---|---|---|---|
| >= 2.5x | T 1538 (1999) · L 1420 (1607) · S 950 (1643) | T 2042 (1130) · L 1779 (818) · S 1610 (596) | T 1769 (1340) · L 1526 (1005) · S 1709 (580) |
| >= 3x | T 1334 (2014) · L 1222 (1651) · S 857 (1683) | **centre** | T 1347 (1838) · L 1182 (1303) · S 1079 (1376) |
| >= 3.5x | T 1503 (1602) · L 1319 (1319) · S 1088 (1323) | T 1328 (1323) · L 1116 (835) · S 1058 (987) | T 1109 (1530) · L 1017 (1040) · S 841 (1261) |
Read: against the pre-registered yardstick (neighbours within ~25 %) this is only PARTLY met — neighbours sit at 70-142 % of the centre, wider than ±25 %. But there is no cliff: all 9 cells are clearly positive. But the best cell flips between halves
(2014-19 favours pullback < 30 %, 2020-25 favours pole >= 2.5x), so the differences between cells are noise of about ±30 %. The rule is on a
plateau; do not re-tune it. No change.
