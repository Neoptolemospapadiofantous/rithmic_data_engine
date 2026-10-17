# No direction → mean reversion (pre-registered 2026-10-17, before any run)

Founder: "if we don't break any of them should we switch to a mean-reverse strategy". Hypothesis: on days where none of the
direction signals fired, the market is balancing and its extremes revert. Earlier fades (vwap_fade, range_day_fade,
cond_rev_*, exhaustion_fade) were run on ALL days and failed; this tests them on no-direction days only, each with an
all-days twin so the effect of the condition is visible.
- No-direction day (known by 10:30): |09:50 − open| ≤ 0.25 ATR AND |10:30 − open| ≤ 0.30 ATR (no cascade) AND NOT (VIX ≥ 20 AND
  (prior IBS ≤ 0.3 OR RSI(2) ≤ 20)) (no weak-day long).
- Fades, completed 5-minute bars 10:35–15:00, first only, one trade a day:
  nd_range / all_range   a bar pokes beyond the first-hour (60-min) range and closes back inside → fade it
  nd_vwap  / all_vwap    a bar closes beyond VWAP ± b·sd (b 1.5 / 2) → fade toward VWAP
  nd_open  / all_open    price ≥ 0.25 ATR from the open → fade toward the open
- Exits: stop 0.15 / 0.25 ATR, target 1 / 1.5 R, flat 15:55. Data 2010-07 → 2024-12, 2025+ held back.
- Mean reversion "works on no-direction days" only if an nd_* PASSES and beats its all_* twin per trade.

## Results (2026-10-17)
All six FAIL at SCREEN — even the in-sample best setting loses: nd_range −1.96 (all_range −2.60), nd_vwap −4.27 (all_vwap −1.36),
nd_open −6.52 (all_open −3.48) pts/trade walk-forward. Restricting to no-direction days helps the range fade slightly and makes the
VWAP and open fades worse; every era negative. Mean reversion is not the answer on no-direction days: stand aside.
