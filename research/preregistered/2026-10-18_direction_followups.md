# Pre-registration — follow-ups on the direction trade: path sizing, path runner, intraday Wyckoff (written 2026-10-18, BEFORE any run)

Founder: "try all and lets do more research", after the day's results: the edge is the day DIRECTION (09:50 cascade / gap
side) on a straight path; entry levels (SMC) and multi-session phases (Wyckoff) add nothing
(2026-10-18_smc_path_direction, 2026-10-18_wyckoff_phases). Three questions. Path efficiency everywhere = the live rota's
`eff = |px − open| / max(hod − lod, 1)` at the decision bar (RTH so far), threshold 0.55 = its trend band (never optimised).
NQ 2010-07 → 2024-12 (`"to": "2024-12-31"`), ES confirm, real cost; holdout 2025+ once for any PASS.

## (A) Path at entry as a skip / size rule on the two passing setups
Ideas: `so_cascade_eff` = `so_cascade` (same grid, checks, stop, 2R) with `eff >= 0.55` added to every check;
`so_cascade_eff_not` = the same with `eff < 0.55`; `gap_or_retest_eff` / `gap_or_retest_eff_not` likewise on `gap_or_retest`.
Four ideas → `control_pct` 1 − 0.05/4 = 0.9875 (400 seeds, default claims). Read as SIZING when both halves are positive:
adopted (as "size up when straight") only if the eff idea passes every gate AND nets ≥ 1.5 × its complement per trade in
walk-forward AND the complement is ≥ 0; adopted as SKIP only if it passes and the complement nets < 0.

## (B) Runner on straight-path days (exit rule `trail_path`, src/research/meta.hpp ExitSim)
On a trade whose entry has eff ≥ 0.55: no target, the 15-minute trail runner (the existing `trail_run` mechanics); other
trades keep the base exit (stop + 2R + 15:55). Run as `exits` on `so_cascade` and `gap_or_retest`, beside the existing rules.
Control (new, for this rule): the GATE-SHUFFLE twin — the runner given to the same NUMBER of trades chosen at random,
200 seeds; must beat it in ≥ 99 % (the exits bar), plus the other exits gates (better per trade than base, better in ≥ 2
of 3 eras incl. 2023–26, higher prop break-even fee).

## (C) Wyckoff inside the session: does a midday range after the morning move break with it or against it?
Morning move M = at(10,30) − open, |M| ≥ 0.4 ATR (a real move). Range = 10:30 → the decision bar, high `hi_since(10,30)` /
low `lo_since(10,30)`, valid only while its height ≤ 0.5 × |M| (a consolidation, not a reversal already). Break = the
first completed 5-minute close above the range high or below its low, scanned 11:30–14:00, entry next bar's open, stop
the range's far side (≥ 0.1 ATR), target 2R, flat 15:55.
`iw_cont` = only breaks in the morning direction (re-accumulation / re-distribution: continuation).
`iw_rev` = only breaks against it (Wyckoff distribution after a markup / accumulation after a markdown: reversal).
Two ideas → `control_pct` 0.975, 400 seeds. `dir` = the break side, so the time control is a random bar with the break-side
rule evaluated there (no trade where no break). Read: whichever passes names the intraday phase; neither = no edge.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab; `run` via scripts/edge_lab.sh, 400 seeds; `exits` 200 seeds; reports data/edge_lab/reports).

### (A) path at entry
| idea | walk-forward net/trade (n) | t | eras 10-18 / 19-22 / 23-26 | controls dir / time / day | verdict |
|---|---|---|---|---|---|
| so_cascade_eff | +4.24, PF 1.22 (1,898) | 2.99 | +1.13 / +7.63 / +7.69 | 100 / 1 / 88 % | FAIL at CONTROLS |
| so_cascade_eff_not | +4.83, PF 1.26 (680) | 2.11 | −0.53 / +2.85 / +19.79 | 96 / 100 / 83 % | FAIL at CONTROLS |
| **gap_or_retest_eff** | **+11.19, PF 1.61 (686)** | **4.36** | **+2.35 / +20.27 / +22.16** | **100 / 99 / 100 %** | **PASS every gate** (prop break-even fee $24,823 at 2 MNQ) |
| gap_or_retest_eff_not | +7.77, PF 1.30 (600) | 2.45 | +0.47 / +10.66 / +10.51 | 92 / 6 / 92 % | FAIL at CONTROLS |

Adoption rule: `gap_or_retest_eff` passes but nets 1.44 × its complement (need ≥ 1.5) and the complement is positive →
**not adopted as sizing, not adopted as a skip** (the rule written before the run decides). The cascade: the path does
nothing (straight +4.24 vs not +4.83). Holdout, one look (the PASS only; full-history run, walk-forward trades ≥ 2025):
2025 59 trades +1,656 pts, 2026 10 trades +693 → **69 trades, +2,349 pts: holdout PASS** (≥ 30, net > 0). Not a clean
holdout: the plain gap retest's own 2025+ result was seen before this split was written (2026-10-17), and 2026 has only 10
trades (the data gap 2026-03-20 → 09-18).

### (B) runner on straight-path days (`trail_path`)
| base | base net | trail_path net | shuffle twin | eras better | verdict |
|---|---|---|---|---|---|
| so_cascade (2,322) | +11,139 | +7,906 | +7,512 (35 % beaten) | 0/3 | FAIL: not better than base |
| gap_or_retest (1,220) | +11,015 | +8,875 | +8,166 (79 %) | 1/3 | FAIL: not better than base |
The 2R target beats a runner on straight days as on any day; the existing thesis exits still pass unchanged
(so_cascade vwap_stall, gap_or_retest open_run).

### (C) intraday Wyckoff
| idea | walk-forward net/trade (n) | eras | verdict |
|---|---|---|---|
| iw_cont (break WITH the morning move) | +1.98 (47) | +7.27 / −1.13 / −5.00 | FAIL at CONTROLS (72 % direction), latest era < 0 |
| iw_rev (break AGAINST it) | +8.70 (26) | n 21 / 4 / 1 | FAIL at SCREEN (< 100 trades) |
The setup is rare (a ≥ 0.4 ATR morning move that then holds a range ≤ half its size until a midday break: ~5 a year, 73 trades in 14.5 years)
— too few trades to say which way a midday range breaks.

**Verdict:** one PASS that the pre-registered rule does not adopt — the gap retest is better on a straight path (+11.19
vs +7.77, holdout +2,349 on 69 trades) but not enough better (1.44 ×) to call it a size-up rule. It is the founder's call
whether to paper it as a variant (`gap_or_retest` + path ≥ 0.55 at entry); the live rota's `regime_min_eff` key
measures the same efficiency (whether the trend engine applies it at a gap-retest entry is not checked yet). Nothing else adopted.
