# Pre-registration — combining many strategies into one side-by-side book (written 2026-10-18, BEFORE any run)

Founder: "more strategies we can add" / "we need many strategies that we can combine". Base = F (cascade + gap retest + 10:00 gap
rule side by side; 2026-10-18_multi_trade_day). Candidates (NQ, intraday, each its own position, ≤ 1 trade a day each, 2 pt cost;
`--trades` = edge_lab's best variant of each spec): monday_long, monday_long_1100, strong_open, late_so, bos_ema_gap, rdir_m1,
weak_day_long, weak_day_gapup, weak_day_gapdown, cascade_asia_2h, asia_open_drive, and from 2026-10-18_side_by_side_adds M (mom10)
and W with a 0.5 ATR stop (wstop_0.5).
Step 1 eligibility: alone net/trade > 0 in BOTH 2010–18 and 2019–24, and ≥ 100 trades 2010–24.
Step 2 greedy (fixed now): from F, among eligible candidates whose addition raises pts/session in BOTH 2010–18 and 2019–24 AND keeps
2019–24 pts per point of max drawdown ≥ the current book's, add the one with the highest 2010–18 pts/session; repeat until none.
Reported: each candidate's daily-P&L correlation with F (2010–26, all sessions), the final book in 2010–18 / 2019–24 / 2025–26
(2025–26 one look), worst day, max DD, share of sessions ≥ +20 pts, Vest $5K eval at $50/trade.
Caveat stated now: the variant inside each spec was picked by edge_lab on the full screen (not only 2010–18) — a selection
leak for the multi-variant specs; the greedy rule's 2010–18 requirement only partly guards it.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick --trades`; 2 pt cost). Step 1, net/trade 2010–18 / 2019–24 / 2025–26 (n), daily corr vs F:
asia_open_drive +0.30 / +0.14 / +6.26 (197/193/60), corr +0.00 · weak_day_gapdown +23.66 / +32.33 / +77.54 (55/86/12), −0.03 ·
weak_day_gapup +1.08 / +19.57 / +35.27, +0.16 · weak_day_long +6.95 / +18.96 / +110.99, +0.06 · wstop_0.5 +1.70 / +14.96 / +115.08,
+0.11 → ELIGIBLE. Not eligible (2010–18 ≤ 0 or too few trades): bos_ema_gap −1.00, cascade_asia_2h −2.77, late_so −0.25, mom10 −0.32,
monday_long −2.37, monday_long_1100 −2.49, strong_open −0.76, rdir_m1 (0 trades before 2019). The NY-morning momentum ideas
(strong_open, late_so, bos_ema_gap, mom10) correlate +0.45 … +0.54 with F — they are the same days as F, not new ones.
Step 2 greedy: F +0.15 / +15.05 / +13.86, pts/DD 0.0121 → + weak_day_gapdown +0.78 / +16.94 / +16.82, 0.0132 → + asia_open_drive
+0.81 / +16.96 / +18.02, 0.0140 → stop (weak_day_gapup, weak_day_long, wstop_0.5 each lower pts/DD — they overlap the gap-down long).
**Final book: F + weakness long after a gap down (09:30 buy, no stop, flat 15:55) + Asia open drive (20:20 ET, flat 02:00).**
1.45 trades/session, sessions ≥ +20 pts 36 %, worst day −557, max DD 2019–24 1,215. Vest $50: 37.6 % pass, 17 days — the gap-down
long has NO stop (worst open loss −606 pts) and breaks the $150 daily limit; Vest-legal variant F + Asia only: see below.
F + Asia only (description): +0.18 / +15.07 / +15.05, max DD 1,104, worst day −418, Vest $50 57.0 %, 24 days (F alone 62.9 %).
**Reading:** of 13 candidates only two add to F, and only on top of it: the gap-down weakness long (+1.9 pts/session 2019–24, a
different kind of day — corr −0.03) and the Asia open drive (≈ +0 points, it only smooths the drawdown). The other morning
momentum ideas fire on F's own days. On a prop account with a daily loss limit the gap-down long needs a stop it was never tested
with (the 0.5 ATR stop on the wider weakness long already costs points, 2026-10-18_side_by_side_adds) — so the combined book is a
cash/futures-account book; on Vest, F stays the book.
