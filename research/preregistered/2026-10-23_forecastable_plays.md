# Trading what is forecastable — six plays, pre-registered 2026-10-23 02:10 Cyprus, BEFORE the run

From out/forecast_battery.txt: range is forecastable (first-30-min range → day range r² 0.51; ATR5 r² 0.43), volume more so,
one extreme is in by 10:30 on 78 % of days, both ORB-5 sides break on 69 %, small gaps fill 61 % / big gaps 32 %, direction is
a coin. NQ 1-minute bars 2014-01 → 2026-10, one trade a day per play, cost 1.5 pts a round trip, flat 15:55 if neither stop
nor target. Forecast range F = 1.9 × first-30-min range (the median ratio day range / first-30 range, measured 2014-19 only).
Control: post-registration twin — same day, same direction, random minute in the 60 minutes after the entry, same stop and
target distances (5 draws). Edge = trade R − twin R.

- P1 EXHAUSTION FADE: from 11:00, the first minute where range-so-far ≥ 1.2 F AND price is within 0.1 F of the day's high (low):
  short (long), stop 0.25 F beyond the extreme, target the day's midpoint.
- P2 COMPRESSION BREAK: at 10:30, if range-so-far ≤ 0.5 F: buy a break above the first-hour high / sell below its low (first to
  go, limit at the level), stop the other side of the first-hour range, target 1 F from entry, hold to close.
- P3 SMALL-GAP FADE: at 09:30, gap (open − prior close) between 0.1 and 0.5 ATR5: trade toward the prior close, stop = gap size
  beyond the open, target the prior close.
- P4 BIG-GAP GO: at 09:35, |gap| > 0.5 ATR5: trade WITH the gap, stop 0.25 ATR5, target 0.75 ATR5, hold to close.
- P5 FIRST-ORB-BREAK FADE: the first 1-minute close outside the 5-minute opening range (09:35–10:30): fade it back to the
  opposite side of the range; stop 0.5 × ORB range beyond the break.
- P6 LATE CONTINUATION: at 15:00, if price is in the top (bottom) quarter of the day's range: long (short) to 15:55, stop at the
  range midpoint.
PASS = net R > 0 in BOTH halves (2014-19 / 2020-26) AND edge over twin ≥ +0.05R in both halves AND ≥ 200 trades per half AND
cluster-free t ≥ 2.5 on the edge over all trades (one trade a day → trades are the clusters). Holm over 6. Below that = the base
rate was real but does not pay after costs. Program in the session scratchpad (Python, research scratch), output
research/programs/out/forecastable_plays.txt. Nothing changes any config.

## Results (2026-10-23; research/programs/out/forecastable_plays.txt; F = 2.09 × first-30-min range)
**No play passes.** Every one is negative or flat after the 1.5-pt cost in at least one half; the base rates are real, the
trades built on them are not paid.

| play | n | net R | twin R | edge | t | 2014-19 net / edge | 2020-26 net / edge | pts a trade |
|---|---|---|---|---|---|---|---|---|
| P1 exhaustion fade | 827 | −0.097 | −0.132 | +0.035 | 1.0 | −0.277 / −0.039 | +0.060 / +0.099 | −1.3 |
| P2 compression break | 617 | −0.128 | −0.145 | +0.017 | 1.1 | −0.198 / +0.005 | −0.046 / +0.031 | −4.6 |
| P3 small-gap fade | 1,617 | −0.055 | −0.095 | +0.041 | 1.7 | −0.125 / +0.041 | +0.016 / +0.040 | −0.1 |
| P4 big-gap go | 824 | +0.027 | −0.033 | +0.060 | 1.3 | −0.011 / +0.031 | +0.060 / +0.085 | +1.7 |
| P5 first ORB-break fade | 2,971 | −0.109 | −0.137 | +0.027 | 1.1 | −0.207 / +0.002 | −0.017 / +0.052 | −0.9 |
| P6 late continuation | 1,728 | −0.018 | −0.044 | +0.026 | 2.9 (Holm sig.) | −0.082 / +0.009 | +0.039 / +0.041 | +1.6 |

Read: (1) every play beats its twin by +0.02 to +0.06R — the structure is there — but the twins themselves lose 0.03–0.15R,
so beating a random minute still lands at or below zero after cost. (2) The only Holm-significant edge (P6, going with the
close's location at 15:00) is +0.026R ≈ 1.6 pts a trade: real, too small to pay a prop fee. (3) P4 (go with a gap > 0.5 ATR) is
the one positive-net play and sits inside the strong-open family (2026-10-23_day1_race.md R4; edge_lab strong_open) — the
same single opening-momentum effect found a third way. (4) Fades of exhaustion, small gaps and the first ORB break all lose:
the 61 % fill / 69 % both-sides rates are true and still unprofitable because the move to get there costs more than it pays.
Conclusion: what is forecastable (size, activity, shape) sets RISK, not entries; the only entry edge the open gives is
momentum after a decisive move, already in the book's family. Nothing changes any config.

## Part 2 — walk-forward optimisation of P4 and P6 (written 2026-10-23 02:35 Cyprus, BEFORE the run)
Grid chosen now; the best variant by net R on 2014-19 (≥ 200 trades) is read ONCE on 2020-26. Luck bar reported: with N
variants the best in-sample t is expected ~ sqrt(2 ln N) by chance. P4: entry {09:35, 09:50}, gap threshold {0.3, 0.5, 0.75, 1.0}
× ATR5, stop {0.15, 0.25, 0.4} × ATR5, target {0.5, 0.75, 1.0, 1.5} × ATR5 or none (96 variants). P6: time {14:30, 15:00,
15:15}, location quantile {0.70, 0.80, 0.90}, stop {midpoint, 0.25 × range, 0.5 × range} (27 variants). PASS = the selected
variant's 2020-26 net R > 0 AND edge over twin ≥ +0.05R AND ≥ 200 OOS trades; else the in-sample pick was luck.

### Part 2 results (2026-10-23; research/programs/out/forecastable_opt.txt)
- **P4 big-gap go PASSES its line, weakly.** 120 variants (60 with ≥ 200 IS trades; luck bar t ≈ 2.9). IS best = 09:35, gap >
  0.3 ATR5, stop 0.15 ATR5, no target, hold to 15:55: IS +0.201R → **OOS 2020-26 +0.089R, edge over twin +0.053R, n 790,
  +2.0 pts a trade, t 0.7**. All top-5 IS variants are positive OOS (+0.06 … +0.10R); 33/60 variants positive OOS, median +0.01.
  Read: a real but thin opening-momentum effect (the strong-open family again), ≈ $4 a trade at 1 MNQ — a filter/overlay for
  the book's own opening trades, not a stand-alone account strategy.
- **P6 late continuation FAILS.** Every one of 27 variants is NEGATIVE in-sample (best −0.050R), so there was nothing to
  select; OOS +0.026R (t 2.1, +2.1 pts) is the same tiny end-of-day drift seen in Part 1. Dropped.
