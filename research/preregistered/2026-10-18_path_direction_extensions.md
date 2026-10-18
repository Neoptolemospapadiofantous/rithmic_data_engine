# Pre-registration — path × direction extensions: second chance, ES, news days (written 2026-10-18, BEFORE any run)

Founder: "more tests", after the daily "direction & path" panel. Base = the confirmed GAP rule (pdm_gap_confirm): gap side
(|gap| ≥ 0.1 ATR, not the roll window), path |px − open| / max(hod − lod, 1) ≥ 0.55, price on the gap side, stop 0.25 ATR, 2R,
flat 15:55, one a day.

## 1. Second chance (`pdx_late`) — only days where the 10:00 read did NOT fire
At the first of 10:30 / 11:00 / 11:30 where the same three conditions hold, enter on that candle close. Discovery 2010–18
(`--quick` screen): continue only if net/trade > 0 in BOTH halves 2010–14 / 2015–18. Confirmation 2019–24: full gates
(`control_pct` 0.983 = 3 tests, 400 seeds), then 2025+ once. No variants (times fixed above).

## 2. ES replication (`pdx_es`) — the identical rule on ES (symbol ES, confirm NQ, real cost 0.35 pt), no tuning, 10:00 only.
Full gates on 2019–24 (`control_pct` 0.983) AND net > 0 on 2010–18 (screen). A pass = the effect is not NQ-specific.

## 3. News days (description only) — the GAP rule split by FOMC / CPI / PPI / NFP day vs the rest, 2010–24 screen. Not
adoptable here (too few news trades, many cells); a flag for the daily panel only if a news split is negative in BOTH
2010–18 and 2019–24.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab; discovery `--quick`; confirmations via scripts/edge_lab.sh).

### 1. Second chance — FAIL
Discovery 2010–18: +2.97/trade on 194 (halves +2.61 on 112, +3.46 on 82) → continued. Confirmation 2019–24
(`pdx_late_confirm`): walk-forward +9.89 on 81 (screen +4.89, t 0.89), controls direction 92 % / time 3 % / day 78 % (need 98 %),
2023–24 −17.64 (4 trades) → FAIL at CONTROLS. The 10:00 read stays the only one; a day that straightens later is not traded.

### 2. ES replication — PASS (holdout flat)
Discovery 2010–18 ES: +0.41 ES pts/trade on 282 (> 0). Confirmation 2019–24 (`pdx_es_confirm`): **PASS every gate** — walk-forward
+4.86 ES pts/trade on 179 (≈ $243 per ES contract, t 3.28), controls 99 / 100 / 100 %, eras +6.14 / +2.61, robustness 6/6.
Holdout 2025+ (one look): 2025 35 trades −6 pts, 2026 4 trades +13 → 39 trades +7 pts: a PASS by the letter (≥ 30, > 0) but flat
(+0.2/trade) while the NQ rule made +919 over the same period. The effect is index-wide in 2010–24; ES adds little in 2025–26.

### 3. News days (description) — no flag
GAP rule on FOMC / CPI / PPI / NFP days vs the rest: 2010–18 +7.36 (44) vs +1.99 (235); 2019–24 +32.88 (29) vs +20.85 (185).
News days are not worse (if anything better) — no warning on the daily panel.
