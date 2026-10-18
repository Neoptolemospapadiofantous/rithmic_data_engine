# Pre-registration — Wyckoff market phases (written 2026-10-18, BEFORE any run)

Founder: "market phases accumulation, mark up, distribution or mark down are the market phases." Never tested here before
(no earlier idea or note mentions Wyckoff / accumulation / distribution). Follows 2026-10-18_smc_path_direction, which found
the day DIRECTION is the edge and entry levels add no timing — so the question is whether the phase is a direction source,
alone or on top of the plan direction.

## Phase inputs (new daily variables, src/research/bars.hpp, known before the day — prior ok sessions' RTH closes C, volume)
`e20` = |C[-1] − C[-21]| / Σ|ΔC| over those 20 steps · `r20` = (C[-1] − C[-21]) / atr · `r60` = (C[-21] − C[-61]) / atr ·
`uv20` = share of the last 20 sessions' RTH volume on up-close sessions. Unit-tested (test_edge_lab, steady uptrend).
Distribution on NQ 2010-07 → 2024-12 (3,469 sessions, looked at BEFORE any outcome): e20 terciles 0.143 / 0.315
(median 0.22), r20 median +1.7, r60 25th pct −0.27, median +2.9, uv20 median 0.52. Continuous front-month closes: roll
jumps sit inside these windows (NQ roll jumps are small next to 20-session moves; not adjusted).

## Phases (thresholds = the e20 terciles, fixed here)
- **Markup** e20 ≥ 0.315 and r20 > 0 · **Markdown** e20 ≥ 0.315 and r20 < 0 (a straight 20-session trend)
- **Accumulation** e20 ≤ 0.143 (a 20-session trading range) and r60 < 0 (it follows a decline)
- **Distribution** e20 ≤ 0.143 and r60 > 0 (it follows a rise)
- otherwise no phase (the middle tercile).
Phase direction: markup / accumulation +1, markdown / distribution −1 (Wyckoff: accumulation resolves UP, distribution DOWN).

## Ideas (NQ 2010-07 → 2024-12, `"to": "2024-12-31"`, ES confirm, real cost; holdout 2025+ once for any PASS)
1. `wy_trend` — 09:30 open, the markup / markdown direction, hold to 15:55. Grid stop ∈ {0.35, 1.0} × ATR, no target.
2. `wy_range` — 09:30 open, accumulation long / distribution short (the Wyckoff reversal claim), hold to 15:55. Same grid.
3. `wy_range_vol` — as 2 with Wyckoff's effort confirmation: accumulation only with uv20 > 0.5, distribution only with
   uv20 < 0.5. Same grid.
4. `dp_phase` — `dp_base` (2026-10-18_smc_path_direction: the day direction D on a straight path, p ∈ {0.30, 0.55}) only when D
   equals the phase direction; `dp_phase_not` = D against the phase direction (the complement; no-phase days in neither).
Family: 5 ideas → `control_pct` = 1 − 0.05 / 5 = 0.99, 400 seeds, default claims (direction AND time-or-day).
**Drift rule**: NQ rose over the sample, so a long-leaning rule beats random direction on drift alone. An idea passes only if
its walk-forward LONG and SHORT trades are each net positive (reported from the trade list), on top of the gates.
For `dp_phase`: adopted only if it passes AND nets more per trade than `dp_phase_not` AND than `dp_base` (+3.72).
The flip of 2 (accumulation short / distribution long = the range continues the prior trend) is read off `wy_range`'s random
-direction control and its sign — not a separate idea.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab via scripts/edge_lab.sh, 400 seeds, need 99 %; reports data/edge_lab/reports/<id>.md).

| idea | walk-forward net/trade (n) | t | eras 10-18 / 19-22 / 23-26 | controls dir / time / day | gate reached |
|---|---|---|---|---|---|
| wy_trend | +2.49, PF 1.09 (505) | 0.65 | −0.44 / −17.19 / +20.05 | 83 / 98 / 29 % | FAIL at CONTROLS (direction 83 %); robustness 4/7 |
| wy_range | +3.34, PF 1.10 (719) | 0.84 | −3.21 / +22.74 / −18.06 | 74 / 29 / 72 % | FAIL at CONTROLS; latest era < 0; prop fee $51 |
| wy_range_vol | +4.33, PF 1.18 (351) | 0.98 | −1.76 / +32.07 / −21.89 | 77 / 85 / 88 % | FAIL at CONTROLS; latest era < 0 |
| dp_phase | +1.43, PF 1.08 (475) | 0.59 | +0.99 / +2.15 / +1.09 | 84 / 13 / 84 % | FAIL at CONTROLS |
| dp_phase_not | +2.20, PF 1.15 (548) | 1.13 | +1.16 / +1.72 / +14.08 | 86 / 54 / 84 % | FAIL at CONTROLS |

The drift rule (long and short legs each positive) was not needed: every idea fails a gate first.

**Verdict: nothing adopted.** The Wyckoff phase, defined from the last 20 / 60 sessions' closes and volume, does not predict
the next session's direction on NQ 2010–24: no idea beats random direction (74–86 % of seeds against 99 % needed), and the
positive averages come from one era each — the range "reversal" (`wy_range`, `wy_range_vol`) made all of it in 2019–22 and
lost in 2010–18 and 2023–26; the trend phase (`wy_trend`) lost in 2010–22 and made it all in 2023–26. As a filter on the
plan direction the phase does nothing: the trades AGAINST the phase net +2.20 per trade, WITH it +1.43 (`dp_base` +3.72).
Not tested here (so not claimed): phases on an intraday scale (a range inside the session after a morning move), Wyckoff
events (spring / upthrust = failed breakouts of the range — the liquidity-sweep and break-box-trap tests of 2026-10-16/17
covered the intraday version and failed), and phases on a weekly scale.
