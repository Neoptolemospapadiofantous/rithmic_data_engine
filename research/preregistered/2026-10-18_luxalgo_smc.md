# Pre-registration — LuxAlgo Smart-Money-Concepts entries not yet tested (written 2026-10-18, BEFORE any run)

Already tested and failed, not repeated here: equal highs / lows sweeps and breaks, the Asia / London / overnight / prior-day /
opening-range boxes with and without order flow, break → box → go traps (2026-10-16 liquidity_boxes, liquidity_boxes_all,
2026-10-17 break_box), BOS / CHoCH + EMA pullback (2026-10-18 bos_ema, m1dir_structure_ema), liquidity as the exit
(2026-10-18 liquidity_exit). Untested from the LuxAlgo SMC toolkit: the ORDER BLOCK retest, the FAIR VALUE GAP fill, the
PREMIUM / DISCOUNT gate, and the ORB 50 % targets. Four tests, six `run` ideas, two `exits` runs. All on completed 5-minute
bars, ET, NQ 2010-07 → 2024-12 (`"to": "2024-12-31"`, holdout 2025+ judged once for any PASS), ES as the confirm market,
real cost (1.0 pt NQ / 0.35 ES), one trade a day, flat 15:55. Family-wise controls on every `run` idea: `control_pct`
= 1 − 0.05 / 6 ideas = 0.992, `control_seeds` 400 (the complements count as ideas).

## Definitions added to the DSL (src/research/edge_lab.hpp; all per trading day since 18:00, completed tf bars only, no look-ahead)
- **Swing** (`swh(tf,n)` / `swl(tf,n)`): a tf bar whose high (low) is strictly above (below) the highs (lows) of the n bars on
  each side, all inside the day; confirmed n bars later. The functions return the level of the most recent confirmed swing
  (NaN before the first one).
- **Break of structure**: the first tf close above the last confirmed, not-yet-broken swing high (bullish; mirror below the
  last swing low). BOS and CHoCH are not distinguished — any first close through the level counts.
- **Order block** (`obh/obl(tf,n)` bullish top / bottom, `obhs/obls(tf,n)` bearish): on a bullish break at bar b, the LAST
  DOWN-CLOSE bar (close < open) between the swing-high bar and b − 1 defines the box [low, high]; no down-close bar in that
  span → no block. Mirror: the last up-close bar before a bearish break. **Unmitigated** = no later tf close below the box low
  (bullish) / above the box high (bearish); the functions return the most recently created unmitigated block, NaN if none.
- **Fair value gap** (`fvgt/fvgb(tf)` bullish top / bottom, `fvgts/fvgbs(tf)` bearish): three consecutive tf bars with
  bar-1 high < bar-3 low (bullish: gap = [bar-1 high, bar-3 low]); mirror bar-1 low > bar-3 high. **Unfilled** = no later tf
  close through the gap's far side (below the bottom for bullish, above the top for bearish); most recent unfilled, NaN if none.

## Test (a) — order-block retest (`smc_ob_retest`)
Scan 5-minute bars 09:50–14:00. Long when the last completed bar traded INTO the most recent unmitigated bullish block
(low ≤ block top) and closed back ABOVE its top, and the block already existed one bar earlier (`lag(5, obh) == obh`, so the
breaking bar itself cannot be the entry). Short mirror on the bearish block. Entry at the next bar's open. Stop = 1 tick below
the block low (above the block high for shorts), never tighter than 0.1 ATR. Target rr × R. Grid: swing length n ∈ {2, 5},
rr ∈ {2, 3} → 4 variants. `why`: the block is where the breakout's buyers were filled; their unfilled resting orders and the
stops of the traders who faded the break sit there, so a return to it is bought.

## Test (b) — fair-value-gap fill (`smc_fvg_fill`)
Same scan. Long when the last completed bar traded into the most recent unfilled bullish gap (low < gap top) and closed above
the gap top, the gap already existing one bar earlier. Short mirror. Stop = 1 tick below the gap bottom (above the top for
shorts), ≥ 0.1 ATR. Target 2R (one variant). `why`: the gap is a one-sided move that left no two-way business; the first
return fills resting orders from the side that missed the move.

## Test (c) — premium / discount gate on the two passing entries
Range = last confirmed swing high and low with n = 5 on 5-minute bars (`swh(5,5)`, `swl(5,5)`), mid = 50 %. DISCOUNT = price
(the decision bar's open) below the mid, PREMIUM = above. Gated ideas, each beside its complement (the exact negation of the
gate on the same base; a day with no swing yet is in neither):
- `cascade_pd`: `so_cascade` (same grid and checks) only when a long is in discount / a short in premium —
  `sign(px − open) · (px − mid) < 0`; `cascade_pd_compl` = the rest.
- `gap_or_retest_pd`: `gap_or_retest` (same scan) only when `sign(gap) · (px − mid) < 0`; `gap_or_retest_pd_compl` = the rest.
Adoption rule: a gated idea is adopted only if it passes every gate AND beats its complement per trade AND beats the ungated
base per trade on the same days (the base re-run with `to` 2024-12-31 — `so_cascade` is registered without `to`, so its
report mixes in 2025–26; the comparison figure is the 2010–24 re-run, not the registry number).

