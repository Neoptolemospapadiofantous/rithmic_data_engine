# Pre-registration — fade the London first hour (written 2026-10-19, BEFORE any run)

Lead: the session table (2026-10-19, scratchpad sess/) — a strong London start (03:00 → 04:00 ET) continued to 09:30 only 42 % of the
time in 2019–26 (random 50 %), reverted through the London open 40 % (random 32 %); 2010–18 was at random (51 / 33 %).
Caution: the lead was found in 2019–26, so the walk-forward to 2024 partly re-uses it; the clean look is the holdout 2025-01 → today.
Rule: at 04:00 ET, if |px − price at 03:00| > k × ATR, trade AGAINST it: dir = −sign(px − at(3,0)). Stop stopk × ATR, target rr R,
flat 09:25, one trade a night. Grid k ∈ {0.1, 0.15, 0.2}, stopk ∈ {0.15, 0.25}, rr ∈ {1, 2} → 12 variants. NQ, ES confirm, 2010-07 →
2024-12, all gates (controls 99 %). Then the walk-forward pick once on 2025-01 → today (≥ 30 trades, net > 0) and, if it passes,
as an add to book F (daily-P&L correlation, pts/session, DD) and the Vest $5k sim.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (build-exp/edge_lab). **FAIL at CONTROLS** — walk-forward 665 trades, +0.76 pts/trade net, PF 1.05, t 0.46; beats
random direction in 79 % of seeds (need 99 %), random time 41 %, random day 91 %. Robustness 3/7: 2010–18 −0.36, 2019–22 −4.29,
2023–26 +7.09 (2024 alone +1,118 pts), ES −276 pts, 2 of 12 variants positive. 5/14 years positive.
Holdout 2025-01 → today (description only, the idea already failed): k 0.1 / stop 0.25 / 2R +2.34 on 113 trades; k 0.2 −0.67 on 39.
**Reading:** the 2019–26 "London reverts" count is mostly 2023–24; as a trade with a stop the fade is a coin flip. Closed.
