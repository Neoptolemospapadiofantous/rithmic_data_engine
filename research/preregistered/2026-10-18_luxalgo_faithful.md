# Pre-registration — LuxAlgo SMC as LuxAlgo defines it (written 2026-10-18, BEFORE any run)

Follow-up to `2026-10-18_luxalgo_smc.md`, whose "What is NOT tested" section named the gaps closed here. A review of that test
(2026-10-18, same day) found the DSL was correct and the runs reproduce exactly (smc_ob_retest 1,798 trades +0.59 FAIL WF,
smc_fvg_fill 350 trades −3.11 FAIL SCREEN), but two definitions were not LuxAlgo's:
- the ORDER BLOCK candle was the last opposite-close bar; LuxAlgo's Smart Money Concepts takes the bar with the LOWEST LOW
  (bullish) / HIGHEST HIGH (bearish) between the swing bar and the breaking bar;
- the FAIR VALUE GAP had no size filter and was filled by a CLOSE through it; LuxAlgo requires the middle bar to close beyond
  bar 1's extreme and its body (in %) to exceed the auto threshold, and removes a gap as soon as price TRADES through it.

These are re-rolls of a failed family, so the family-wise bar counts them with the six earlier ideas: 8 ideas →
`control_pct` = 1 − 0.05 / 8 = 0.994, `control_seeds` 400. Everything else as before: completed 5-minute bars, ET,
NQ 2010-07 → 2024-12 (`"to": "2024-12-31"`, holdout 2025+ judged once for any PASS), ES confirm, real cost (1.0 pt NQ /
0.35 ES), one trade a day, flat 15:55, entry at the next bar's open.

## DSL additions (src/research/edge_lab.hpp) — an optional last argument `mode`, default 0 = the 2026-10-18 definitions
- `obh/obl/obhs/obls(tf, n, 1)`: on a bullish break at bar b (first close above the last unbroken swing high, swing bar p),
  the block is the bar with the lowest low among bars p+1 … b−1 (ties → the most recent); box = [its low, its high]. No bar
  in that span → no block. Mirror for bearish (highest high). Mitigation unchanged = LuxAlgo's default "Close": a later close
  below the box low (bullish) / above the box high (bearish). `swh/swl` unaffected by mode.
- `fvgt/fvgb/fvgts/fvgbs(tf, 1)`: bars 1, 2, 3 completed in order. Bullish when bar-3 low > bar-1 high AND bar-2 close >
  bar-1 high AND bar-2 body% = (close − open) / open > T; bearish mirror (bar-3 high < bar-1 low, bar-2 close < bar-1 low,
  −body% > T). T = 2 × the mean |body%| of every tf bar of the series completed before bar 3 (LuxAlgo's auto threshold:
  `ta.cum(abs(delta)) / bar_index * 2`, cumulative from the first bar of the data). Filled = a later bar's LOW below the gap
  bottom (bullish) / HIGH above the gap top (bearish). Most recent unfilled gap, NaN if none.

Not replicated (stated so it is not claimed): LuxAlgo's high-volatility bar parsing for order blocks (bars with range
≥ 2 × ATR(200) have high/low swapped), its "confluence" filter, volumetric order blocks, and LuxAlgo's swing confirmation
(it uses `ta.highest` over `length` bars to the RIGHT only: a bar is a swing top when it is the highest of the next `length`
bars). Our swings stay the symmetric n-each-side pivot.

## Test (e) — order-block retest, LuxAlgo block (`smc_ob_retest_lux`)
Exactly `smc_ob_retest` with mode 1. Grid n ∈ {5, 50} (LuxAlgo's internal and swing lengths), rr ∈ {2, 3} → 4 variants.

## Test (f) — fair-value-gap fill, LuxAlgo gap (`smc_fvg_fill_lux`)
Exactly `smc_fvg_fill` with mode 1 (enter when the last bar traded into the gap — low < top — and closed above the top; with
the wick fill rule a bar that trades below the bottom has already removed the gap, so the entry bar must hold inside it).
Grid rr ∈ {2, 3} → 2 variants.

## Adoption
Only on PASS of every gate, then the 2025+ holdout once, then paper. A PASS whose net per trade is within the cost of the
earlier definition's result is reported as "definition-sensitive", not as an edge.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab, reports data/edge_lab/reports/<id>.md). The mode-0 ideas re-ran identical
(smc_ob_retest 1,798 / +0.59, smc_fvg_fill 350 / −3.11), so the DSL change did not move the earlier results.

| test | idea | walk-forward net/trade | trades (/yr) | eras 2010-18 / 19-22 / 23-26 | gate reached | verdict |
|---|---|---|---|---|---|---|
| (e) | smc_ob_retest_lux | −0.62 (PF 0.96) | 923 (56) | −1.40 / +4.96 / −6.15 | FAIL at SCREEN — every variant < 0 (n=5: −1.20 / −1.35; n=50: −2.53 / −3.15) | FAIL |
| (f) | smc_fvg_fill_lux | −1.65 (PF 0.90) | 1,366 (83) | −0.07 / −3.53 / −2.34 | FAIL at WALK-FORWARD — rr=3 +0.18 in-sample (t 0.26), rr=2 −0.62 | FAIL |

**Verdict: nothing adopted.** LuxAlgo's own definitions do no better than ours: the lowest-low block is retested on
about half as many days (923 vs 1,798 walk-forward trades) and nets below zero; the filtered, wick-filled gap trades about
4× as often as the unfiltered close-filled one (1,366 vs 350 — not investigated why) and nets below zero in every era. With the 2026-10-16/18 results the whole LuxAlgo SMC toolkit is now tested on NQ
2010–24 — swings / BOS / CHoCH, EQH / EQL sweeps, order blocks (two definitions), FVGs (two definitions), premium /
discount, ORB targets — and no part of it is an entry or exit edge at real cost. The /orb SMC tab stays context.
