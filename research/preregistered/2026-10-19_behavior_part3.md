# Pre-registration — NQ behaviour part 3 + order flow (written 2026-10-19, BEFORE any run)

Founder: "do all of them. also test for behaviours on orderflow data whatever we have." Program `research/programs/behavior3.cpp`
(NQ 1m 2010-07 → 2026-10, RTH, roll windows excluded, ATR units) + `research/programs/awk_20261019/streaks.awk` (book trades).
Descriptive unless a rule is stated. Eras 2010–18 / 2019–22 / 2023–26; "robust" = same sign in all three and |t| ≥ 2.

1. **Streaks** (book F daily P&L, 2 pt cost): month-to-month and week-to-week autocorrelation; P(next month > 0 | this month > 0) vs
   (| ≤ 0). FILTER rule: trade the book only when its own last-20-trading-day (or last-60) P&L is > 0. Adopt only if pts/session
   AND the Vest operation (eval $120 / funded $50 / claim $500 keep $250, 2 pts) improve in BOTH 2010–18 and 2019–25.
2. **Pullback depth** on the cascade's own trades: MAE in R (R = 0.35 ATR) of winners (net > 0) and of 2R hits, by era; share of 2R
   winners whose MAE went past 0.5R / 0.75R. And on trend days (|C−O| ≥ 0.5 range, direction = the close), the deepest pullback
   after 10:00 against the day's direction, in ATR, by hour it happened.
3. **Gap fills**: gap = 09:30 open − prior RTH close, in ATR; filled = RTH touches the prior close; by gap size (0.1–0.25, 0.25–0.5,
   0.5–1, > 1), up vs down, VIX < 20 vs ≥ 20: fill rate, median minutes to fill, fill by 10:00 / 11:00.
4. **Sweeps of yesterday's high / low**: RTH trades through PDH (PDL); within 30 min back below (above) it = sweep, else hold; then the
   close: beyond the level vs back inside. Sweep reversal rate vs hold continuation rate.
5. **Round numbers**: first RTH touch of a 100-point level (x00) from below / above; within 30 min price either pushes ≥ 0.15 ATR through
   or rejects ≥ 0.15 ATR back. Control: the same for x50 levels and x25/x75. Round numbers "matter" only if x00 differs from the
   controls by ≥ 3 points of rate in all three eras.
6. **Afternoon**: on days the 12:00 state is straight (≥ 0.55) and ≥ 0.4 ATR from the open, 12:00 → 14:00, 14:00 → 15:55 moves in the
   morning's direction; share of afternoons that reverse ≥ half the morning move.
7. **Early volume**: RVOL = 09:30–09:45 volume ÷ mean of the prior 20 days' same window; trend-day rate by RVOL fifth, and inside the
   10:30 "straight + moved" group.
8. **Order flow (2023-03 → 2026, aggressor buy/sell volume per minute; book imbalance per minute)**:
   a. delta (buy − sell aggressor volume) 09:30–10:00 vs the 10:00 → 15:55 move: correlation, and direction hit rate;
   b. delta agreeing vs disagreeing with the 10:00 price move → the rest of the day in the move's direction;
   c. divergence: price at a new RTH high (low) at 10:30 but cumulative delta below (above) its 10:00 value → rest of the day;
   d. absorption at PDH/PDL: on the first touch, aggressor volume in the touch minute ≥ 2× the 20-minute mean pushing INTO the level
      and price not through by ≥ 0.1 ATR within 15 min → reversal rate vs other touches;
   e. resting-book imbalance 09:25–09:30 (bid − ask within 10 bp) vs the first 30 min and the day;
   f. the book's cascade trades split by 09:50 delta agreeing with the trade vs not (net R/trade).
   Order-flow findings are 2023–26 only (≈ 3.5 years): anything that looks real is a hypothesis for the paper fleet, never a rule.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (`research/programs/out/behavior3.txt`; streaks via `awk_20261019/streaks.awk`).
1. Streaks: monthly autocorrelation −0.235 (2010–18) / −0.027 (2019–26); P(next month > 0 | this > 0) 40.0 / 62.1 % vs (| this ≤ 0) 58.8 /
   73.3 % — no momentum, if anything losing months are followed by better ones. Equity-curve filter: last 20 days > 0 → pts/session
   +0.15 / +15.05 / +13.86 becomes −0.95 / +10.31 / +8.59; last 60 → −0.31 / +12.55 / +9.32. **FAIL — never switch the book off on a
   losing streak.**
