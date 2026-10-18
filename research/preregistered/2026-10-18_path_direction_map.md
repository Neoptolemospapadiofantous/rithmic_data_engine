# Pre-registration — the PATH × DIRECTION map (written 2026-10-18, BEFORE any run)

Founder: "path and direction" → "all three" (map, paper, pages) → "stats". The day's results say the edge is the day
DIRECTION on a straight PATH, and that the gap retest is much stronger on a straight path (2026-10-18_direction_followups:
+11.19 vs +7.77). This maps where that holds, then confirms one cell out of sample.

## Discovery map — NQ 2010-07 → 2018-12 ONLY (`"to": "2018-12-31"`); nothing after 2018 is looked at in this step
One trade at a fixed entry time T, in direction D, stop 0.25 ATR, target 2R, flat 15:55, real cost 1.0 pt.
Three direction sources (one idea each, the page's definitions):
- `pdm_cascade`: D = side of the open when |at(9,50) − open| > 0.25 ATR (the 09:50 cascade), else none.
- `pdm_gap`: D = gap side when |gap| ≥ 0.1 ATR and not in the roll window, else none.
- `pdm_mom`: D = the 45-minute 1-min momentum side (er(1,45) ≥ 0.3, price on that side of VWAP and the open), else none.
Cells (= variants): entry T ∈ {10:00, 10:30, 11:00, 12:00, 13:00} × path band b of eff = |px − open| / max(hod − lod, 1)
at T ∈ {0: < 0.30, 1: 0.30–0.55, 2: ≥ 0.55} × side s ∈ {+1: price on D's side of the open, −1: against it} → 30 cells per
source, 90 in all. Reported per cell from the screen: trades, net/trade, PF, t.

## Selection rule (fixed now)
The cell with the highest t among cells with ≥ 150 trades in 2010–18, per source (3 picks). Each pick must also have
net/trade > 0 in BOTH halves 2010–14 and 2015–18 (from the screen's yearly table) — else the next cell by t.

## Confirmation — 2019-01 → 2024-12 (`"from": "2019-01-01"`, `"to": "2024-12-31"`), the 3 picks only
Full edge_lab gates on the fixed cell (one variant each, no grid), family 3 → `control_pct` 0.9833, 400 seeds, default
claims. A pick is CONFIRMED only if it passes every gate in 2019–24. Then the sealed 2025+ holdout once
(≥ 30 trades, net > 0). Only then a paper variant. The map itself (90 cells) is description, not evidence.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab; map `--quick` screen 2010-07 → 2018-12; confirmation via scripts/edge_lab.sh).

### Discovery map 2010–18 (net pts/trade at 1.0 pt cost; NQ was 2,000–7,000 then, so points are small)
- **Gap side, straight path (b 2), price on the gap side (s +1): positive at EVERY entry time** — 10:00 +2.84 (n 279, t 2.46),
  10:30 +2.64 (2.40), 11:00 +2.18 (2.18), 12:00 +0.93 (1.05), 13:00 +2.17 (2.37). Against the gap side, or choppy (b 0),
  mostly negative (worst 12:00 choppy on-side −3.28, t −2.88).
- Cascade side: weak everywhere (best n ≥ 150 cell 10:30 straight on-side +0.92, t 1.01).
- Momentum side: weak (best n ≥ 150: 10:00 straight +0.60, t 0.52); "against" cells are empty by construction (the momentum
  definition already requires price on its side of the open).
Picks by the rule (highest t with n ≥ 150, both halves > 0): gap 10:00 b2 s+1 (2010–14 +2.07 / 2015–18 +3.57),
cascade 10:30 b2 s+1 (+0.63 / +1.25), momentum 10:00 b2 s+1 (+0.38 / +0.79).

### Confirmation 2019–24 (one fixed cell each, full gates, need 98.3 %)
| pick | walk-forward net/trade (n) | PF | t | eras 19-22 / 23-24 | controls dir/time/day | verdict |
|---|---|---|---|---|---|---|
| **pdm_gap_confirm** (gap side, 10:00, eff ≥ 0.55, on side) | **+28.26 (163)** | 2.09 | 4.26 | +29.29 / +26.98 | 100 / 100 / 100 % | **PASS** (robustness 6/6) |
| **pdm_mom_confirm** (45-min momentum side, 10:00, eff ≥ 0.55) | **+22.16 (170)** | 1.79 | 3.36 | +21.34 / +23.21 | 100 / 100 / 100 % | **PASS** (robustness 6/6) |
| pdm_cascade_confirm (cascade side, 10:30, eff ≥ 0.55) | +17.99 (176) | 1.69 | 3.12 | +26.97 / −8.15 | 100 / 100 / 100 % | FAIL at ROBUSTNESS (latest era < 0) |
(The walk-forward needs 12 months of history, so its trades start 2020; the screen covers 2019–24.)

### Holdout 2025+ (one look, the two confirmed picks; full-history run, walk-forward trades)
- gap: 2025 42 trades +754 pts, 2026 8 trades +165 → **50 trades, +919 pts (+18.4/trade): PASS**
- momentum: 2025 40 trades +1,465, 2026 13 trades +983 → **53 trades, +2,448 pts (+46.2/trade): PASS**
Not perfectly clean: the gap retest's 2025+ result (a related rule) was seen on 2026-10-17 and again today; this exact rule
had never been run on 2025+ before. 2026 is short (the 2026-03-20 → 09-18 data gap).

### Overlap (description, after the verdicts — not a test): 10:00, straight path, 2019–24 screen
| days | trades | net/trade | PF | t |
|---|---|---|---|---|
| gap side AND momentum side agree | 98 | +32.93 | 2.68 | 4.21 |
| gap side, momentum absent or against | 116 | +13.65 | 1.52 | 1.91 |
| momentum side, gap absent or against | 130 | +9.69 | 1.36 | 1.46 |
The two confirmed picks share about half their days, and the shared days carry most of the result: they are one bet,
strongest when both direction sources agree on a straight path at 10:00.

**Verdict: CONFIRMED — `pdm_gap_confirm` and `pdm_mom_confirm` pass the out-of-sample confirmation and the holdout.**
Next per the rule above: a paper variant. They overlap, so paper them as one book entry each (not stacked on the same
day) until the forward record says otherwise; the agree-days split is a hypothesis for the forward test, not a finding.
