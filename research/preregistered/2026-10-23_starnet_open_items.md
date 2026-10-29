# StarNet desk's open items — pre-registered 2026-10-23

Founder: "tell me starnet findings" → "test all". Written BEFORE any run. Tool: `build-exp/edge_lab run` on `data/edge_lab/cache`
(read-only), reports to the session scratchpad (no Postgres load, no write into data/edge_lab/). In-sample = 2010-07 → 2024-12; an
idea that PASSES all five edge_lab gates gets ONE look at 2025-01 → 2026-10 (≥ 30 trades, net > 0).

## Not re-run (already tested and closed; re-running would only add multiple-testing luck)
- Failed-cascade reversal: `cascade_fail_reverse` (2026-10-16_owned_data_ideas) FAIL at controls, 2023-26 −5.05.
- Earnings days: `cascade_earn` (2026-10-16_earnings_mornings) FAIL; 2026-10-19_expiry_earnings not adopted (t 0.60).
- Absorption at the stated threshold: tested 6+ times (orb_order_flow, rithmic_direction, behavior3, orderflow_5_a_day,
  flow_big_bars, desk_fl_absorb_nonf) — too rare or negative. One loosened spec is run below (X3) only because the desk asked
  for a threshold that fires ≥ 100 times.

## X1 — Leveraged-ETF close rebalance, TODAY's move (the desk's version used yesterday's return, the wrong mechanism)
Daily-reset leveraged/inverse ETFs must buy into the close after an up day and sell after a down day, in proportion to TODAY's
return from the prior close (Cheng & Madhavan 2009; Bogousslavsky 2021). Rule: at 15:30 (X1a) / 15:45 (X1b), if
!rollwin && |px − pc| ≥ p × ATR, trade in the direction of (px − pc) to 15:55, stop 0.15 ATR, no target; p ∈ {0.5, 0.75, 1.0}.
Confirm on ES. PASS = edge_lab's five gates.

## X2 — Market intraday momentum (Gao, Han, Li & Zhou 2018: the first half hour predicts the last half hour)
At 15:30, direction = sign(10:00 open − prior close), if !rollwin && |at(10:00) − pc| ≥ k × ATR, k ∈ {0, 0.25, 0.5}; stop 0.15 ATR,
flat 15:55. Confirm on ES. PASS = five gates.

## X3 — Absorption, loosened to fire (2023-03 → 2024-12, NQ flow days)
Completed 5m bar 10:00-14:30 whose aggressor delta is against its close by ≥ k × its volume (k ∈ {0.02, 0.05}) → trade with the
close, stop 0.2 ATR, flat 15:55, !rollwin. PASS = five gates (min 100 trades). Expected: fail (this is close to the delta-divergence
bar already measured at ≈ −1 pt/trade).

## X4 — Skip the roll window in book F's cascade (a data fix, not a new edge)
The NQ cache is a calendar-rolled continuous contract; across roll Thursday −3…+6 days `open`/`pc`/`atr` mix two contracts, and
expiry week trades the thin expiring contract. Fixed setting (no selection): so_cascade k1 0.25, k2 0.30, stop 0.35 ATR, 2R (book F's
cascade). Compare all days vs !rollwin vs rollwin only, NQ and ES, eras 2010-18 / 2019-24 / 2025-26 (the last is shown, not chosen on).
ADOPT the skip only if the roll-window trades' total net is < 0 in BOTH 2010-18 and 2019-24 on NQ AND skipping does not lower ES in
either era. (Gap retest already carries !rollwin.)

## Results (run 2026-10-23, after the text above)
Reports: `research/programs/out/x23/*.md`; specs `research/ideas/x23_*.json`. In-sample 2010-07 → 2024-12; nothing passed, so the
2025-26 holdout was not used for X1-X3.

| idea | verdict | in-sample best (all variants) | walk-forward | why it fails |
|---|---|---|---|---|
| X1a LETF close from 15:30 | FAIL at SCREEN | p 0.5: −0.21 pts/trade (every variant < 0) | +1.69 on 469, PF 1.16 | positive only 2019-22; 2023-26 −3.86; ES −195 |
| X1b LETF close from 15:45 | FAIL at SCREEN | p 0.5: −0.27 (every variant < 0) | +2.82 on 455, PF 1.39, controls pass | 2010-18 −1.03, 2023-26 −1.47; the walk-forward profit is 2019-22 only (picking whatever worked last year) |
| X2 intraday momentum (Gao et al.) | FAIL at SCREEN | k 0.5: −0.34 (k 0: −1.17 on 3,045) | +0.20 on 483 | none |
| X3 absorption, loosened | FAIL at CONTROLS | k 0.05: +0.28 on 251 | +1.96 on 73 | random time/day as good; still only 73 walk-forward trades |

**X4 roll window, fixed book-F cascade (k1 0.25, k2 0.30, stop 0.35 ATR, 2R), total net pts (per trade):**
| | 2010-18 | 2019-24 | 2025-26 (shown only) |
|---|---|---|---|
| NQ all days | +1,101 (+0.80, n 1,378) | +8,282 (+8.37, n 989) | +2,919 (+13.71, n 213) |
| NQ roll window only | **+212** (+1.20, n 177) | **+38** (+0.32, n 118) | +162 (+6.76, n 24) |
| ES all days | +28 (+0.02) | +848 (+0.94) | +855 (+4.67) |
| ES roll window only | +59 (+0.36) | −221 (−1.84) | +31 (+1.37) |
Rule (roll-window trades < 0 in BOTH eras on NQ) NOT met: on NQ they are positive in both eras → the skip is NOT adopted. The desk's
"roll-window days lose −4.28 pts/trade" came from its walk-forward (each month picks a different variant on 18 roll trades a year),
not from book F's fixed setting. Book F stays as it is.
