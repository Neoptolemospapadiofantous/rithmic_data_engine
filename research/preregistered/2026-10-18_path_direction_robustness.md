# Pre-registration — robustness of the confirmed path × direction rules (written 2026-10-18, BEFORE any run)

Founder: "more tests", after `pdm_gap_confirm` / `pdm_mom_confirm` were confirmed (2026-10-18_path_direction_map) and no course
indicator improved them (2026-10-18_indicator_filters). Four checks; none can make the rules better by picking the best
neighbour — they can only show the result is fragile, or add an exit / a sizing rule under their own adoption bar.

## A. Plateau (description + a pass/fail bar) — 2019-01 → 2024-12, `--quick` screen
Vary ONE parameter at a time around the confirmed cell (10:00 entry, eff ≥ 0.55, stop 0.25 ATR, 2R):
entry 09:50 / 09:55 / 10:00 / 10:05 / 10:10 / 10:15 · eff 0.45 / 0.50 / 0.55 / 0.60 / 0.65 · stop 0.20 / 0.25 / 0.30 / 0.35 ATR ·
target 1.5 / 2 / 3 R. PASS = every neighbour nets > 0 per trade AND ≥ 50 % of the confirmed cell's net/trade. A failing
neighbour is reported as fragility; the confirmed cell is NOT replaced by a better neighbour.

## B. Exits (`edge_lab exits` on both confirmed ideas, 2019–24) — the existing 15 rules and their own gates (beat base per trade,
timing / twin control 99 %, eras, prop fee). Adopted only on PASS.

## C. Both-agree days, 2010-07 → 2018-12 (the years the 2019–24 split did not use)
Split the straight-path 10:00 days into: gap AND momentum agree · gap only · momentum only. The 2019–24 split (+32.9 / +13.7 /
+9.7) is CORROBORATED if 2010–18 shows agree-days > both single kinds per trade AND agree-days > 0. Then the sizing idea
"2 units when both agree, 1 otherwise" goes to paper as a hypothesis; it is not called an edge.

## D. Combined book (description) — one trade a day: the gap side when the gap rule fires, else the momentum side; 2019–24 and
2010–18 screens, net/trade and trades/yr, vs each rule alone.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab; A, C, D `--quick` screens; B `exits` written to data/edge_lab/reports/<id>__exits.md).

### A. Plateau 2019–24 — PASS for both rules (every neighbour > 0 and ≥ 50 % of the confirmed cell)
| parameter | GAP (confirmed +22.48 screen) | MOM (confirmed +19.68 screen) |
|---|---|---|
| entry 09:50 / 09:55 / 10:00 / 10:05 / 10:10 / 10:15 | +13.41 / +20.06 / +22.48 / +13.03 / +19.93 / +21.50 | +11.88 / +13.03 / +19.68 / +13.42 / +13.59 / +16.94 |
| path ≥ 0.45 / 0.50 / 0.55 / 0.60 / 0.65 | +19.46 / +23.02 / +22.48 / +26.20 / +24.23 | +20.11 / +20.11 / +19.68 / +19.39 / +18.44 |
| stop 0.20 / 0.25 / 0.30 / 0.35 ATR | +19.58 / +22.48 / +27.24 / +29.72 | +14.24 / +19.68 / +21.68 / +24.74 |
| target 1.5 / 2 / 3 R | +17.57 / +22.48 / +29.22 | +13.17 / +19.68 / +25.75 |
Weakest neighbour: GAP 10:05 +13.03 (58 %), MOM 09:50 +11.88 (60 %). Wider stops / bigger targets net more points per trade
(more risk per trade too); per the rule the confirmed cell is NOT replaced. MOM is insensitive to the path threshold (its
own 45-minute straightness already implies a straight path).

### B. Exits — FAIL on both (no rule passes): the fixed 2R target stays. Best attempts: GAP open_run / liq_run better in
both eras but fail the timing control (81 % / 28 %); MOM open_run likewise (70.5 %).

### C. Both-agree days on 2010–18 (never looked at before) — CORROBORATED
| days | 2010–18 | 2019–24 (from the map, for reference) |
|---|---|---|
| gap AND momentum agree | **+4.54 (100), t 2.30** | +32.93 (98) |
| gap only | +1.89 (179) | +13.65 (116) |
| momentum only | **−1.77 (166)** | +9.69 (130) |
Agree-days beat both single kinds and are > 0 in the earlier years too → "2 units when both agree, 1 otherwise" goes to paper as
a hypothesis. Momentum-only days LOST in 2010–18: the momentum rule's earlier-years edge sits on the agree days.

### D. Combined book (one trade a day: gap side if it fires, else momentum side)
2010–18: +1.12/trade on 445 (total +498) vs GAP alone +2.84 on 279 (+792) · 2019–24: +17.65 on 344 (+6,071) vs GAP +22.48 on
214 (+4,811), MOM +19.68 on 228 (+4,487). Adding the momentum-only days helped in 2019–24 and hurt in 2010–18 — era-dependent.

**Verdict:** both rules sit on a broad plateau (not a lucky cell); no exit beats 2R; the strongest and most stable form is
the GAP rule, sized up when momentum agrees. Momentum-only days are the fragile part (negative 2010–18).
