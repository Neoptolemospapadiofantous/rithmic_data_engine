# Pre-registration — SMC retests with the day's PATH and DIRECTION (written 2026-10-18, BEFORE any run)

Founder: "incorporate path and direction to the mix". The SMC entries failed on their own (2026-10-18_luxalgo_smc,
2026-10-18_luxalgo_faithful): an order-block or gap retest in EITHER direction on ANY day nets about zero or less. Question
here: do they work as PULLBACK entries — only in the day's established direction, only on days whose path so far is straight?

## The two gates (both already used elsewhere, values taken from there, not optimised here)
- **Direction D** = the Plan page's day direction: the 09:50 cascade side when |at(9,50) − open| > 0.25 ATR, otherwise the
  gap side when |gap| ≥ 0.1 ATR (and not in the roll window); otherwise no direction (no trade).
  `CAS = abs(at(9,50)-open) > 0.25*atr`, `D = CAS*sign(at(9,50)-open) + (1-CAS)*(!rollwin && abs(gap) >= 0.1)*sign(gap)`.
- **Path** = the live rota's efficiency since the open, eff = |px − open| / (hod − lod), with price on D's side of the open:
  `abs(px-open)/max(hod-lod, 1) >= p && sign(px-open) == D`. p ∈ {0.30, 0.55} = the rota's middle and trend bands
  (scripts/live_rota.sh RANGE_MAX / TREND_MIN).

## Ideas (5-minute scan 09:55–14:00, one trade a day, entry next bar's open, target 2R, flat 15:55)
1. `smc_ob_dp` — order-block retest (2026-10-18 rule: bar traded into the most recent unmitigated block and closed back
   beyond its near edge, block older than one bar) on D's side only, gates on. Stop 1 tick beyond the block's far edge,
   ≥ 0.1 ATR. Grid: swing n ∈ {2, 5} × block definition m ∈ {0 = last opposite close, 1 = LuxAlgo lowest-low} × p → 8 variants.
2. `smc_fvg_dp` — fair-value-gap fill, same way. Grid m ∈ {0, 1 = LuxAlgo threshold + wick fill} × p → 4 variants.
3. `dp_base` — the reference with no level: the first 5-minute bar in the window where both gates are on, in D's direction,
   stop 0.25 ATR. Grid p → 2 variants. Reported, not adopted on its own (it is the momentum/cascade family already tested).

**`dir` = D in all three**, so the random-TIME control enters the same day, a random bar of the window, in the day's
direction with the trade's own stop and target: a PASS means the LEVEL adds something beyond "trade the day's direction".
The random-DAY control is not accepted as the second claim here (`claims`: direction AND time), because D already selects
the day.

Family: the SMC family had 8 ideas; with these 3 → `control_pct` = 1 − 0.05 / 11 ≈ 0.995, `control_seeds` 400.
NQ 2010-07 → 2024-12 (`"to": "2024-12-31"`), ES confirm, real cost 1.0 / 0.35 pt.

## Adoption
`smc_ob_dp` or `smc_fvg_dp` is adopted only if it passes every gate (incl. the time control at 99.5 %) AND nets more per
trade than `dp_base` with the same p in walk-forward. Then the 2025+ holdout once, then paper.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab via scripts/edge_lab.sh, reports data/edge_lab/reports/<id>.md, 400 seeds, need 99.5 %).

| idea | walk-forward net/trade (n) | eras 10-18 / 19-22 / 23-26 | controls: dir / TIME / day | real net vs random-time mean | gate reached |
|---|---|---|---|---|---|
| dp_base | +3.72, PF 1.20 (1,599) | +0.86 / +6.72 / +5.78 | 100 / 59 / 100 % | +5,953 vs +5,640 | FAIL at CONTROLS (time); robustness 7/7, prop fee $534 |
| smc_ob_dp | +0.97, PF 1.07 (746) | −0.34 / +1.85 / +3.13 | 92 / 1 / 78 % | +722 vs +2,745 | FAIL at CONTROLS; prop fee $13 |
| smc_fvg_dp | +2.43, PF 1.20 (1,225) | +1.06 / +5.13 / −0.16 | 100 / 65 / 99 % | +2,978 vs +2,625 | FAIL at CONTROLS (time); latest era < 0 |

Quick screen by variant: smc_ob_dp best m=0 n=2 p=0.55 +3.12 (t 2.35 < 2.73 needed for 8 variants), the LuxAlgo block (m=1)
and the middle band (p=0.30) mostly negative; smc_fvg_dp p=0.55 +2.25 / +2.22 (m=0 / 1), p=0.30 ≈ +0.9.

**Verdict: nothing adopted.** Direction and path carry the result: every idea beats random DIRECTION (dp_base and the FVG
version in 100 % of seeds), but none beats random TIMING — entering at a random 5-minute bar of the same window, in the day
direction, with the trade's own stop and target, does as well (FVG, base) or far better (order block: +2,745 vs +722). The
SMC levels do not add timing value inside a directional day; the order-block retest subtracts it. The direction itself is the
known edge (cascade / gap retest = one bet, 2026-10-18_plan_checks); p = 0.55 (straight path) beats p = 0.30 in every idea.
