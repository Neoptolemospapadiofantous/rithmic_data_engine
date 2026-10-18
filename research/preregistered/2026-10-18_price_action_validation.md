# Pre-registration — validating every price-action test (written 2026-10-18, BEFORE any run)

Founder: "can we validate all test we already did for price action". Scope: the 58 price-action specs in research/ideas
(bb_*, bos_*, ema_*, iw_*, pat_*, rd_*, scalp_*, smc_*, ts_*, wy_*) — swings/BOS/CHoCH, SMC (order blocks, FVGs, premium/discount),
candle patterns, EMA pullbacks/crosses, Wyckoff phases, range mean reversion, scalps.
Checks (pass/fail fixed now):
1. **Code** — build-exp/test_edge_lab: every unit check passes (incl. swings, BOS, LuxAlgo OB/FVG modes, path, Wyckoff vars).
2. **Pipeline** — scripts/edge_lab_validate.sh (full, build-exp binaries): coin-flip rules on real NQ must FAIL, look-ahead traps
   must make no trades, buy-every-day must FAIL at CONTROLS, a planted 8-pt edge on a synthetic market must PASS and 0 pts FAIL;
   the 3 / 5-pt plants give the smallest edge the gates can see; same-input runs give the same output.
3. **Reproducibility** — every one of the 58 specs re-run now: same verdict as its stored report (data/edge_lab/reports); the
   screen's best pts/trade within ± 0.5 (data was appended since some runs — a larger move is reported, not a fail by itself).
4. **Is any "fail" a hidden reverse edge?** — every spec whose best screen variant has t ≤ −2.0 is re-run with its direction
   flipped (dir → −dir); a flipped spec that PASSES the full pipeline is a finding (to be confirmed like any new idea).
5. **Cost** — gross pts/trade (net + cost) of every spec's best screen variant: a spec with gross ≥ +3 that failed only on cost
   is listed (it would matter with cheaper execution); gross < +3 means it has no edge to save.
A price-action family's FAIL is VALIDATED when 1 and 2 pass and the family's specs reproduce (3) with no flipped PASS (4).

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp; the scope list resolved to 55 specs, not 58 — the prefixes match 55 files).
1. **Code: PASS** — test_edge_lab 598/598.
2. **Pipeline: PASS** — scripts/edge_lab_validate.sh full (EDGE_LAB_BIN / SYNTH_BIN = build-exp): 76 / 76 as expected — 12 coin-flip
   rules on real NQ and 12 on synthetic data all FAIL, both look-ahead traps make 0 trades, buy-every-day FAILS at CONTROLS, the
   planted 8-pt edge PASSES (+5.36 measured) and 0 pts FAILS, the same idea twice is identical, 46 / 46 stored PASS ideas reproduce.
   Detection limit: planted 5 pts → FAIL at CONTROLS (+1.79 measured), 3 pts → FAIL at WALK-FORWARD — the gates cannot see an edge
   much below ~5 planted pts/trade. Below that size the edge is also under the 2-pt round-trip cost, so it would not be tradable.
3. **Reproducibility: PASS** — 55 / 55 specs give the same verdict AND the same best pts/trade (difference 0.00) as their stored
   reports. (Table: scratchpad pav/cmp.txt.)
4. **Reverse edge: none** — the 5 specs with t ≤ −2 flipped: bos_ema_gx −0.37, ema_asia −0.71, ema_ldn −0.59, pat_inside +0.68
   (FAIL at CONTROLS), scalp_daily_1005 −0.85. Their gross was −0.84 … +0.17: flipping cannot clear the cost.
5. **Cost: no spec failed only on cost.** Gross ≥ +3 among the fails: bb_or_gap +5.51, wy_range +5.11, ema_cross_vwap__tape_trend
   +4.19, smc_ob_dp +4.12, iw_cont +4.00, pat_flag_casc +3.26, smc_fvg_dp +3.25, pat_structure_casc +3.16 — every one FAILS at
   CONTROLS (random direction / time on the same days earns as much), i.e. the days carry the money, not the pattern.
Stored PASSES among the 55 (6), and what later stopped each: pat_3bar (frozen holdout −0.78, t −3.0), ema_cross_vwap (frozen
setting: no edge before 2019), bos_ema_gap (flat 2010–18, weaker than the gap retest on the same days; 2026-10-18_combine_many
ineligible), bb_on_gap (a variant of the gap retest), pat_double_casc (the cascade plus a pattern), scalp_m1_1000 (the 10:00
momentum scalp — real, 19 trades a year).
**Verdict: the price-action FAILS are VALIDATED** — the code, the gates and the stored results are correct, and no failed idea hides
a reversed edge or an edge lost to cost. The patterns that look profitable earn exactly what trading the same days in a random
direction earns.