2. Pullback depth: cascade winners went 0.36R against on average (2R winners 0.29R); 29 % of winners went > 0.5R and 12 % > 0.75R
   against first; losers 0.95R. Trend days: the deepest pullback after 10:00 averages 0.49 ATR (all eras 0.488–0.492), ending at any
   hour (09:30–10:30 17 %, 10:30–11:30 20 %, then 11–14 % per hour).
   Follow-up stop width (book F + weakness long, sized to the stop): net R/trade 0.35 ATR −0.018 / +0.171 / +0.071, 0.5 −0.034 / +0.106 /
   +0.032, 0.7 −0.025 / +0.064 / +0.017; Vest operation 2 pts 2019–25 $15,449 → $7,344 → $3,519, 2010–18 $10,234 → $4,766 → $2,495
   (losing windows 8 → 15 → 18, 7 → 17 → 32). **Wider stops FAIL** — the win rate rises 3–6 points but the farther 2R target and the
   lower size cost more. The 0.35 ATR stop stays.
3. Gap fills: small gaps fill — 0.10–0.25 ATR up 74–76 %, down 75–86 % (median 8–13 min, ~half by 10:00); 0.25–0.5 ATR 54–62 % (median
   ~40 min); 0.5–1 ATR 31–39 % (median 85–141 min, ≤ 4 % by 10:00); > 1 ATR 12–24 %. Down gaps with VIX ≥ 20 fill most (0.1–0.25: 86 %).
   Stable across eras except thin cells. The gap retest (gap ≥ 0.1 ATR, trade WITH the gap) therefore runs against a ~75 % fill on the
   smallest gaps — a candidate filter (gap ≥ 0.25 ATR), not tested as a trade here.
4. Yesterday's high / low: taken from inside, ~83 % come back through within 30 min (a "sweep"); after a sweep the day still closes
   beyond the level 52 % (high) / 43 % (low); after a 30-min hold it closes beyond 80 % / 68 % (all eras). A first break is a coin flip;
   a break that holds 30 min usually stays.
5. Round numbers: first touch of x00 broke through 50.8 % vs x50 48.5 % and x25 46.5 % (per era x00 − x50 = 1.2 / 2.9 / 2.4 pts) →
   below the 3-point bar in all eras: **round numbers do not matter** beyond a hair.
6. Afternoon after a trend morning (12:00 straight ≥ 0.55, ≥ 0.4 ATR): 12:00 → 14:00 −0.000 ATR, 14:00 → 15:55 +0.013 (t 1.4); gives back
   ≥ half the morning only 16.6 % → afternoons drift flat; holding to 15:55 neither gains nor loses on average.
7. Early volume (RVOL 09:30–09:45): trend-day rate 47–50 % in every fifth; inside "straight + moved at 10:30" 59–62 % in every fifth →
   **RVOL adds nothing** beyond straightness.
8. Order flow 2023-03 → 2026 (one era, ~645 days — hypotheses only):
   a. delta 09:30–10:00 vs 10:00 → 15:55: corr +0.032, same direction 53.5 %.
   b. 10:00 move ≥ 0.1 ATR with delta agreeing: rest of day +0.064 ATR (t 2.1, n 402) vs disagreeing −0.073 (n 71).
   c. 10:30 new high/low with delta divergence: reversal +0.020 (n 43) vs without −0.055 (n 267) — nothing.
   d. absorption at PDH/PDL (heavy aggression into the level, not through): reversed first 66.7 % (n 15!) vs 36.2 % others — too few.
   e. book imbalance 09:25–09:30: corr +0.029 with the day; sign right 50.4 % (first 30 min) / 53.4 % (day) — nothing.
   f. cascade trades with delta since 09:30 agreeing: +0.167 R (n 458) vs disagreeing +0.036 R (n 55, t 0.2).
   → order flow mostly AGREES with price (delta disagrees on only ~1 cascade trade in 9), so it adds little information; the one
   hypothesis worth a forward look is "skip a cascade trade when delta disagrees" (b / f) — 55 trades is far too few to act on.

## Follow-up, pre-registered 2026-10-19 BEFORE running (prompted by 2b: trend-day pullbacks after 10:00 average 0.49 ATR > the 0.35 ATR stop)
Book F (cascade + gap retest + 10:00 gap rule) with stop k ∈ {0.35 (base), 0.5, 0.7} ATR, target 2R, flat 15:55, each trade sized to
its own stop (same $ risk). Measured: net R/trade by era, and the Vest operation (eval = first trade $120, funded $50, claim $500 keep
$250; 2 and 6 pts scaled; 2019–25 and 2010–18). A wider stop is adopted only if the operation's mean net per year rises in all four
cells AND losing windows do not rise in any. (Earlier evidence: the mext stop-width twin on the cascade, 2026-10-17.)
