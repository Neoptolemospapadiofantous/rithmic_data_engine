# Overnight volume shocks — what NQ does after a "huge fill" minute (written 2026-10-19; event study first, then a pre-registered trade)

Founder: "today at globex we had a huge fill, check" (Monday 2026-10-19). 06:38 ET: NQ 29,687 → 29,759 in one minute on 2,168 contracts
(14× the night's average), ES 7,208 (35×) in the same minute; half given back by 06:41, re-tested 06:42, held 29,740–29,765. Also a buy
sweep at 02:17:19.890 (several 23-lots in one millisecond at 29,799.75) four minutes before the night's high (29,813, 02:21).

Event study (`research/programs/volume_shock_study.cpp`, out/volume_shock_study.txt; description): overnight minutes 18:05–09:25 with
volume ≥ K × the previous 60 min average and a move ≥ 0.05 ATR, signed in the shock's direction (ATR units):
- 08:25–09:25 (data releases), K 5 (106 / yr): +60 min −0.025 (t −3.6), +120 min −0.040 (t −3.6), all three eras negative; 61 % give back half
  within 5 min. K 20 (13 / yr): +15 min +0.040 (t 3.3, all eras positive), +120 min −0.038 — the biggest news runs first, then fades.
- London 03:00–08:25, K 10 (56 / yr): flat for 60 min, to the 09:30 open −0.043 (t −4.1; eras −0.051 / −0.007 / −0.048).
- Asia, K 5–20: small continuation (+0.005 … +0.018), not consistent across eras.

## Pre-registered trade (written BEFORE running)
`shock_fade_rel`: 08:25–09:25 ET, 1-min scan; the last 1-min bar's volume ≥ 5 × the average of the 60 before it and its move ≥ 0.05 ATR;
trade AGAINST that minute at its close; stop stopk × ATR (stopk ∈ {0.10, 0.15, 0.25}); no target; exit after max_bars (60 → `shock_fade_rel`,
120 → `shock_fade_rel120`); one trade a day. edge_lab gates (controls 99 %), 2010-07 → 2024-12; holdout once if it passes. The London
fade-to-the-open (K 10) is reported as description only (2019–22 ≈ 0).

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (edge_lab, loaded to the DB): `shock_fade_rel` (60 min) **FAIL at SCREEN** — all 3 stops lose, walk-forward −0.78 pts/trade on 359
(gross +0.22), every era negative, random direction beaten in only 57 % of seeds; `shock_fade_rel120` **FAIL at SCREEN** — −2.86 on 314,
2023–26 −14.41. The event-study drift (−0.025 / −0.040 ATR, averaged over EVERY shock minute, several per morning) does not survive as one
trade a day with a stop: the first shock of the morning is not faded reliably and the noise around a release stops the trade out.
**Closed.** The overnight-shock findings stay descriptive: release shocks are given back more often than not (61 % half back within 5 min),
the very biggest run ~15 min first, London shocks tend to be given back by the 09:30 open.
