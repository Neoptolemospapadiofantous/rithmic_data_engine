# Pre-registration — the two-edge book, the path definition, day of week (written 2026-10-18, BEFORE any run)

Founder: "more tests". GAP rule = pdm_gap_confirm (10:00 candle close, gap side, straight path ≥ 0.55, 0.25 ATR stop, 2R).
WEAK rule = the fixed form of weak_day_long that the dry `tradeify_day` executor runs: prior IBS ≤ 0.3 and VIX ≥ 20 → long at the
09:30 open, no stop, flat 15:55 (one variant, no grid).

## 1. Two-edge book on one account (description)
Same-day overlap: how often the WEAK long and a GAP trade fire on the same day, and how often the GAP trade is SHORT then (an
opposite position — banned on Tradeify, FTA §6.7). Resolution fixed now: on an opposite-direction day the WEAK long (already open
since 09:30) is kept and the GAP short is skipped. Book = WEAK + GAP (with that skip), 1 unit each, monthly walk-forward net:
total, max drawdown, losing months, vs each rule alone. 2011-07 → 2026.

## 2. Path definition robustness (pass / fail)
Replace the range-based path |px − open| / max(hod − lod, 1) by the 5-minute close efficiency of the six bars 09:30–10:00:
|close(10:00) − open| / Σ|Δ 5-min close| (the open as the first point) ≥ 0.55, everything else unchanged. PASS = net > 0 in
2010–18 AND 2019–24 and ≥ 50 % of the confirmed rule's 2019–24 screen (+22.48). A FAIL means the edge depends on the exact formula.

## 3. Day of week (description only): GAP rule Mon … Fri in 2010–18 and 2019–24; no rule unless a day is negative in both.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick` screens; monthly = walk-forward months 2011-07 → 2026-10, 178 months).

### 1. Two-edge book
Same day (2010–26): WEAK long and GAP long together 38 days — GAP +39.24/trade there; WEAK long and GAP SHORT 18 days — GAP
−1.72/trade → skipping the opposite GAP trade costs nothing (GAP per trade +12.48 with the skip vs +12.01 without).
| book (1 unit each, monthly) | total pts | max drawdown | total / drawdown | worst month | losing months |
|---|---|---|---|---|---|
| GAP (opposite days skipped) | +5,950 | 304 | 19.6 | −246 | 53 |
| WEAK long (no stop) | +8,220 | 1,395 | 5.9 | −608 | 21 |
| **both** | **+14,170** | **566** | **25.0** | −405 | 54 |
The two edges together keep most of each one's return with far less drawdown than the WEAK rule alone.

### 2. Path definition — PASS
5-minute close efficiency (six bars 09:30–10:00) instead of the range: 2010–18 +3.74/trade (236, t 2.89; the range form +2.84),
2019–24 +19.21 (198, t 3.59) = 85 % of +22.48. The edge does not depend on the exact path formula.

### 3. Day of week (2010–18 | 2019–24, net/trade, n) — no rule
Mon +2.11 (71) | +20.73 (47) · Tue +1.74 (66) | +6.46 (42) · Wed +0.77 (54) | +26.65 (35) · Thu +5.17 (52) | +32.11 (46) ·
Fri +6.02 (36) | +26.26 (44). Every weekday positive in both periods.
