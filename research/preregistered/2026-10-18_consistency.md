# Pre-registration — a CONSISTENT daily book: scale out, fewer outlier days (written 2026-10-18, BEFORE any run)

Founder: "cascade is not reliable. I cant have 1 winning day in 10 days of 100 points lets say". Measured first (description): the
every-day book (2R / 0.35 ATR, 1 a day) 2019–26 made +19,749 pts at the 2 pt cost, and its best 20 days (1 % of 1,786 sessions)
made HALF of it. The goal here is the shape: most short stretches positive, not a few big days.

Variants of the book (entries unchanged: cascade, 10:00 gap / momentum on a straight path, gap retest; 0.35 ATR stop; 1 trade a day):
- scale-out `partial_r` p ∈ {0 (none), 0.25, 0.5, 1} R: half exits at p × R, the other half's stop moves to entry, runner to 2R
  (edge_lab managed exit, unchanged code);
- weakness long w ∈ {in (no stop, as now), out}.
8 variants. Measured on 2010-07 → 2026-10 per variant (via `--trades`, 2 pt cost per round trip): pts/session 2010–18 / 2019–24 /
2025–26; green days; longest run of losing trade days; **the share of rolling 10-session windows with net > 0** (the consistency
measure); the share of total profit from the best 1 % of days; max drawdown.
Selection (fixed now): the variant with the highest 10-session-window share over 2019–24, provided pts/session > 0 in 2010–18 AND
2019–24 and the best-1 %-days share ≤ 35 %. Reported against the current book (p 0, w in). No winner = say so.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--trades`, 2 pt cost; sessions 2010-07 → 2026-10; green / red-run / 10-day windows / top-1 % /
drawdown over 2019–24; max DD 2019–26).
| variant | pts/session 2010–18 / 2019–24 / 2025–26 | green days | longest red run | 10-session windows > 0 | best 1 % of days' share | max DD |
|---|---|---|---|---|---|---|
| current (p 0, w in) | +0.39 / +8.86 / +21.34 | 41 % | 13 | 60 % | 43 % | 1,279 |
| p 0.25, w in | −0.36 / +4.89 / +16.76 | 66 % | 4 | 63 % | 77 % | 1,197 |
| p 0.5, w in | −0.07 / +7.01 / +17.27 | 58 % | 4 | 62 % | 53 % | 1,281 |
| p 1, w in | +0.13 / +6.54 / +18.27 | 46 % | 6 | 59 % | 57 % | 1,455 |
| p 0, w out | −0.22 / +6.91 / +4.26 | 39 % | 13 | 58 % | 39 % | 1,731 |
| p 0.25, w out | −0.97 / +1.57 / +0.33 | 67 % | 3 | 58 % | 88 % | 1,481 |
| p 0.5, w out | −0.76 / +3.71 / +0.04 | 58 % | 4 | 60 % | 44 % | 1,604 |
| p 1, w out | −0.46 / +3.61 / +1.99 | 44 % | 5 | 56 % | 57 % | 1,655 |
**No variant qualifies** (every one has the best 1 % of days carrying > 35 % of the profit). Scaling out changes how the days FEEL
— up to 66 % green days, losing runs ≤ 4 — but not the shape of the money: the share of positive 10-session windows stays 56–63 %
everywhere, the partial half earns little, and the profit concentrates even more in the few big days. These are trend-day edges:
their money comes from the days the market runs, whichever way they are cut.
