# Flag book — nine tests (pre-registered 2026-10-22, before any run)

Founder: "what else can we test" → "test all" / "and doc". Doc: https://claude.ai/artifact/Hh3HWKm1aex8R1XhJZjcRG

## Baseline (frozen, out/flag_prop_ev_prob.txt)
BOOK2 (4-5m, pole >= 3×, pullback 5-38 %, grade A/B, 2 open per market, $150 daily stop, 2 micros each market), NQ + ES wide15
exports 2014-01 → 2026-10-16: 6,291 trades, +0.220R a trade, months > 0 78.2 %, account EV select_daily_50k median $4,593,
apex_eod_50k $2,772. RULE: 938 trades, +0.394R.

## How variants are judged
Each variant (except T1, T8, T9) is a modified copy of the two wide15 exports, written by `book_tests.cpp` and run through the
unchanged `flag_prop_ev … prob` (the same booking, daily stop, bootstrap and account EV as the baseline). A variant only removes
trades (T2, T3, T4, T7), regrades them (T6) or re-simulates the entry (T5) — nothing else changes.

PASS (BOOK2) = all of: avg R a trade >= baseline + 0.03R; better in BOTH halves 2014-19 and 2020-26; better on BOTH NQ and ES
separately (for filters that apply to one market's trades); median EV select_daily_50k AND apex_eod_50k not lower than baseline.
T3 (one trade per move) passes on lower 12-month max drawdown (bootstrap median) AND EV not lower — avg R may stay flat.
About 15 variants → one false pass is expected by chance; a pass is a paper candidate, not a change to the live book.

## The tests
- **T1 execution on ticks** (after 16:05 ET only — Postgres ticks): every BOOK2 / RULE flag-mid fill since the collector's own
  data (2026-09-18 → today), NQ + ES. Measure (a) touch vs trade-through: did the market trade at least 1 tick through the
  limit (a sure fill) or only touch it; (b) stop slippage: first trade at or beyond the stop vs the stop price; (c) the 1m
  simulation's outcome vs the tick replay's. Judged descriptively; the measured trade-through miss rate and slippage are then
  applied to the full history (the `thru` export already tests trade-through on history).
- **T2 shape at the fill**: the flag's shape from the closed flag bars BEFORE the fill minute (flag_behavior Part A rules).
  T2a skip if deep or down channel · T2b skip if down channel · T2c (info) tight box only. Fills with no closed flag bar keep.
- **T3 one trade per move**: T3a no two open trades on the same market and side · T3b no two open same-side trades across NQ
  and ES · T3c both.
- **T4 NQ–ES agreement**: T4a keep a flag only if the OTHER market is on the same side of its session open and VWAP at the
  registration minute · T4b keep only if the other market registered a same-side wide flag (4-5m) within 15 min before.
- **T5 limit offset**: re-simulated entry with the limit k ticks from the flag middle toward the pole top (k = +1, +2: fills
  more) and away (k = −1, −2: fills less, better price). k = 0 must reproduce the export (check).
- **T6 grade refit**: walk-forward weights refit monthly instead of yearly (same features, same target, same thirds). Also
  reported: share of flags whose grade differs between yearly and monthly fits. k = yearly must reproduce the export grade.
- **T7 time of day**: BOOK2 by registration window (09:45-11:00 / 11:00-12:00 / 12:00-14:00). T7a drop 12:00-14:00 · T7b drop
  11:00-14:00.
- **T8 prop rule sets**: BOOK2 trades through every plan in payout_sim.hpp (Tradeify Select / Growth, MFF, Apex, Topstep,
  Lucid) at 2-5 micros — descriptive EV per eval; rules as coded there (not re-verified today).
- **T9 more markets**: data check only — YM / RTY / GC / CL exist in bars_1m only from 2026-09-28 (18 sessions); the owned
  Databento history is NQ + ES only. No edge test is possible without buying history.

## Results (2026-10-22; out/book_tests.txt, out/flag_prop_ev_firms.txt, out/book_exec.txt)
Checks: T5 k = 0 re-simulation = the export on 47,858 of 47,858 trades; T6 yearly refit = the export grade on 111,067 of 111,068.
**No variant passes.** BOOK2 base 2.44 trades/day, +0.220R, EV $4,593 / $2,772 (Select Daily 50K / Apex EOD 50K):
- T2a/T2b skip down channel at the fill (no flag is deep at a mid fill — identical): 1.86/day, +0.230R, NQ worse, ES better,
  12-month DD median $2,036 vs $2,470, EV $4,154 / $2,623 → FAIL. T2c tight box only: +0.286R but 0.72/day, EV $443 → FAIL.
- T3a no two same market+side: 1.47/day, +0.207R, EV $1,147 / $957, DD unchanged → FAIL. T3b (= T3c) across markets: EV $205 → FAIL.
- T4a other market same side of open+VWAP (93 % kept): +0.208R, EV $3,733 → FAIL. T4b other market flagged within 15 min (57 %):
  +0.245R, NQ 2020-26 worse, EV $1,873 → FAIL.
- T5 offset ±1/±2 ticks: +0.213 … +0.234R, EV within ±$210 (bootstrap noise) → no change; keep the exact middle.
- T6 monthly refit: +0.219R, EV $4,542; grades differ on 4.8 % of flags, 50 of 6,320 BOOK2 trades leave → FAIL (no gain), grade stable.
- T7a stop at 12:00: +0.252R but ES 2014-19 worse and EV $3,785 → FAIL. T7b stop at 11:00: +0.305R, better in all four cells, EV
  $2,314 → FAIL on EV only (the smoother, slower book). Registration window 09:45-11:00 carries the edge (NQ +0.173 / +0.377R).
- General finding: EV per eval follows trades a day; every filter that removes trades loses EV even at higher R.
- T8 at 2 micros / $150: MFF Rapid 50K $5,572, Topstep 50K $5,184, Select Daily 50K $4,577, Lucid Flex 50K $3,479, Select Flex 50K
  $3,428, Apex EOD $2,681 (rules as coded 2026-10-11/12; MFF cap and Apex share assumed; best size in-sample).
- T9 not testable: YM/RTY/GC/CL in bars_1m only since 2026-09-28; owned history NQ + ES only.
- T1 (run at the founder's request DURING RTH, in a lighter form than planned: ticks only for each fill's entry window and its 1m exit
  minute ± 1 min — 4.3 M ticks in 25 s, 0 collector drops; exits keep the 1m outcome, ticks measure slippage and stop/target order):
  91 4-5m flag-mid fills over 22 sessions 2026-09-22 → 10-21 (51 taken by BOOK2/RULE). Traded >= 1 tick through the limit 90/91
  (99 %; the one touch-only fill was a loser), 51/51 for the book. Stop slippage median 0.11 / mean 0.16 / worst 0.56 pts. Stop and
  target in the same minute: 0. Avg R after costs: 1m +0.183 → with slippage, trade-through only +0.189 (book +0.248 → +0.242).
  → PASS: the 1m simulation and the cost assumption (1.5 NQ / 0.5 ES pts) hold on ticks. The book's +0.248R over 51 fills rests on
  five winners on 10-14 / 10-16 and says nothing about the edge.
