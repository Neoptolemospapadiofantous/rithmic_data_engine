# Pre-registration — the book's worst days vs the size forecast; ES accounts beside NQ (written 2026-10-19, BEFORE any run)

1. TAILS. Book day P&L in R (rebuilt cascade + gap retest + 10:00 gap rule, 1 pt cost, R = 0.35 ATR) by predicted-range fifth
   (research/programs/out/intraday_range_pred.csv, walk-forward). Measured per fifth and era (2014–18 / 2019–24): mean day R, share of
   days ≤ −2R (two stops, the $5k daily-limit zone at $75 risk), worst day. SKIP rule: a fifth whose ≤ −2R share is ≥ 1.5× the
   average AND whose mean day R is below the average in BOTH eras → test skipping it on the Vest operation (funded $50 / claim $500
   keep $250, eval $120; 2019–25 and 2010–18 windows, 2 and 6 pt scaled cost); keep only if mean net per year rises in all four.
2. ES. The same book on ES (cascade 0.25/0.30, gap retest, 10:00 gap rule; 0.35 ATR stop, 2R), ES ATR = 14-session mean RTH range,
   cost scaled to the same cost/ATR ratio as NQ. (a) ES operation alone (10 slots); (b) 5 NQ + 5 ES slots vs 10 NQ: mean net per year,
   losing windows, worst window, in both eras. ES is ADDED only if (b) has fewer or equal losing windows and a worst window no worse in
   both eras at 2 pts, with mean net ≥ 90 % of 10 NQ. Same-direction-per-instrument; NQ and ES accounts may hold opposite directions on a
   day — CHECK Vest's hedging rule wording (opposite sides of the SAME market) before use.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19.
1. Tails (day R by predicted-range fifth, quietest → wildest): 2014–18 mean −0.09 / +0.24 / +0.30 / +0.03 / +0.21 (all +0.15), ≤ −2R days
   9.7 / 7.1 / 14.5 / 17.9 / **22.3 %** (all 14.8 %); 2019–24 +0.42 / +0.28 / +0.19 / +0.41 / +0.33 (all +0.32), ≤ −2R 6.9 / 9.0 / 11.2 / 16.7 /
   **21.4 %** (all 13.4 %). The predicted-wildest fifth has 1.5–1.6× the two-stop days but earns at or above the average → **skip rule not
   met.** (Description: more setups fire on wide days, so wide days carry both more −2R days and more profit.)
2. ES. First run used a hand-made ES ATR (14 RTH ranges incl. the same day) that did not match edge_lab's stop ATR — invalid, discarded.
   Re-run with edge_lab's own ATR exported per day (also re-checked NQ: atr.csv is edge_lab's ATR lagged one day, mean ratio 0.981;
   the NQ operation re-run on the exact ATR gives +$15,566 vs +$15,530 — unchanged).
   ES book per trade (ES pts, 0.35 cost): cascade +0.02 / +0.94 / +4.67, gap retest −0.16 / +2.94 / +0.78, 10:00 gap rule +3.56 (all).
   Operation (mean net/yr, losing windows, worst): 2 pts 2019–25 — 10 NQ +$15,566 (5/82, −$3,600) · 10 ES +$7,440 (11/82) · **5+5
   +$11,503 (4/82, −$1,400)**; 2010–18 — 10 NQ +$10,788 (5/89, −$4,000) · 10 ES +$1,931 (32/89) · 5+5 +$6,359 (6/89, −$2,769).
   6 pts: 5+5 +$7,943 (7/82) vs NQ +$11,289 (15/82); 2010–18 +$3,555 (15/89) vs +$7,480 (8/89).
   → **ES NOT added**: 5+5 has more losing windows than 10 NQ in 2010–18 (6 vs 5) and earns 59–74 % of it. ES halves the worst window
   in 2019–25 — a smoother but smaller operation, the founder's call if smoothness is worth ~$4k a year.
