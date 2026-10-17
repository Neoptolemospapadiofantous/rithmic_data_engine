# Pre-registration — chart / candle patterns, and the condition matrix (written 2026-10-16, BEFORE any run)

Founder: "test all. and re test breakouts reversal trend and pull conditions etc." Data and pipeline validated the same
day (research/validation/2026-10-16_pipeline_validation.md). NQ 2010–26, 1.0 pt cost.

## B. Patterns — 9 patterns × {standalone, cascade direction} = 18 tests; controls 99.7 % (0.05 / 18), 400 seeds
Completed 15-minute bars, scan 10:00–15:00 (flag: 11:00–14:30), one trade a day, flat 15:55, stop k × ATR,
k ∈ {0.15, 0.25}, target 2R (2 variants each). "Cascade direction" = also require |px − open| > 0.25 ATR and the pattern's
direction = sign(px − open).
1 `pat_engulf` engulfing · 2 `pat_pin` pin bar / hammer (wick ≥ 60 % of the range, close in the outer third) ·
3 `pat_inside` inside-bar breakout · 4 `pat_outside` outside bar closing in its outer quarter · 5 `pat_3bar` three-bar
swing reversal · 6 `pat_double` double bottom / top (equal lows/highs within 0.05 ATR in the last 4 h, then a close beyond
the last 2 h's high/low) · 7 `pat_structure` higher low + break of the prior 2 h high (mirror) · 8 `pat_flag` first hour
≥ 0.5 ATR, three tight 15-minute bars (< 0.25 ATR), then a close beyond them in the first hour's direction ·
9 `pat_engulf_mtf` engulfing in the direction of the 60-minute trend (close vs its EMA20).

## C. Condition matrix — 4 families × 7 conditions = 28 combinations
Families (price only, unchanged specs): breakout `orb15_break_price`, reversal `onbox_sweep_price`, trend `so_cascade`,
pullback `ema_pullback_htf`. Conditions added to every entry: none · VIX ≥ 20 · VIX < 20 · VIX/VIX3M ≥ 1 · VVIX ≥ 100 ·
trade with the 50-day trend (direction × (prior close − SMA50) > 0) · not an FOMC day.
A combination is VALIDATED only if (a) it passes every edge_lab gate (controls 99.8 %, 0.05 / 28) AND (b) the frozen-
setting holdout is positive in BOTH splits (setting chosen on 2010–18 → tested 2019–22; on 2010–22 → 2023–26).

## Results (appended after the runs — tests unchanged)
### B. Patterns (gates at 99.7 %)
PASS the gates: `pat_3bar` (+2.51 pts/trade, 1,757 trades) and `pat_double_casc` (+3.15, 1,797). Everything else FAILS:
engulfing −0.84 (cascade direction +2.71), pin bar −2.63 (+0.40), inside bar −2.55 (+1.21), outside bar −4.08 (−0.36),
double alone +1.52, structure +1.07 (+2.16), flag +1.37 (+3.12, 199 trades), engulfing with the 60-minute trend +2.45
(+2.41). Frozen holdout of the two passes: `pat_3bar` chosen on 2010–18 was −0.78 there (t −3.0) and +0.56 on 2019–22,
chosen on 2010–22 −0.33 there and +7.15 on 2023–26 → no edge before 2023, NOT validated. `pat_double_casc`: +0.09 on
2010–18 → +2.39 on 2019–22 (t 0.9); +0.86 on 2010–22 → +9.81 on 2023–26 (t 2.7) → positive in both holdouts but no edge in
its development years; monthly corr 0.35 with the cascade — a forward-test candidate at most.
### C. Condition matrix (gates at 99.8 % + both frozen holdouts positive)
**No combination is validated.** The plain cascade (`cond_trend_none`) also fails this family's 99.8 % bar (random day
96 %) while its holdouts stay positive (+6.89 t 2.5 / +9.24 t 2.1) — its edge is the direction of the open move; the exact
trigger adds little over a random day of the same month in that direction. Best-looking but unvalidated: cascade on
VIX < 20 days (holdouts +10.53 t 3.0 / +14.44 t 2.5), cascade on VVIX ≥ 100 days (+9.48 / +13.49). Breakouts, reversals
and pullbacks: no condition makes them pass; VIX/VIX3M ≥ 1 is the worst condition for every family.
