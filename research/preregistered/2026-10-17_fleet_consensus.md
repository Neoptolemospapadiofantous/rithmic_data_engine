# Fleet consensus — does the fleet as a crowd carry an edge its members don't? (2026-10-17, PRE-REGISTERED)

Founder: "find a way to trade any of the strategies or trades in the fleet on a daily … some filter or feature that would
help us trade 1-3 trades a day on sessions that would give us an advantage … from the fleet, not random macro strategies."

Already known (do not re-test): every fleet family is negative after cost (family sweep 2026-10-16); the information in a
fleet entry is "which way the day has moved" (2026-10-17_fleet_entries_exits.md); tape/flow filters, regime labels, EMA
rules, re-entry and meta take/skip are measured. NOT yet tested: the fleet as an ENSEMBLE — many unrelated strategies
entering the same side in the same few minutes.

## Data
`hist_trades`, symbol MNQ, labels nq_hist + nq_bars (nq_barcheck excluded), 2021-01 → 2026-10-09 (2023 partial).
Signal unit = strategy ROOT (`split_part(strategy_id,'__',1)`), so 50 overlay copies of one base count once.
Prices: NQ `bars_1m`. Cost 1.6 pts round trip (measured 1.3 + margin); 2.4 shown as a stress.

## Signal
For each minute m: L = distinct roots entering long in [m−5, m), S = short. net = L − S, n = L + S.
Trigger when n ≥ N and |net|/n ≥ A (agreement). Direction FOLLOW = sign(net); FADE = −sign(net). Entry = open of minute m.
Grid: N ∈ {top 5 %, 2 %, 1 %, 0.5 % of that session's minutes by n, thresholds fixed per session on 2021-2022 only},
A ∈ {0.6, 0.8}. ≤ 3 trades/day, one position at a time.
Sessions: RTH 09:35-15:00 ET, London 03:00-09:25, Asia 18:00-02:30.
Exits (each a separate cell): (a) stop 0.25 ATR14 / 2R / flat 15:55 (session end overnight); (b) hold 30 min; (c) hold 60 min;
(d) hold to session end.
Cells: 4 N × 2 A × 2 dir × 4 exits × 3 sessions = 192.

## Controls (a cell must beat ALL)
1. **Open-direction twin** — same minute, direction = sign(price − session open). This is the bar the fleet failed on 10-17.
2. **Random-minute twin** — same day, uniform random minute in the same window, direction = consensus sign computed at THAT
   minute (n ≥ 1; else skip), 200 draws, cell must beat the 99th percentile.
3. **Random-direction** — same minute, coin flip, 200 draws, 99th percentile.

## Pass
Multiple-testing: 192 cells → per-cell bar t ≥ 3.3 (≈ Bonferroni 0.1 %). Plus: positive in each era (2021-22, 2023-24,
2025-26), walk-forward (choose N/A/exit on all prior years, trade the next) > 0, ≥ 100 trades, beats controls 1-3.
A pass becomes a paper-fleet candidate only (forward from the next session). A fail is recorded and not re-gridded.

## Results
(filled in below after the run — nothing above this line is edited after the run)

### Run 2026-10-17 (scratchpad consensus.py; 1,039 RTH / 1,071 London / 1,075 Asia fleet-covered days, 2021-01 → 2026-10-09)
**FAIL — no cell reaches the bar.** Best of 192 cells: t = 1.85 (bar 3.3; the expected best of 192 null cells is ≈ 2.8), so
the random-minute / random-direction twins were not needed (computed only for t > 2 — none).

| Cell | n | trades/day | net pts/trade | t | 2021-22 / 2023-24 / 2025-26 | open-direction twin |
|---|---|---|---|---|---|---|
| RTH, top 5 %, agree 0.8, follow, 0.25 ATR stop / 2R | 1,575 | 1.5 | +4.70 | 1.85 | +3.85 / +9.64 / +1.85 | +2.92 |
| RTH, top 5 %, agree 0.6, follow, stop / 2R | 1,634 | 1.6 | +3.68 | 1.47 | +1.95 / +9.36 / +1.76 | +3.25 |
| RTH, top 5 %, agree 0.6, follow, hold to 15:55 | 1,013 | 1.0 | +8.51 | 1.46 | +9.29 / +12.29 / +4.02 | +1.37 |

Reading: (1) following the crowd beats fading it everywhere — every RTH FADE cell is negative (the fleet's fades lose, as on
10-17). (2) The best RTH follow cells beat the open-direction twin by 1-2 pts/trade, positive in every era, at 1-1.5
trades/day — but the per-trade noise is ~±2.5 pts, so this is indistinguishable from the best of 192 random cells.
(3) London and Asia: nothing; consensus there is noise. Not a paper candidate; not re-gridded.
Caveat: counts are distinct roots per minute summed over 5 minutes (a root re-entering inside 5 min counts twice — rare,
one position per id).
