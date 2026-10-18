# Pre-registration — weekly path / direction and turn-of-month flows on the cascade (written 2026-10-18, BEFORE any run)

Founder: "test more". Base = the book's cascade (as in 2026-10-18_cause_effect_effort: +0.80 / +8.37 / +13.71 per trade in
2010–18 / 2019–24 / 2025–26). Cells restrict the base (a filtered check lets the next check fire).
1. **Weekly path × direction** (edge_lab `e5` / `r5` = the last 5 sessions' efficiency / move in ATR; straight = e5 ≥ 0.661, the
   2010–18 top tercile measured earlier today): week straight & cascade WITH the week's direction · week straight & AGAINST ·
   week not straight. 3 cells.
2. **Turn of the month** (Lakonishok–Smidt / Ariel: index inflows in the last trading day and the first three of the month;
   edge_lab `tdom` = trading day of the month, `tdleft` = trading days left): TOM = tdleft ≤ 1 or tdom ≤ 3. Cells: TOM longs ·
   TOM shorts · rest longs · rest shorts. 4 cells.
Rule (fixed now, as before): SIZE-UP if net/trade ≥ 1.25 × the base in BOTH 2010–18 and 2019–24; SKIP if < 0 in BOTH; else
nothing; 2025–26 reported once. (For the long / short cells the bar is the base of the same side.)

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick` screens; net/trade at 1 pt, n).
| cell | 2010–18 | 2019–24 | 2025–26 | rule |
|---|---|---|---|---|
| base | +0.80 (1,378) | +8.37 (989) | +13.71 (213) | |
| longs / shorts (side bases) | +0.79 (729) / +0.57 (694) | +8.03 (535) / +9.85 (494) | −7.06 (114) / +32.11 (108) | |
| week straight, WITH the week | −1.97 (209) | −7.28 (136) | +32.52 (31) | **SKIP** (< 0 in both) |
| week straight, AGAINST | −1.29 (237) | +15.64 (142) | −7.39 (36) | no |
| week not straight | +1.80 (946) | +9.76 (721) | +14.40 (149) | no (2019–24 under +10.46) |
| turn of month, longs | +1.77 (192) | +13.43 (137) | +4.68 (32) | **SIZE-UP** (vs longs ≥ 0.99 / ≥ 10.04) |
| turn of month, shorts | +0.56 (176) | +3.73 (124) | +42.12 (27) | no |
| rest of month, longs | +0.44 (537) | +6.18 (398) | −11.65 (82) | no |
| rest of month, shorts | +0.58 (518) | +11.90 (370) | +28.78 (81) | no |
**Verdict (the written rule):** SKIP a cascade that goes WITH a straight week (both periods negative — the same shape as the
gap rule paying less after a straight fortnight, 2026-10-18_daily_path_direction); SIZE UP cascade LONGS at the turn of the month
(the last trading day and the first three — the documented month-turn inflow). Caveats, stated with the verdict: 2 of 7 cells pass
(more than chance alone at this bar, but not by much); the skip rule's 2025–26 look goes the other way (+32.52 on 31 trades); the
turn-of-month longs beat the long base in 2025–26 (+4.68 vs −7.06). Both go to the forward record (paper / the /orb guide notes),
not into sizing, until it agrees.
