# Pre-registration — more than one trade a day from the book's setups (written 2026-10-18, BEFORE any run)

Founder: "must make at least 20-50 points a day" → "more tests". The book takes ONE trade a day (first setup that fires) and
averages +7–9 pts/session at 1 NQ. Question: does taking the later setups too (one position at a time) add points per day?
Inputs: the screen trades of cascade-only, retest-only and the 10:00 gap rule (0.35 ATR, 2R, flat 15:55; `--trades`), merged by
entry time per day; a trade is taken only if it enters after the previous taken trade's exit. Cost 2 pts per trade.
Variants: A first only (baseline) · B up to 2 a day · C up to 3 a day · D 2nd only if the 1st lost · E 2nd only if the 1st won.
Measured: pts/session 2010–18 / 2019–24 / 2025–26, trades/session, % sessions ≥ +20 pts, max drawdown 2019–24 (pts), and the
VestMarkets $5K eval (+$500 goal, $150 daily loss, $300 static DD) with risk/trade sized so two stops ≤ $150 ($75) for B–E vs A at
$125 — pass rate and median trading days to pass, 2019–26 starts.
Adopt a variant only if pts/session > A in BOTH 2010–18 and 2019–24 AND its 2019–24 max drawdown ≤ 1.3 × A's. 2025–26 reported once.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (merged screen trades of cascade-only, retest-only, gap rule; 2 pt cost; pts/session 2010–18 / 2019–24 / 2025–26).
| variant | pts/session | trades/session 2019–24 | sessions ≥ +20 | max DD 2019–24 | Vest eval |
|---|---|---|---|---|---|
| A first only | −0.36 / +6.66 / +3.49 | 0.82 | 34 % | 1,139 | $125: 43.2 %, median 10 days |
| B up to 2 | −0.62 / +7.35 / +7.03 | 0.90 | 36 % | 1,079 | $75: 31.4 %, 18 days |
| C up to 3 | identical to B (a 3rd never fits) | | | | |
| D 2nd only after a loss | −0.69 / +7.35 / +7.03 | 0.90 | 36 % | 1,079 | $75: 31.4 %, 18 days |
| E 2nd only after a win | −0.29 / +6.66 / +3.49 | 0.83 | 34 % | 1,139 | $75: 48.3 %, 24 days |
**Nothing adopted** (B/D lower than A in 2010–18; E adds nothing in 2019–24). A second trade is rare — 0.08 a session — because
the first trade is usually still open at the later setups' times (it holds to its stop, 2R or 15:55), so "one position at a time"
leaves no room. Where it fits it adds +0.7 pts/session (2019–24), and sizing the eval so two stops fit the $150 daily limit makes
passing slower and less likely. More trades from these setups do not lift the daily average toward 20 pts.

## Addendum (written after the run above, BEFORE this run)
F: take every setup that fires (cascade, retest, gap rule — at most one each), overlapping positions allowed (same-direction
overlap = a larger position). Same measures; Vest eval at $50 risk per trade (three stops = $150). Same adoption rule vs A.
Run 2026-10-18: F +0.15 / +15.05 / +13.86 pts/session (vs A −0.36 / +6.66 / +3.49), 1.26 trades/session 2019–24, sessions ≥ +20
pts 35 %, worst day −418, max DD 2019–24 1,247 (≤ 1.3 × 1,139) → **ADOPTED by the rule**: run all three setups side by side.
Points per point of drawdown double (6.66 / 1,139 = 0.0058 → 15.05 / 1,247 = 0.012) because the setups rarely lose on the same
day. Vest eval at $50 per trade: 62.9 % pass, median 24 trading days. Description only (not in the rule): $60 → 34.2 %, 11 days;
$75 → 23.0 %, 8 days — the daily-limit check adds the day's trades' worst points together (conservative: they happen at different
times), so the drop past $50 may be overstated. Caveats: 2010–18 is ≈ 0 (the edge is 2019+, as for every setup in the book); the
"median day" is still ≈ 0 — F raises the average, not the share of +20 days.