## Test (d) — ORB 50 % targets as the exit (`exits` rule `orbt`, src/research/meta.hpp ExitSim)
Range = 09:30–09:44 high H / low L, width w. Levels: H + k·w/2 above and L − k·w/2 below, k ∈ {1, 2} (LuxAlgo's ORB targets).
Rule `orbt`: target = the nearest level in the trade's direction at least 1R beyond the entry, filled 2 ticks before it; no
such level (or entry before 09:45) → the idea's own target. Stop unchanged. Control: the TARGET-DISTANCE twin used for `liq`
(the rule's own target distances in ATR shuffled across trades), ≥ 99 % of 200 seeds, plus the exits gates (better per trade,
better in ≥ 2 of 3 eras incl. 2023–26, higher prop break-even fee). Bases: `so_cascade`, `gap_or_retest`.
Adopted only on PASS; the other 13 exit rules are re-run unchanged in the same report.

## What is NOT tested here (so it is not claimed)
LuxAlgo's "internal vs swing" structure split (two pivot lengths), its volumetric order blocks, OB "lowest-low candle"
variant (we use the last down-close candle, as pre-registered above), FVG auto-threshold filter, and the OB / FVG as a
stop-placement for other entries.

## Results (appended 2026-10-18 after the runs — rules unchanged; reports in data/edge_lab/reports/<id>.md)
Reference for (c): the ungated `so_cascade` re-run on 2010–24 (scratch, not loaded) = **+4.27 pts/trade on 2,113 trades**
(PF 1.22; its registry report, +4.80, includes 2025–26); `gap_or_retest` is registered on 2010–24 already: **+9.03 on 1,220**.

| test | idea | walk-forward net/trade | trades (per yr) | eras 2010-18 / 19-22 / 23-24 | gate reached | controls dir / time / day | verdict |
|---|---|---|---|---|---|---|---|
| (a) | smc_ob_retest | +0.59 (PF 1.03) | 1,798 (109) | −1.47 / +1.52 / +1.81 | FAIL at WALK-FORWARD | 53 / 95 / 92 % | FAIL — best in-sample variant n=2 rr=3 only +1.16, t 1.6; the direction is a coin flip |
| (b) | smc_fvg_fill | −3.11 (PF 0.80) | 350 (21) | −0.83 / −10.28 / +1.61 | FAIL at SCREEN | 32 / 0 / 2 % | FAIL — loses at the real cost in every era but the last |
| (c) | cascade_pd | −0.58 (PF 0.97) | 170 (10) | −4.73 / −1.65 / +9.74 | FAIL at WALK-FORWARD | 84 / 98 / 32 % | FAIL — the gate keeps 8 % of the cascade's trades and they lose; worse than the base (+4.27) |
| (c) | cascade_pd_compl | +4.45 (PF 1.23) | 2,143 (130) | +1.06 / +8.09 / +8.34 | FAIL at CONTROLS (day 95 % vs 99.2 % family bar) | 100 / 9 / 95 % | = the base (+4.27 on 2,113): the gate removes nothing that mattered |
| (c) | gap_or_retest_pd | +4.86 (PF 1.23) | 113 (7) | −4.83 / −3.53 / +20.78 | FAIL at CONTROLS | 76 / 96 / 55 % | FAIL — 9 % of the base's trades, below the base (+9.03), negative in two eras |
| (c) | gap_or_retest_pd_compl | +8.94 (PF 1.42) | 1,191 (72) | +0.45 / +15.88 / +15.87 | PASS | 100 / 25 / 100 % | = the base (+9.03 on 1,220) minus the 113 gated trades — passes because the base does |
| (d) | so_cascade `orbt` | +4.53 vs base +4.80 (2,322 trades) | — | +0.73 / +6.12 / +9.95 (base +1.04 / +7.70 / +8.56) | exits | target-distance twin beaten 72 % (need 99) | FAIL — not better than base |
| (d) | gap_or_retest `orbt` | +8.72 vs base +9.03 (1,220) | — | +0.57 / +15.25 / +16.52 (base +0.42 / +16.11 / +16.89) | exits | twin beaten 86 % (need 99) | FAIL — not better than base |

**Verdict: nothing adopted.** (a) The order-block retest trades every day (109/yr) and nets ≈ 0 after cost — the block is where
price pauses, not where it turns; 2010–18 negative. (b) The fair-value-gap fill loses outright (−3.11/trade). (c) The
premium / discount gate is structurally at odds with both passing entries: a cascade long is by construction ABOVE the open
and usually above the mid of the morning's swing range, a gap retest long sits at the top of the opening range — so "discount"
keeps 7–10 trades a year and those lose in 2010–22. Each complement equals its ungated base. (d) The ORB 50 % targets cut the
winners short on both bases (win rate up 2–4 points, net per trade down 0.3) and lose to targets of the same sizes placed
without the level. Eight tests (six `run`, two `exits`) at the family-wise bar: 1 PASS, and it is the complement that
re-states a known edge. The other 13 exit rules re-ran unchanged (so_cascade still PASS vwap_stall, gap_or_retest PASS open_run).
