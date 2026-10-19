# Flags, optimised another way: day context + walk-forward settings (written 2026-10-19, BEFORE any run)

Founder: "we need another way to optimise this" (after the 3-year settings sweep failed its 2010–2023 check).
Program `research/programs/flag_chart/flag_wfo.js` (Node, reads data/edge_lab/cache/NQ.csv, 2010-07 → 2026-10-16). NY 09:30–16:00.
Flags as on the flag-chart page. Grid: 5m — pole 4 / 6 / 8 bars × ≥ 0.1 / 0.2 / 0.3 ATR × flag 3 / 5 bars × ≤ 0.4 / 0.6 × pole × target
1 / 1.5 / 2R (108 settings); 1m — pole 10 / 15 / 20 × ≥ 0.2 / 0.3 ATR × flag 3 / 5 × ≤ 0.4 / 0.6 × 1 / 1.5 / 2R (72). Net = minus 1 pt;
results in R (net ÷ risk to the stop) so eras compare, and in points.
A. CONTEXT at the breakout bar: WITH the day = price on the flag's side of the 09:30 open AND of session VWAP; STRAIGHT = |price − open| /
   (high − low so far) ≥ 0.55. Groups: with + straight, with only, against. Measured over every setting; a context "works" if with+straight
   beats against in ≥ 90 % of settings AND its median net R > 0 in all three eras (2010–18 / 2019–22 / 2023–26).
B. WALK-FORWARD: each month, among the settings (× context group with+straight or all), pick the one with the best net R per trade over the
   previous 24 months (≥ 30 trades); trade it in that month only. Out-of-sample 2012-07 → 2026-10. Pass = net R > 0 in all three eras and t ≥ 2.
   Anything that passes A or B then goes through edge_lab gates (random direction / time / day controls) before it counts.

## Results (2026-10-19, research/programs/out/flag_context_wfo.txt; 368,859 trade rows over 180 settings)
A. Context, median net R over settings by era (2010–18 / 2019–22 / 2023–26):
   5m with+straight +0.052 / −0.018 / +0.178, with only −0.204 / −0.043 / −0.091, against −0.250 / +0.090 / −0.079; with+straight beats
   against in 81 / 93 settings (87 %). 1m: every group negative or ≈ 0; with+straight beats against in only 39 %. → **A not met** (87 % < 90 %,
   2019–22 median < 0), but on 5m the day's direction is clearly what separates the good flags from the bad.
B. Walk-forward (best setting over the prior 24 months, traded the next month, OOS 2012-07 → 2026-10):
   5m all +0.056 R (t 0.82); **5m with+straight +0.137 R (t 1.99), +3.66 pts/trade, 323 trades, eras +0.174 / +0.013 / +0.334** — positive in
   every era, t one hundredth short of 2; 1m all −0.012, 1m with+straight −0.044. → **B not met by the letter (t 1.99); the 5m with-the-day
   flag is the near miss.**

## Follow-up, pre-registered 2026-10-19 BEFORE running
The walk-forward's most-picked 5m with+straight setting as ONE fixed rule through edge_lab (so it meets the random direction / time / day
controls): `flag5_trend` — NY 09:45–15:00, 5-min scan: pole = close 6 bars before the flag (bars k−12 → k−6) moved ≥ m × ATR, flag = the 5
bars before the breakout bar with range ≤ 0.6 × pole, breakout = the 5-min close beyond the flag; only WITH the day (price on that side of
the 09:30 open and of VWAP) on a straight path (≥ 0.55); stop one tick beyond the flag; target rr R; m ∈ {0.1, 0.2, 0.3}, rr ∈ {1.5, 2} → 6
variants; max 2 a day. 2010-07 → 2024-12, all gates; holdout once if it passes.
Result of the follow-up (2026-10-19, edge_lab, loaded to the DB): **`flag5_trend` PASS every gate** — walk-forward 475 trades, +7.48 pts/trade
net, PF 1.53, t 3.30, 28.8 / yr, win 50 %; controls 100 / 100 / 100 %; robustness 7 / 7 (eras +2.08 / +7.43 / +18.80, cost × 1.5, one bar
late, ES +40 pts, all 6 variants positive); prop break-even fee $2,786. **Holdout 2025-01 → 2026-10 (one look, rr 1.5): m 0.1 +6.84 on 55
(PF 1.21), m 0.2 +9.76 on 41 (PF 1.27), m 0.3 +14.81 on 28 → PASS** (not perfectly clean: flags were looked at on 2023-10 → 2026 earlier today,
though never with the with-the-day filter).
As an add to book F (m 0.2, rr 1.5, full history): pts/session +0.15 / +15.05 / +13.86 → **+0.42 / +17.11 / +15.00**, max DD 2019–24 1,247 →
1,290, pts/DD 0.0121 → **0.0133**, daily corr with F 0.25 → **ADOPTED as a book add by the 2026-10-18 rule.**
On the Vest $5k operation it does NOT help: plan + flags at $50 $13,457 (10) / $8,598 (5) / $10,288 (17) / $5,888 (14), at $25 $15,736 (6) /
$10,349 (4) / $11,528 (16) / $7,053 (9) vs the plan $16,139 (5) / $11,201 (5) / $12,542 (7) / $7,718 (7) — its stop (median 0.2 ATR) is tighter
than the book's, so its days breach the $150 daily limit more often. → book / larger accounts and the paper fleet, not the $5k Vest plan.
