# Overall market context as sizing on the flag book — pre-registered 2026-10-22

Founder: "also what about adding overall context. can we test that on our strategy?" Written BEFORE any run. Program:
`research/programs/regime_size.cpp` mode `context` (same file as the gap test), judged by `flag_prop_ev … mtm`.

Already tested elsewhere and not repeated: 200-day trend (fails), 5m HH/HL and the 09:30-09:35 break (in RULE / the grade), NQ-ES
agreement (fails), volatility rising / prior week / 15m + daily EMA / noise band (fail), the gap (PASSES — the candidate).

Eight context checks, binary, known before the entry minute, mirrored to the flag's side:
- C1 OPEN BEYOND YESTERDAY: the 09:30 open beyond the prior RTH high (bull) / low (bear)
- C2 OVERNIGHT: the 09:30 open in the top third of the overnight range (18:00-09:30), bottom third for bears
- C3 ROOM LEFT: the day's RTH range so far (09:30 → the minute before entry) below 0.6 × ATR14
- C4 VWAP: the price one minute before entry above the session VWAP (below for bears)
- C5 DAY POSITION: that price in the upper half of the day's range so far (lower half for bears)
- C6 TREND 20: the prior close above the 20-session average of RTH closes (below for bears)
- C7 OPEN DRIVE: the move from the 09:30 open to 10:00 (or to the minute before entry, if earlier) with the flag
- C8 WEEK: the prior close above the midpoint of last week's RTH high-low (below for bears)
For each, the favourable value is chosen on 2014-19 BOOK2 trades by mean net R (NQ + ES pooled); 2020-26 shown.

Variants:
- A_Ck: 2 micros when Ck is favourable, else 1 — judged against flat 1 micro (BOOK2 1 micro, 2 open, $500 stop)
- B_Ck: the candidate's gap sizing plus one micro when Ck is favourable (1 + gap + context = 1-3 micros) — judged against the candidate
  (gap sizing alone)
**PASS** (conservative marking): on at least two of topstep_50k / lucid_flex_50k / select_daily_50k, 2014-19 EV >= reference + $100 AND
2020-25 EV >= reference; optimistic marking not below the reference. 16 variants are tested, so a pass by a small margin is weak
evidence; worst drawdown and years better are reported for any pass.

## Results, part 1 (2026-10-22; out/context_sizing.txt, context_sizing_mtm.txt)
C4 (VWAP side) and C5 (upper half of the day's range) are CONSTANT on BOOK2 — the wide15 detection already requires the flag above the
open and VWAP — so they cannot be tested. PASS vs flat 1 micro: A_C2 open in the overnight top third (Topstep $1,483 → $1,690, Lucid
$1,198 → $1,408, Select Daily $1,404 → $1,419) and A_C1 open beyond yesterday's high/low ($1,476 → $1,405, $1,225 → $1,331,
$1,232 → $1,137). FAIL: A_C3 (sized when NOT much room is left — the favourable side), A_C6 (below the 20-session average), A_C7 (opening
drive), A_C8 (last week's midpoint). Every B_ variant (gap sizing + a third micro) FAILS — 3 micros is too big for a $2,000 drawdown.

## Addendum written before its run — higher timeframes (founder: "what about trading 5m flags on the 1h 4h 1d")
Holding 1h / 4h / daily flags for days breaks the prop rules modelled (flat by the close), so this tests 5m-chart BOOK2 flags lined up
with the higher timeframe, sized the same way (A: 2 micros when favourable, judged against flat 1 micro; same PASS rule):
- C9 1h TREND: the last completed 1-hour bar (full session, buckets from 18:00) closed above the EMA20 of 1-hour closes, chained across days
  (below for bears)
- C10 4h TREND: the same on 4-hour buckets (18:00, 22:00, 02:00, 06:00, 10:00, 14:00)
- C11 DAILY POLE: yesterday's RTH session moved with the flag by more than 0.5 × ATR (open → close) — the 5m flag forms inside a daily
  flag's pullback day
Favourable side chosen on 2014-19 as before.
- (addendum 2, written before its run; founder: "possibly 30m to 15m test") C12 15m TREND and C13 30m TREND: as C9 on 15- and 30-minute
  buckets of the whole trading day (EMA20 chained across days), the last bucket completed before the entry minute.

## Results, part 2 (addenda; out/context_sizing.txt, context_sizing_mtm.txt)
Higher timeframes FAIL as sizing (vs flat; Topstep | Lucid | Select Daily, 2014-19 → 2020-25): C9 1h trend $1,169 → $1,213 | $835 →
$991 | $720 → $793 (passes Topstep only; NQ +0.15 vs −0.17R but ES reversed), C10 4h trend $1,146 → $973 | $863 → $862 | $898 → $719
(Topstep only; ES reversed), C11 (yesterday NOT a pole day — flags the day after a big move with them are worse on both markets in both
halves, NQ −0.10 / +0.07 vs +0.16 / +0.30R) $954 → $1,075 | $642 → $901 | $547 → $828, C12 15m trend $890 → $1,095 (99.7 % of BOOK2 flags are
already with the 15m trend — sizing it doubles nearly everything), C13 30m trend (96 % with the flag; the "favourable" minority is too
small to mean anything) $952 → $763. Worst drawdowns rise to $9-13K. Only the opening-location checks pass: gap (regime_sizing.md),
C2 open in the overnight top third, C1 open beyond yesterday. Stacking them (3 micros) fails.
