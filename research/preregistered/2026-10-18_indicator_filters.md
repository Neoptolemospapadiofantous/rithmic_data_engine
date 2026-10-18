# Pre-registration — course indicators as FILTERS on the path × direction trade (written 2026-10-18, BEFORE any run)

Founder sent a trading course's module list (CVD, open interest, funding rates, order flow · moving averages, EMAs, VWAP,
Bollinger, Keltner, volume profile · RSI, MACD, momentum), then "run the indicator filter study". Every one of these failed
as a stand-alone ENTRY (2026-10-16 family sweep, 2026-10-17 EMA sweep). Here they are tried only as FILTERS on the two
confirmed path × direction rules (2026-10-18_path_direction_map): does the indicator at the 10:00 candle close pick the
better days? Open interest and funding rates are not tested (crypto-only / no NQ data locally).

## Bases (fixed, from the map) — entry at the 10:00 candle close, stop 0.25 ATR, 2R, flat 15:55, one a day
- GAP: gap side (|gap| ≥ 0.1 ATR, not the roll window), path eff ≥ 0.55, price on the gap side of the open.
- MOM: 45-minute momentum side (er(1,45) ≥ 0.3, price on that side of VWAP and the open), path eff ≥ 0.55.

## Filters (D = the base's direction; all on completed 5-minute bars or RTH-so-far values at 10:00)
1. RSI(14, 5-min) on D's side of 50 · 2. MACD line ema(12) − ema(26) (5-min) on D's side of 0 · 3. "Bollinger" — price beyond
the VWAP ± 1 SD band in D's direction (edge_lab has no plain standard deviation; the RTH VWAP band is the stand-in) ·
4. Keltner: price beyond EMA(20, 5-min) ± 2 × ATR(10, 5-min) in D's direction · 5. EMA: price on D's side of EMA(20, 5-min) ·
6. VWAP: price on D's side of VWAP (MOM already requires it — reported, not selectable for MOM) · 7. Volume profile: price outside
the prior session's 70 % value area in D's direction (long above pvah, short below pval).
Each filter in two polarities: ON (filter true) and OFF (its complement). 7 × 2 × 2 bases = 28 cells.
8. CVD (RTH aggressor delta on D's side) exists only from 2023-03: reported for 2023–24 as description, never selectable.

## Discovery — 2010-07 → 2018-12 (`--quick` screen, per cell: trades, net/trade, t)
Selection rule (fixed now): per base, the cell (either polarity) with the highest net/trade that has ≥ 80 trades, nets
≥ 1.25 × the unfiltered base per trade over 2010–18, AND is above the base in both halves 2010–14 and 2015–18. At most one
pick per base; none qualifying = no filter.

## Confirmation — 2019-01 → 2024-12, the picks only
Full gates (`control_pct` 1 − 0.05/2 = 0.975, 400 seeds) AND net/trade ≥ 1.25 × its own complement over 2019–24 AND above the
unfiltered base (GAP +28.26, MOM +22.16 walk-forward). Then 2025+ once. A confirmed filter becomes a SIZE rule (more size when
on), not a skip, unless its complement nets < 0.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab; discovery `--quick` 2010-07 → 2018-12; confirmation via scripts/edge_lab.sh).

### Discovery 2010–18 (net pts/trade; unfiltered GAP +2.84 on 279, MOM +0.60 on 266)
| filter | GAP on / off | MOM on / off |
|---|---|---|
| 1 RSI(14) on D's side of 50 | +2.84 (279) / — (0) | +0.60 (266) / — (0) |
| 2 MACD line on D's side | +2.77 (276) / +8.71 (3) | +0.79 (262) / −11.57 (4) |
| 3 beyond VWAP ±1 SD ("Bollinger" stand-in) | +3.21 (195) / +1.97 (84) | **+1.32 (214)** / −2.34 (52) |
| 4 beyond Keltner EMA20 ± 2 ATR(10) | **+3.82 (188)** / +0.81 (91) | +0.69 (253) / −1.15 (13) |
| 5 EMA(20) on D's side | +2.84 (279) / — (0) | +0.60 (266) / — (0) |
| 6 VWAP on D's side | +3.00 (275) / −8.36 (4) | (required by MOM) |
| 7 outside prior value area | +3.18 (257) / −1.16 (22) | +0.76 (185) / +0.26 (81) |
RSI, EMA, MACD and VWAP are ON on (nearly) every day: a straight path with price beyond the open already implies them, so
they cannot filter this trade. Selection: GAP Keltner (+3.82 = 1.35 × base) FAILS the both-halves condition (2010–14 +1.83
vs base +2.07; 2015–18 +5.61 vs +3.57) and nothing else reaches 1.25 × → no GAP filter. MOM VWAP-band (+1.32 = 2.2 × base;
halves +1.48 vs +0.38, +1.17 vs +0.79) → the one pick.

### Confirmation 2019–24
`if_mom_vband_confirm` passes every gate (walk-forward +19.95/trade on 137, t 2.66, controls 100 %, eras +19.47 / +20.60),
BUT nets LESS than the unfiltered MOM rule (+22.16) and less than its own complement on the same years (screen: band ON
+18.93 on 186, band OFF +23.00 on 42) → **NOT CONFIRMED** (both adoption conditions fail). Holdout not looked at.

### CVD, 2023-03 → 2024-12 (description only)
GAP: CVD agrees on 62 of 70 days (+25.84) vs 8 against (+46.41); MOM: agrees on 71 of 71. Like RSI/EMA, almost always on.

**Verdict: no course indicator improves the path × direction trade.** Most are redundant with the straight-path condition
(on every day); the two that split the days (Keltner extension for GAP, the VWAP-band stretch for MOM) helped in 2010–18 but
not consistently (Keltner) or not out of sample (VWAP band). Trade the confirmed rules unfiltered.
