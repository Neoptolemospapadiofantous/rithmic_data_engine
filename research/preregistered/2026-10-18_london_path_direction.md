# Pre-registration — path × direction at the LONDON open (written 2026-10-18, BEFORE any run)

Founder: "more tests". The NY edge: the overnight gap's side, joined at 10:00 when the 09:30–10:00 path is straight
(2026-10-18_path_direction_map). Same idea one session earlier: the Asia session's move (18:00 → 03:00 ET) is the "gap", the
London open (03:00) is the "open", and the 03:00–03:30 path decides. If this works the desk has a second, earlier session.

Rule (`ldn_pd`): direction D = sign(at(3,0) − at(18,0)) when |at(3,0) − at(18,0)| ≥ 0.1 ATR (the Asia move, the gap analogue),
else no trade. At the 03:30 five-minute candle close: path = |px − at(3,0)| / max(whi(3,0,3,30) − wlo(3,0,3,30), 1) ≥ 0.55 and
price on D's side of at(3,0). Enter on that close, stop 0.25 ATR, target 2R, flat 09:25 (before the NY open, so it never overlaps
the NY rule). No variants. Real cost 1.0 pt, ES confirm.
Discovery 2010-07 → 2018-12 (screen): continue only if net > 0 in both halves 2010–14 / 2015–18. Confirmation 2019–24: full
gates (`control_pct` 0.95, single test, 400 seeds), then 2025+ once.

(Also recorded, not part of this test: the NY sizing book — 2 units when gap and momentum agree, 1 on gap-only days, half after a
straight fortnight — computed the same day from the four groups' screens: 2010–18 +1,300 pts vs flat +792, 2019–24 +7,079 vs
+4,811, 2025–26 +2,432 vs +919; weights stated before that run, the groups themselves from earlier findings on those years.)

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab, `--quick` screen). Discovery 2010–18: +0.25 pts/trade on 237 (t 0.25); halves 2010–14
+0.67 (117), 2015–18 −0.17 (120) → **FAIL at discovery** (second half < 0); not confirmed, no holdout look. The NY open's
path × direction effect does not carry to the London open (Asia move as the direction, 03:00–03:30 path).

### Same-day additions (description, not tests)
- Gap size (gap rule split; n, net/trade): small 0.1–0.3 ATR 2010–18 +3.55 (102) / 2019–24 +18.00 (80) / 2025–26 +34.69 (26);
  medium 0.3–0.6 +1.22 (109) / +19.80 (76) / +20.70 (12); large > 0.6 +4.35 (68) / +32.18 (58) / −19.33 (12). No stable
  ordering → no sizing by gap size.
- Sizing book month by month (walk-forward trades of the four groups, 2011-07 → 2026-10, 178 months): flat +5,161 pts, max
  drawdown 401, worst month −246 (2025-12); book (2 / 1 / 1 / 0.5) +9,150 pts, max drawdown 384, worst month −246, 45 losing
  months, losing years 2013 (−35) and 2019 (−8).
