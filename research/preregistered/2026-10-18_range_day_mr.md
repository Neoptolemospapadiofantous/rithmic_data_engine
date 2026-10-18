# Pre-registration — mean reversion on RANGE days defined by daily + intraday path (written 2026-10-18, BEFORE any run)

Founder: "everyday I wanna be able to see direction and path so I can trade a strategy mean reverse or trend". The TREND side
is confirmed (2026-10-18_path_direction_map, _daily_path_direction). Every mean-reversion test so far lost, including on
"no-direction" days (2026-10-17_no_direction_mr: nd_range −1.96, nd_vwap −4.27 at the in-sample best). This is one new try with
the sharper range-day definition from today's work, so the daily page can say honestly what to do on a range day.

RANGE day at the decision bar = the 10-session daily path is NOT straight (`e10 < 0.429`) AND the intraday path is choppy
(`abs(px − open) / max(hod − lod, 1) < 0.30`, the live rota's chop band). Scan 5-minute closes 10:00–14:00, one trade a day,
stop 0.25 ATR, flat 15:55, target t ∈ {1, 2} R. NQ 2010-07 → 2024-12, ES confirm, real cost; family 3 → `control_pct` 0.983,
400 seeds; holdout 2025+ once for any PASS.
1. `rd_vwap` — a 5-min bar closes beyond VWAP ± 1.5 × vsd → fade it (short above, long below).
2. `rd_range` — a 5-min bar pokes beyond the first-hour (09:30–10:30) range and closes back inside → fade it (from 10:30).
3. `rd_open` — price is ≥ 0.3 ATR from the 09:30 open (and still a range day by the path) → fade toward the open.
Mean reversion is adopted for range days only on a full PASS (every gate) AND long and short trades each net > 0. Otherwise the
daily page says "range day: stand aside".

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (scripts/edge_lab.sh, build-exp/edge_lab).
| idea | walk-forward net/trade (n) | screen tg=1 / tg=2 | eras 10-18 / 19-22 / 23-26 | verdict |
|---|---|---|---|---|
| rd_vwap | −2.78, PF 0.85 (529) | −1.15 / +0.32 (1,113) | +0.77 / −3.45 / −17.73 | FAIL at WALK-FORWARD |
| rd_range | −2.35, PF 0.82 (432) | −3.26 / −1.83 (810) | +0.13 / −8.00 / −3.85 | FAIL at SCREEN |
| rd_open | +0.07, PF 1.00 (108) | +1.02 / +0.66 (158) | −0.16 / +4.18 / −9.91 | FAIL at WALK-FORWARD |
**Verdict: no mean-reversion edge on range days either** — the sharper definition (no straight fortnight + a choppy morning)
does not rescue any fade; each loses in 2023–26. The daily page says "range day: stand aside".
