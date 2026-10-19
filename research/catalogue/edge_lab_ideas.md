# B — edge_lab research pipeline inventory

Generated 2026-10-19 from `research/ideas/*.json`, `data/edge_lab/reports/`, `data/edge_lab/results.loaded.csv`, Postgres (`research_ideas`, `research_runs`, `research_tests`; SELECT only) and `data/edge_lab/validation/`. Read-only; nothing was run.

## Sources at a glance

| source | count |
|---|---|
| spec files `research/ideas/*.json` | 442 (426 base ideas + 16 `__tape_*` derived specs); all valid JSON, file name = `id` everywhere |
| verdict reports `data/edge_lab/reports/*.md` (+ matching `.json`) | 436 = 400 base ideas + 36 derived (`__exits` 12, `__meta*` 8, `__tape_*` 16) |
| other report files | 849 csv (`.monthly.csv`, `.variants.csv`, `__meta_page*.trades/features.csv`) |
| `research_ideas` (DB, one row per id) | 436 rows, last_run 2026-10-16 13:44Z → 2026-10-18 17:34Z, sum(runs) 781 |
| `research_runs` (DB, every run) | 801 rows over 436 ids (2026-10-16 11:10Z → 2026-10-18 17:34Z); 800 full + 1 QUICK-LOOK |
| `results.loaded.csv` | 801 run rows (892 physical lines — multi-line quoted fields) = exactly `research_runs` |
| `research_tests` (DB) | 108 rows, 2026-10-11 → 10-15, verdicts edge 12 / weak 30 / no edge 64 / invalid 2 — the OLDER hand-run study ledger (ids like `R7-weak-close-cc-nq`); no id overlaps edge_lab |
| other DB tables present | `research_coverage`, `research_variants` (not inventoried) |

## Counts by verdict stage

Base ideas (400 reports; gates in order: 1 screen → 2 walk-forward → 3 controls → 4 robustness → 5 prop fit):

| verdict | ideas |
|---|---|
| FAIL at SCREEN | 100 |
| FAIL at WALK-FORWARD | 74 |
| FAIL at CONTROLS | 177 |
| FAIL at ROBUSTNESS | 3 |
| FAIL at PROP FIT | 0 (no idea has ever failed only the prop gate) |
| **PASS** | **46** |

Derived reports (36):

| kind | verdicts |
|---|---|
| `__tape_trend` / `__tape_rev` (16) | CONTROLS 9, WALK-FORWARD 6, PASS 1 (`gap_or_retest__tape_trend`) |
| `__exits` (12) | "no exit rule passes" 8; PASS (vwap_stall) 2 (`so_cascade`, `so_cascade_dev`), PASS (vwap) 1 (`cascade_gap_agree`), PASS (open_run) 1 (`gap_or_retest`) |
| `__meta`, `__meta_book`, `__meta_page` (8) | PASS 2 (`gap_or_retest__meta_page`, `so_cascade_dev__meta`); RANDOM SKIP 2; SHUFFLED LABELS 2; ROBUSTNESS 2 |

All 436 combined: SCREEN 100, WALK-FORWARD 80, CONTROLS 186, ROBUSTNESS 5, RANDOM SKIP 2, SHUFFLED LABELS 2, no-exit-rule 8, PASS 53 (49 plain + 4 exit-rule PASS). DB `research_ideas` counts are identical.

## All PASS ideas (sorted by walk-forward net pts/trade)

WF = walk-forward out-of-sample (each month uses the variant best over the previous 12 months), net of 1 pt cost. ES ideas are in ES points. "data" = the history the report was judged on — see problem 1.

| id | verdict | WF n | net pts/trade | PF | t | years + | data | report date (UTC) | runs |
|---|---|---|---|---|---|---|---|---|---|
| rdir_m1_delta | PASS | 68 | 37.29 | 2.22 | 2.91 | 4/4 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 |
| rdir_m1 | PASS | 90 | 34.81 | 2.1 | 3.02 | 4/4 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 |
| weak_day_flight | PASS | 209 | 29.8 | 1.74 | 2.78 | 10/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 |
| dpf10_notstraight_confirm | PASS | 119 | 29.44 | 2.12 | 3.75 | 5/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:51 | 1 |
| pdm_gap_confirm | PASS | 163 | 28.26 | 2.09 | 4.26 | 5/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:16 | 1 |
| pdm_mom_confirm | PASS | 170 | 22.16 | 1.79 | 3.36 | 5/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:16 | 1 |
| if_mom_vband_confirm | PASS | 137 | 19.95 | 1.67 | 2.66 | 5/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:44 | 1 |
| casc_flow | PASS | 398 | 18.5 | 1.48 | 3.24 | 4/4 | 2023-03-01→2026-10-16 | 2026-10-17 15:20 | 1 |
| gap_or_retest__meta_page | PASS | 644 | 15.66 | 1.66 |  |  | none→none | 2026-10-18 12:17 | 1 |
| casc_flow_twin | PASS | 438 | 14.99 | 1.37 | 2.7 | 4/4 | 2023-03-01→2026-10-16 | 2026-10-17 15:20 | 1 |
| gor_flow | PASS | 215 | 14.18 | 1.34 | 1.85 | 3/4 | 2023-03-01→2026-10-16 | 2026-10-17 15:20 | 1 |
| m1_10am_all | PASS | 367 | 13.67 | 1.66 | 3.46 | 11/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:59 | 1 |
| gor_flow_twin | PASS | 252 | 13.57 | 1.31 | 1.85 | 3/4 | 2023-03-01→2026-10-16 | 2026-10-17 15:20 | 1 |
| weak_day_long | PASS | 824 | 13.2 | 1.36 | 2.5 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 7 |
| gap_or_retest_eff | PASS | 686 | 11.19 | 1.61 | 4.36 | 12/14 | 2010-07-01→2024-12-31 | 2026-10-18 14:02 | 1 |
| cascade_gap_agree__exits | PASS (vwap) | 897 | 10.05 | 1.72 |  |  | none→none | 2026-10-16 18:32 | 1 |
| monday_long | PASS | 372 | 9.37 | 1.34 | 1.84 | 9/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:14 | 2 |
| gap_or_retest__exits | PASS (open_run) | 1220 | 9.27 | 1.52 |  |  | none→none | 2026-10-18 14:03 | 5 |
| cascade_gap_agree_g2 | PASS | 733 | 9.1 | 1.47 | 3.66 | 15/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 |
| gap_or_retest | PASS | 1220 | 9.03 | 1.43 | 4.49 | 10/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 |
| cascade_gap_agree_g05 | PASS | 987 | 8.97 | 1.44 | 3.96 | 14/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 |
| gap_or_retest_pd_compl | PASS | 1191 | 8.94 | 1.42 | 4.35 | 10/13 | 2010-07-01→2024-12-31 | 2026-10-18 13:01 | 1 |
| cascade_gap_agree | PASS | 897 | 8.59 | 1.43 | 3.7 | 14/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 |
| gor_or30 | PASS | 1037 | 8.49 | 1.42 | 4.03 | 9/13 | 2010-07-01→2024-12-31 | 2026-10-17 15:20 | 1 |
| so_cascade_dev__meta | PASS | 1134 | 8.22 | 1.3 |  |  | none→none | 2026-10-16 14:05 | 1 |
| grid_weak_day_1555 | PASS | 878 | 8 | 1.22 | 1.96 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 |
| gor_gap | PASS | 1243 | 7.86 | 1.36 | 3.91 | 13/14 | 2010-07-01→2024-12-31 | 2026-10-17 15:20 | 1 |
| cascade_pibs_agree_compl | PASS | 1609 | 7.49 | 1.36 | 4.04 | 14/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 |
| gap_breakout | PASS | 1268 | 7.43 | 1.33 | 3.73 | 11/14 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 |
| gor_or5 | PASS | 1384 | 7.35 | 1.33 | 3.86 | 12/14 | 2010-07-01→2024-12-31 | 2026-10-17 15:20 | 1 |
| bb_on_gap | PASS | 127 | 7.31 | 1.53 | 1.74 | 6/12 | 2010-07-01→2024-12-31 | 2026-10-17 15:25 | 1 |
| ema_cross_vwap | PASS | 956 | 6.79 | 1.23 | 2.54 | 5/8 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 |
| bdh_opt | PASS | 1205 | 6.32 | 1.33 | 3.31 | 13/14 | 2010-07-01→2024-12-31 | 2026-10-18 10:51 | 1 |
| gap_or_retest__tape_trend | PASS | 952 | 5.98 | 1.3 | 2.81 | 9/14 | 2010-07-01→2024-12-31 | 2026-10-17 14:50 | 1 |
| so_cascade__exits | PASS (vwap_stall) | 2322 | 5.82 | 1.41 |  |  | none→none | 2026-10-18 14:03 | 7 |
| so_cascade_dev__exits | PASS (vwap_stall) | 2318 | 5.8 | 1.41 |  |  | none→none | 2026-10-16 14:05 | 1 |
| dir_market | PASS | 752 | 5.63 | 1.24 | 2.13 | 6/14 | 2010-07-01→2024-12-31 | 2026-10-17 13:31 | 1 |
| cascade_orb15_beyond | PASS | 1959 | 5.33 | 1.28 | 3.61 | 9/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 |
| pdx_es_confirm | PASS | 179 | 4.86 | 1.74 | 3.28 | 4/5 | 2019-01-01→2024-12-31 | 2026-10-18 15:01 | 1 |
| so_cascade | PASS | 2322 | 4.8 | 1.22 | 3.27 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 8 |
| cascade_asia_2h | PASS | 304 | 4.32 | 1.5 | 2.37 | 5/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 |
| strong_open | PASS | 1115 | 4.18 | 1.17 | 1.85 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 9 |
| bos_ema_gap | PASS | 1204 | 4.03 | 1.22 | 2.48 | 8/13 | 2010-07-01→2024-12-31 | 2026-10-17 21:20 | 1 |
| union_2r | PASS | 2481 | 4.01 | 1.19 | 3.11 | 12/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:53 | 1 |
| cascade_noon | PASS | 3454 | 3.83 | 1.19 | 3.35 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 15:00 | 1 |
| es_weak_day | PASS | 690 | 3.38 | 1.34 | 2.27 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 15:14 | 1 |
| pat_double_casc | PASS | 1797 | 3.15 | 1.17 | 2.35 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 |
| mom_scan_3_pb | PASS | 2103 | 3.09 | 1.15 | 2.25 | 8/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:40 | 1 |
| pat_3bar | PASS | 1757 | 2.51 | 1.1 | 1.69 | 6/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 |
| scalp_m1_1000 | PASS | 312 | 2.46 | 1.47 | 2.59 | 10/13 | 2010-07-01→2024-12-31 | 2026-10-17 21:52 | 1 |
| es_cascade_gap_agree | PASS | 854 | 1.97 | 1.42 | 3.69 | 14/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 |
| es_gap_or_retest | PASS | 1129 | 1.45 | 1.24 | 2.78 | 7/12 | 2010-07-01→2024-12-31 | 2026-10-17 15:05 | 1 |
| es_cascade | PASS | 2131 | 1.1 | 1.2 | 2.97 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 |

Notes on the PASS list:
- 46 base PASS + `gap_or_retest__tape_trend` + 6 derived PASS. Every base PASS is reproduced by the 2026-10-18 validation run (46/46).
- Walk-forward t below 2 on several PASSes (the WF gate is only n ≥ 30, net > 0, PF ≥ 1.05): `strong_open` 1.85, `gor_flow` / `gor_flow_twin` 1.85, `monday_long` 1.84, `bb_on_gap` 1.74, `pat_3bar` 1.69, `grid_weak_day_1555` 1.96.
- Small samples: `rdir_m1_delta` 68, `rdir_m1` 90 trades (4 years of MBO data); `dpf10_notstraight_confirm` 119, `bb_on_gap` 127, `if_mom_vband_confirm` 137 — all on 5 years or fewer.
- Many PASSes are the same trade family (09:50 cascade / gap-or-retest / weak day): cascade_* 8, gap_or_retest / gor_* 9, weak_day_* 3, es_* mirrors 4. They are not independent edges.

## Consistency problems

1. **238 of the 416 spec-backed reports were judged on data that overlaps the sealed holdout (2025-01 → today).** The spec files carry no `to` field for 199 ideas, and those reports ran on 2010-07-01 → **2026-10-16**; 39 more (MBO/order-flow ideas) ran to 2026-03-19. Only 178 reports are on the in-sample window 2010-07-01 → 2024-12-31. **23 of the 47 spec-backed PASSes** include holdout years: to 2026-10-16 — `so_cascade strong_open weak_day_long weak_day_flight grid_weak_day_1555 cascade_gap_agree cascade_gap_agree_g05 cascade_gap_agree_g2 cascade_pibs_agree_compl cascade_noon cascade_asia_2h ema_cross_vwap pat_3bar pat_double_casc es_cascade es_cascade_gap_agree es_weak_day` and (MBO, 2023-03→) `casc_flow casc_flow_twin gor_flow gor_flow_twin`; to 2026-03-19 — `rdir_m1 rdir_m1_delta`. The 6 `__exits`/`__meta` PASSes record no window (they reuse the parent's trades: `so_cascade*` and `cascade_gap_agree` parents include the holdout, `gap_or_retest` does not). In-sample-only PASSes: 19 on 2010-07→2024-12 plus 5 `*_confirm` ideas on 2019-01→2024-12. A holdout look for those is no longer clean. (Report spec = spec on disk for all 416, so these are not stale copies — the specs themselves are unbounded.)
2. **Specs with no report: 26**, none in the DB either: `etf_base etf_brk_1 etf_brk_5 etf_brk_15 etf_pb_1 etf_pb_5 etf_pb_15 flag1m_gx flag1m_ny gap_fade_m1 gap_fade_m2 gx_ema_follow gx_ema_run gx_ema_run1 if_gap if_mom ldn_pd london_fade nq_es_follow pd_hold pdm_cascade pdm_gap pdm_mom pdx_fade quad_close trend_day_pb`. All written 2026-10-18 14:16 → 10-19 12:00, after the last DB run (10-18 17:34Z) — run outside the saved pipeline (research programs) or not yet run.
3. **Reports with no spec: 20**, all derived (`__exits` ×12, `__meta*` ×8) — expected (spec = parent idea). No base report lacks its spec.
4. **DB vs report files: no disagreement.** For all 436 ids the report's first `Verdict:` line equals `research_ideas.verdict` and the report mtime equals `last_run` to the minute; `research_runs` and `results.loaded.csv` hold the same 801 runs.
5. **Multi-verdict report files:** the four `__meta_page` reports contain 3 `## Verdict` lines each (step blocks); `so_cascade__meta_page` disagrees with itself — FAIL at ROBUSTNESS (first/last) vs FAIL at RANDOM SKIP (second block). The DB keeps ROBUSTNESS.
6. **Ideas whose verdict changed between runs (13; latest = what the report and DB now say):**

| id | runs | history | latest |
|---|---|---|---|
| cnbox_break_of | 3 | 10-16T14:28 FAIL at SCREEN; 10-16T14:29 FAIL at CONTROLS; 10-17T13:12 FAIL at CONTROLS | FAIL at CONTROLS @2026-10-17T13:12Z |
| cnbox_break_price | 3 | 10-16T14:28 FAIL at SCREEN; 10-16T14:29 FAIL at WALK-FORWARD; 10-17T13:12 FAIL at WALK-FORWARD | FAIL at WALK-FORWARD @2026-10-17T13:12Z |
| cnbox_sweep_of | 3 | 10-16T14:28 FAIL at SCREEN; 10-16T14:29 FAIL at CONTROLS; 10-17T13:12 FAIL at CONTROLS | FAIL at CONTROLS @2026-10-17T13:12Z |
| cnbox_sweep_price | 3 | 10-16T14:28 FAIL at SCREEN; 10-16T14:29 FAIL at WALK-FORWARD; 10-17T13:12 FAIL at WALK-FORWARD | FAIL at WALK-FORWARD @2026-10-17T13:12Z |
| dir_trend60 | 2 | 10-17T13:31 FAIL at SCREEN; 10-17T13:31 FAIL at CONTROLS | FAIL at CONTROLS @2026-10-17T13:31Z |
| gap_fade_open | 7 | 10-16T11:13 FAIL at ROBUSTNESS; 10-16T11:15 FAIL at CONTROLS; 10-16T11:17 FAIL at CONTROLS; 10-16T11:18 FAIL at CONTROLS; 10-16T11:19 FAIL at CONTROLS; 10-16T13:42 FAIL at CONTROLS; 10-17T13:14 FAIL at CONTROLS | FAIL at CONTROLS @2026-10-17T13:14Z |
| last_hour | 7 | 10-16T11:13 PASS; 10-16T11:15 FAIL at CONTROLS; 10-16T11:17 FAIL at CONTROLS; 10-16T11:18 PASS; 10-16T11:19 FAIL at ROBUSTNESS; 10-16T13:42 FAIL at ROBUSTNESS; 10-17T13:14 FAIL at ROBUSTNESS | FAIL at ROBUSTNESS @2026-10-17T13:14Z |
| last_hour_all | 6 | 10-16T11:16 PASS; 10-16T11:17 PASS; 10-16T11:18 PASS; 10-16T11:19 FAIL at ROBUSTNESS; 10-16T13:42 FAIL at ROBUSTNESS; 10-17T13:14 FAIL at ROBUSTNESS | FAIL at ROBUSTNESS @2026-10-17T13:14Z |
| quiet_pm_break | 4 | 10-16T13:40 FAIL at SCREEN; 10-16T13:41 FAIL at WALK-FORWARD; 10-16T13:42 FAIL at WALK-FORWARD; 10-17T13:16 FAIL at WALK-FORWARD | FAIL at WALK-FORWARD @2026-10-17T13:16Z |
| so_cascade | 9 | 10-16T11:13 FAIL at CONTROLS; 10-16T11:15 FAIL at CONTROLS; 10-16T11:17 FAIL at CONTROLS; 10-16T11:18 PASS; 10-16T11:19 PASS; 10-16T13:39 PASS; 10-16T13:42 PASS; 10-16T17:36 PASS; 10-17T13:16 PASS | PASS @2026-10-17T13:16Z |
| so_cascade__exits | 10 | 10-16T12:54 FAIL — no exit rule passes; 10-16T12:56 PASS (vwap_stall); 10-16T12:56 PASS (vwap_stall); 10-16T12:56 PASS (vwap_stall); 10-16T12:58 PASS (vwap_stall); 10-17T12:40 PASS (vwap_stall); 10-18T12:29 PASS (vwap_stall); 10-18T12:31 PASS (vwap_stall); 10-18T13:02 PASS (vwap_stall); 10-18T14:03 PASS (vwap_stall) | PASS (vwap_stall) @2026-10-18T14:03Z |
| so_cascade__meta | 5 | 10-16T12:53 PASS; 10-16T12:58 PASS; 10-16T16:43 PASS; 10-17T15:57 FAIL at SHUFFLED LABELS; 10-17T15:57 FAIL at SHUFFLED LABELS | FAIL at SHUFFLED LABELS @2026-10-17T15:57Z |
| strong_open | 11 | 10-16T11:10 QUICK-LOOK; 10-16T11:10 PASS; 10-16T11:11 PASS; 10-16T11:13 PASS; 10-16T11:15 FAIL at CONTROLS; 10-16T11:17 FAIL at CONTROLS; 10-16T11:18 PASS; 10-16T11:19 PASS; 10-16T13:39 PASS; 10-16T13:42 PASS; 10-17T13:16 PASS | PASS @2026-10-17T13:16Z |

   Mostly gate changes on 2026-10-16 (controls/robustness rules tightened) — e.g. `last_hour` / `last_hour_all` went PASS → FAIL at ROBUSTNESS; `so_cascade` went CONTROLS → PASS. **Stale derived PASSes:** `so_cascade_dev__meta` (PASS, last run 10-16 14:05Z) and `so_cascade_dev__exits` were never re-run after the meta gates changed — the parent `so_cascade__meta` re-run on 10-17 now FAILS at SHUFFLED LABELS, so `so_cascade_dev__meta`'s PASS is likely obsolete.
7. **Near-duplicate specs (identical rules/params/data window; differ only in id/name/why and a control or cap setting):**

| pair | difference | verdicts |
|---|---|---|
| `so_cascade` vs `cond_trend_none` | control_pct 0.99→0.998, seeds 400 | **PASS vs FAIL at CONTROLS** — same trade, stricter control flips the verdict |
| `so_cascade__tape_trend` vs `cond_trend_none__tape_trend` | same | CONTROLS / CONTROLS |
| `cascade_vvix_high` vs `cond_trend_vvix` | control_pct | CONTROLS / CONTROLS |
| `cascade_ts_fear` vs `cond_trend_vts` | control_pct | WALK-FORWARD / WALK-FORWARD |
| `ema_pullback_htf` vs `cond_pull_none` | control_pct | CONTROLS / CONTROLS |
| `onbox_sweep_price` vs `cond_rev_none` | control_pct, seeds | WALK-FORWARD / WALK-FORWARD |
| `orb15_break_price` vs `cond_brk_none` | control_pct, seeds | CONTROLS / CONTROLS |
| `mom_scan_1` vs `mom_scan_3` | max_trades 1 vs 3 | CONTROLS / CONTROLS |
| `union_scalp` vs `union_scalp_3` | max_trades 1 vs 3 | SCREEN / SCREEN |
| `gx_ema_run` vs `gx_ema_run1` | max_trades 3 vs 1 | no report |

   No two specs are byte-identical once id/name/why are removed.
8. Every idea counts toward the multiple-testing load: 400 base ideas + 36 derived judged, ~781 full runs; 46 base passes ≈ 11.5 % — well above the ~5 % a null pipeline would produce, but see problem 1 and the family overlap above.

## Validation (`data/edge_lab/validation/summary.md`, 2026-10-18 19:07Z, full mode)

78 checks: **76 as expected, 0 not, 2 informational** (detection-limit rows). Groups: noise on real NQ 12/12 fail as they should; noise on synthetic data 12/12; look-ahead traps 2/2 (0 trades); market drift 1/1 (fails at controls); planted edge 2/2 (strong plant passes, reversed plant fails); determinism 1/1 (`so_cascade` identical twice); reproducibility 46/46 (every stored base PASS reproduces exactly). Detection limit: a weak planted edge (+1.79 pts) fails at CONTROLS, a +0.11 one at WALK-FORWARD — the pipeline cannot see edges below ~2 pts/trade. Not covered: the 7 derived PASSes and `gap_or_retest__tape_trend`.

## Full table — every idea with a verdict (436)

WF n / net / PF / t / years+ from the report JSON `oos` block (derived `__exits`/`__meta` reports have no t or year split). Date = report mtime = DB `last_run` (UTC). Runs = `research_ideas.runs`. trades.csv = `<id>.trades.csv` present.

| id | verdict | stage | WF n | net pts/trade | PF | t | years + | data | date (UTC) | runs | spec | report | trades.csv |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| all_open | FAIL at SCREEN | 0 | 413 | -3.48 | 0.81 | -1.79 | 0/5 | 2010-07-01→2024-12-31 | 2026-10-17 13:36 | 1 | yes | yes | no |
| all_range | FAIL at SCREEN | 0 | 609 | -2.6 | 0.83 | -1.81 | 3/9 | 2010-07-01→2024-12-31 | 2026-10-17 13:36 | 1 | yes | yes | no |
| all_vwap | FAIL at SCREEN | 0 | 736 | -1.36 | 0.93 | -0.85 | 2/6 | 2010-07-01→2024-12-31 | 2026-10-17 13:36 | 1 | yes | yes | no |
| asiabox_break_of | FAIL at SCREEN | 0 | 350 | -7.34 | 0.81 | -1.67 | 1/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:11 | 2 | yes | yes | no |
| asiabox_break_price | FAIL at SCREEN | 0 | 1190 | -1.26 | 0.94 | -0.82 | 4/10 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| asiabox_sweep_of | FAIL at CONTROLS | 2 | 382 | 10.43 | 1.28 | 2.08 | 4/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:11 | 2 | yes | yes | no |
| asiabox_sweep_price | FAIL at WALK-FORWARD | 1 | 1387 | -0.52 | 0.98 | -0.25 | 3/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| asia_open_drive | FAIL at WALK-FORWARD | 1 | 561 | -2.27 | 0.82 | -1.68 | 7/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 7 | yes | yes | no |
| bb_on | FAIL at CONTROLS | 2 | 162 | 2.87 | 1.18 | 0.8 | 4/11 | 2010-07-01→2024-12-31 | 2026-10-17 15:25 | 1 | yes | yes | no |
| bb_on_gap | PASS | 5 | 127 | 7.31 | 1.53 | 1.74 | 6/12 | 2010-07-01→2024-12-31 | 2026-10-17 15:25 | 1 | yes | yes | no |
| bb_or | FAIL at CONTROLS | 2 | 281 | 0.73 | 1.05 | 0.32 | 6/14 | 2010-07-01→2024-12-31 | 2026-10-17 15:25 | 1 | yes | yes | no |
| bb_or_gap | FAIL at CONTROLS | 2 | 103 | 2.25 | 1.15 | 0.55 | 5/13 | 2010-07-01→2024-12-31 | 2026-10-17 15:25 | 1 | yes | yes | no |
| bb_pd | FAIL at SCREEN | 0 | 173 | 0.31 | 1.03 | 0.12 | 5/12 | 2010-07-01→2024-12-31 | 2026-10-17 15:25 | 1 | yes | yes | no |
| bb_pd_gap | FAIL at WALK-FORWARD | 1 | 72 | -0.68 | 0.95 | -0.18 | 3/9 | 2010-07-01→2024-12-31 | 2026-10-17 15:25 | 1 | yes | yes | no |
| bdh_1550 | FAIL at SCREEN | 0 | 30 | -1.43 | 0.7 | -0.73 | 5/8 | 2010-07-01→2024-12-31 | 2026-10-18 10:28 | 1 | yes | yes | no |
| bdh_dm_1450 | FAIL at SCREEN | 0 | 284 | -1.74 | 0.72 | -2.43 | 0/7 | 2010-07-01→2024-12-31 | 2026-10-18 10:28 | 1 | yes | yes | no |
| bdh_fail_reverse | FAIL at SCREEN | 0 | 201 | -5.02 | 0.69 | -1.94 | 1/6 | 2010-07-01→2024-12-31 | 2026-10-18 10:28 | 1 | yes | yes | no |
| bdh_opt | PASS | 5 | 1205 | 6.32 | 1.33 | 3.31 | 13/14 | 2010-07-01→2024-12-31 | 2026-10-18 10:51 | 1 | yes | yes | no |
| bdh_orb15_gap | FAIL at CONTROLS | 2 | 1218 | 6.37 | 1.37 | 3.43 | 10/14 | 2010-07-01→2024-12-31 | 2026-10-18 10:29 | 1 | yes | yes | no |
| bdh_orb15_mid | FAIL at CONTROLS | 2 | 1742 | 2.92 | 1.13 | 1.69 | 6/11 | 2010-07-01→2024-12-31 | 2026-10-18 10:28 | 1 | yes | yes | no |
| bdh_orb15_mid_10 | FAIL at CONTROLS | 2 | 1581 | 3.97 | 1.16 | 1.99 | 7/10 | 2010-07-01→2024-12-31 | 2026-10-18 10:28 | 1 | yes | yes | no |
| bdh_orb5 | FAIL at SCREEN | 0 | 800 | -2.08 | 0.9 | -1.1 | 2/9 | 2010-07-01→2024-12-31 | 2026-10-18 10:28 | 1 | yes | yes | no |
| bdh_orb_retest | FAIL at SCREEN | 0 | 457 | -0.36 | 0.97 | -0.27 | 4/8 | 2010-07-01→2024-12-31 | 2026-10-18 10:28 | 1 | yes | yes | no |
| bdh_prevbar_scalp | FAIL at SCREEN | 0 | 125 | -0.48 | 0.93 | -0.37 | 2/7 | 2010-07-01→2024-12-31 | 2026-10-18 10:28 | 1 | yes | yes | no |
| bos_ema_gap | PASS | 5 | 1204 | 4.03 | 1.22 | 2.48 | 8/13 | 2010-07-01→2024-12-31 | 2026-10-17 21:20 | 1 | yes | yes | no |
| bos_ema_gx | FAIL at SCREEN | 0 | 605 | -1.29 | 0.87 | -1.12 | 1/7 | 2010-07-01→2024-12-31 | 2026-10-17 21:20 | 1 | yes | yes | no |
| bos_ema_ldn | FAIL at SCREEN | 0 | 675 | -0.7 | 0.95 | -0.42 | 3/8 | 2010-07-01→2024-12-31 | 2026-10-17 21:20 | 1 | yes | yes | no |
| bos_ema_ny | FAIL at WALK-FORWARD | 1 | 1964 | 0.78 | 1.04 | 0.66 | 8/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:20 | 1 | yes | yes | no |
| bos_ema_ny15 | FAIL at CONTROLS | 2 | 2077 | 1.01 | 1.06 | 0.89 | 7/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:20 | 1 | yes | yes | no |
| btc_cascade | FAIL at WALK-FORWARD | 1 | 772 | -2.93 | 0.95 | -0.62 | 2/6 | 2020-01-06→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| btc_weekend_monday | FAIL at CONTROLS | 2 | 86 | 8.09 | 1.17 | 0.59 | 3/6 | 2020-01-06→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| btc_weekend_sunday | FAIL at WALK-FORWARD | 1 | 25 | -3.05 | 0.94 | -0.12 | 2/6 | 2020-01-06→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_asia_2h | PASS | 5 | 304 | 4.32 | 1.5 | 2.37 | 5/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_asia_hold | FAIL at CONTROLS | 2 | 377 | 1.02 | 1.06 | 0.44 | 3/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_earn | FAIL at CONTROLS | 2 | 217 | 7.04 | 1.29 | 1.23 | 8/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_ema_filter | FAIL at CONTROLS | 2 | 2112 | 3.67 | 1.17 | 2.45 | 12/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_es_agree | FAIL at CONTROLS | 2 | 2119 | 4.35 | 1.2 | 2.86 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_es_agree_compl | FAIL at CONTROLS | 2 | 613 | 4.94 | 1.23 | 1.76 | 12/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_fail_reverse | FAIL at CONTROLS | 2 | 255 | 5.47 | 1.22 | 1.28 | 6/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_gap_agree | PASS | 5 | 897 | 8.59 | 1.43 | 3.7 | 14/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_gap_agree_compl | FAIL at CONTROLS | 2 | 1274 | 3.35 | 1.14 | 1.6 | 10/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_gap_agree__exits | PASS (vwap) | 5 | 897 | 10.05 | 1.72 |  |  | none→none | 2026-10-16 18:32 | 1 | derived (cascade_gap_agree) | yes | no |
| cascade_gap_agree_g05 | PASS | 5 | 987 | 8.97 | 1.44 | 3.96 | 14/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_gap_agree_g2 | PASS | 5 | 733 | 9.1 | 1.47 | 3.66 | 15/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_gap_agree_g3 | FAIL at CONTROLS | 2 | 590 | 7.47 | 1.39 | 2.75 | 14/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:11 | 2 | yes | yes | no |
| cascade_gx_agree | FAIL at CONTROLS | 2 | 1182 | 6.88 | 1.39 | 3.97 | 13/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| cascade_gx_agree_compl | FAIL at CONTROLS | 2 | 837 | 2.44 | 1.11 | 1.03 | 7/12 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| cascade_ldn_agree | FAIL at CONTROLS | 2 | 1114 | 5.39 | 1.29 | 2.86 | 13/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| cascade_ldn_agree_compl | FAIL at CONTROLS | 2 | 982 | 4.74 | 1.24 | 2.28 | 6/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| cascade_london_2h | FAIL at SCREEN | 0 | 723 | -0.18 | 0.98 | -0.21 | 5/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_london_hold | FAIL at SCREEN | 0 | 659 | -1.29 | 0.91 | -0.95 | 4/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_noearn | FAIL at CONTROLS | 2 | 2180 | 4.1 | 1.2 | 2.82 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_noon | PASS | 5 | 3454 | 3.83 | 1.19 | 3.35 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 15:00 | 1 | yes | yes | no |
| cascade_ny_2h | FAIL at CONTROLS | 2 | 1864 | 2.17 | 1.12 | 1.68 | 7/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_on_break | FAIL at CONTROLS | 2 | 2022 | 4.58 | 1.22 | 2.98 | 12/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_on_break_compl | FAIL at CONTROLS | 2 | 623 | 4.96 | 1.19 | 1.54 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_orb15_beyond | PASS | 5 | 1959 | 5.33 | 1.28 | 3.61 | 9/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| cascade_orb15_beyond_compl | FAIL at CONTROLS | 2 | 514 | 2.39 | 1.13 | 0.96 | 6/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| cascade_orb15_narrow | FAIL at CONTROLS | 2 | 1097 | 4.79 | 1.24 | 2.49 | 11/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:07 | 1 | yes | yes | no |
| cascade_orb15_narrow_compl | FAIL at CONTROLS | 2 | 939 | 4.17 | 1.2 | 1.88 | 7/13 | 2010-07-01→2024-12-31 | 2026-10-17 21:07 | 1 | yes | yes | no |
| cascade_orb5_beyond | FAIL at CONTROLS | 2 | 2135 | 5.04 | 1.27 | 3.66 | 10/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| cascade_orb5_beyond_compl | FAIL at WALK-FORWARD | 1 | 79 | -10.95 | 0.51 | -2.18 | 3/12 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| cascade_pd | FAIL at WALK-FORWARD | 1 | 170 | -0.58 | 0.97 | -0.12 | 4/14 | 2010-07-01→2024-12-31 | 2026-10-18 13:01 | 1 | yes | yes | no |
| cascade_pd_compl | FAIL at CONTROLS | 2 | 2143 | 4.45 | 1.23 | 3.28 | 10/14 | 2010-07-01→2024-12-31 | 2026-10-18 13:01 | 1 | yes | yes | no |
| cascade_pibs_agree | FAIL at WALK-FORWARD | 1 | 978 | 1.2 | 1.05 | 0.51 | 7/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_pibs_agree_compl | PASS | 5 | 1609 | 7.49 | 1.36 | 4.04 | 14/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_pyramid | FAIL at CONTROLS | 2 | 2355 | 4.62 | 1.17 | 2.39 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_rates_move | FAIL at CONTROLS | 2 | 371 | 5.56 | 1.18 | 1.2 | 5/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_rates_move_compl | FAIL at CONTROLS | 2 | 1964 | 4.91 | 1.24 | 3.18 | 13/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_reentry | FAIL at CONTROLS | 2 | 3511 | 3.62 | 1.17 | 3.11 | 13/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_rvol | FAIL at CONTROLS | 2 | 921 | 4.55 | 1.21 | 2.02 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_rvol_compl | FAIL at CONTROLS | 2 | 1600 | 4.92 | 1.22 | 2.66 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_skip_opex | FAIL at CONTROLS | 2 | 2247 | 4.5 | 1.2 | 2.99 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_skip_opex_compl | FAIL at CONTROLS | 2 | 41 | 16.36 | 1.71 | 1.22 | 5/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_t0945 | FAIL at CONTROLS | 2 | 2456 | 3.57 | 1.17 | 2.61 | 12/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_t0955 | FAIL at CONTROLS | 2 | 2351 | 4.42 | 1.2 | 3 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_t1000 | FAIL at CONTROLS | 2 | 2465 | 3.98 | 1.18 | 2.76 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_tokyo_2h | FAIL at SCREEN | 0 | 492 | -0.74 | 0.92 | -0.62 | 3/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_tokyo_hold | FAIL at WALK-FORWARD | 1 | 626 | 0.21 | 1.02 | 0.16 | 6/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_trail | FAIL at CONTROLS | 2 | 2311 | 2.56 | 1.14 | 2.05 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_ts_calm | FAIL at CONTROLS | 2 | 2085 | 5.13 | 1.24 | 3.4 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_ts_fear | FAIL at WALK-FORWARD | 1 | 144 | -1.75 | 0.95 | -0.22 | 6/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_vvix_high | FAIL at CONTROLS | 2 | 816 | 9.35 | 1.32 | 2.99 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cascade_vvix_high_compl | FAIL at CONTROLS | 2 | 1505 | 2.51 | 1.15 | 1.74 | 10/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| casc_book_confirm | FAIL at WALK-FORWARD | 1 | 50 | 1.58 | 1.03 | 0.1 | 1/2 | 2024-09-01→2026-03-19 | 2026-10-18 17:34 | 1 | yes | yes | no |
| casc_flow | PASS | 5 | 398 | 18.5 | 1.48 | 3.24 | 4/4 | 2023-03-01→2026-10-16 | 2026-10-17 15:20 | 1 | yes | yes | no |
| casc_flow_twin | PASS | 5 | 438 | 14.99 | 1.37 | 2.7 | 4/4 | 2023-03-01→2026-10-16 | 2026-10-17 15:20 | 1 | yes | yes | no |
| cnbox_break_of | FAIL at CONTROLS | 2 | 56 | 7.09 | 1.3 | 0.8 | 1/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cnbox_break_price | FAIL at WALK-FORWARD | 1 | 344 | 0.86 | 1.04 | 0.27 | 6/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cnbox_sweep_of | FAIL at CONTROLS | 2 | 48 | 6.6 | 1.26 | 0.67 | 3/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cnbox_sweep_price | FAIL at WALK-FORWARD | 1 | 409 | 0.19 | 1.01 | 0.08 | 8/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| compression_breakout | FAIL at CONTROLS | 2 | 532 | 4.89 | 1.2 | 1.48 | 13/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 3 | yes | yes | no |
| cond_brk_nofomc | FAIL at CONTROLS | 2 | 1398 | 1.8 | 1.05 | 0.83 | 4/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cond_brk_none | FAIL at CONTROLS | 2 | 1393 | 2.39 | 1.07 | 1.06 | 5/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:12 | 2 | yes | yes | no |
| cond_brk_none__tape_trend | FAIL at CONTROLS | 2 | 1868 | 3.32 | 1.15 | 2.25 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 14:49 | 1 | yes | yes | no |
| cond_brk_trend50 | FAIL at CONTROLS | 2 | 759 | 5.65 | 1.19 | 1.93 | 5/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_brk_vix20 | FAIL at WALK-FORWARD | 1 | 603 | -0.03 | 1 | -0.01 | 5/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_brk_vixlow | FAIL at CONTROLS | 2 | 760 | 2.19 | 1.09 | 0.97 | 4/10 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_brk_vts | FAIL at WALK-FORWARD | 1 | 110 | -4.96 | 0.86 | -0.62 | 2/8 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_brk_vvix | FAIL at CONTROLS | 2 | 908 | 4.18 | 1.13 | 1.56 | 9/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_pull_nofomc | FAIL at CONTROLS | 2 | 1690 | 1.58 | 1.06 | 0.87 | 6/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_pull_none | FAIL at CONTROLS | 2 | 1669 | 2.12 | 1.08 | 1.13 | 5/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_pull_none__tape_trend | FAIL at WALK-FORWARD | 1 | 1984 | 0.11 | 1 | 0.08 | 5/15 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| cond_pull_trend50 | FAIL at CONTROLS | 2 | 860 | 1.37 | 1.05 | 0.53 | 6/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_pull_vix20 | FAIL at WALK-FORWARD | 1 | 648 | 1.1 | 1.03 | 0.32 | 4/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_pull_vixlow | FAIL at CONTROLS | 2 | 897 | 3.39 | 1.14 | 1.53 | 6/10 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_pull_vts | FAIL at WALK-FORWARD | 1 | 163 | 0.25 | 1.01 | 0.03 | 7/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_pull_vvix | FAIL at CONTROLS | 2 | 908 | 2 | 1.07 | 0.74 | 6/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_rev_nofomc | FAIL at WALK-FORWARD | 1 | 1004 | 0.01 | 1 | 0 | 6/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_rev_none | FAIL at WALK-FORWARD | 1 | 1015 | -0.61 | 0.98 | -0.3 | 5/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_rev_none__tape_rev | FAIL at WALK-FORWARD | 1 | 1528 | -0.32 | 0.98 | -0.22 | 5/15 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| cond_rev_trend50 | FAIL at SCREEN | 0 | 626 | 0.58 | 1.03 | 0.29 | 4/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_rev_vix20 | FAIL at CONTROLS | 2 | 470 | 2.42 | 1.08 | 0.72 | 10/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_rev_vixlow | FAIL at SCREEN | 0 | 609 | -0.99 | 0.95 | -0.44 | 4/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_rev_vts | FAIL at WALK-FORWARD | 1 | 90 | 0.16 | 1.01 | 0.02 | 7/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_rev_vvix | FAIL at CONTROLS | 2 | 607 | 2.2 | 1.07 | 0.72 | 8/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_trend_nofomc | FAIL at CONTROLS | 2 | 2295 | 4.64 | 1.21 | 3.13 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_trend_none | FAIL at CONTROLS | 2 | 2322 | 4.8 | 1.22 | 3.27 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_trend_none__tape_trend | FAIL at CONTROLS | 2 | 1905 | 3.04 | 1.13 | 1.88 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| cond_trend_trend50 | FAIL at CONTROLS | 2 | 1165 | 5.07 | 1.23 | 2.35 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_trend_vix20 | FAIL at CONTROLS | 2 | 623 | 3.52 | 1.1 | 0.97 | 7/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_trend_vixlow | FAIL at CONTROLS | 2 | 1671 | 5.34 | 1.32 | 3.69 | 11/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_trend_vts | FAIL at WALK-FORWARD | 1 | 144 | -1.75 | 0.95 | -0.22 | 6/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cond_trend_vvix | FAIL at CONTROLS | 2 | 816 | 9.35 | 1.32 | 2.99 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cpi_fade | FAIL at WALK-FORWARD | 1 | 81 | -4.58 | 0.88 | -0.42 | 4/12 | 2010-07-01→2024-12-31 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cta_cross_0930 | FAIL at SCREEN | 0 | 174 | -1.11 | 0.97 | -0.15 | 4/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| cta_cross_1800 | FAIL at CONTROLS | 2 | 214 | 7.37 | 1.21 | 0.9 | 5/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:13 | 2 | yes | yes | no |
| daily_1005 | FAIL at CONTROLS | 2 | 1401 | 4.52 | 1.14 | 1.96 | 6/11 | 2010-07-01→2024-12-31 | 2026-10-17 21:49 | 1 | yes | yes | no |
| data_drift | FAIL at CONTROLS | 2 | 299 | 1.14 | 1.1 | 0.6 | 5/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| dir_breakout | FAIL at CONTROLS | 2 | 549 | 3.67 | 1.16 | 1.29 | 6/12 | 2010-07-01→2024-12-31 | 2026-10-17 13:31 | 1 | yes | yes | no |
| dir_breakout__tape_trend | FAIL at CONTROLS | 2 | 568 | 2.28 | 1.1 | 0.88 | 8/14 | 2010-07-01→2024-12-31 | 2026-10-17 14:50 | 1 | yes | yes | no |
| dir_ema14 | FAIL at CONTROLS | 2 | 595 | 5.36 | 1.23 | 1.85 | 9/12 | 2010-07-01→2024-12-31 | 2026-10-17 15:56 | 1 | yes | yes | no |
| dir_ema_pb | FAIL at CONTROLS | 2 | 401 | 4.42 | 1.16 | 1.16 | 3/9 | 2010-07-01→2024-12-31 | 2026-10-17 13:31 | 1 | yes | yes | no |
| dir_fib38 | FAIL at SCREEN | 0 | 387 | -2.86 | 0.87 | -0.98 | 3/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:31 | 1 | yes | yes | no |
| dir_market | PASS | 5 | 752 | 5.63 | 1.24 | 2.13 | 6/14 | 2010-07-01→2024-12-31 | 2026-10-17 13:31 | 1 | yes | yes | no |
| dir_market__tape_trend | FAIL at CONTROLS | 2 | 625 | 1.71 | 1.07 | 0.66 | 6/14 | 2010-07-01→2024-12-31 | 2026-10-17 14:50 | 1 | yes | yes | no |
| dir_or_retest | FAIL at CONTROLS | 2 | 417 | 5.43 | 1.26 | 1.67 | 6/12 | 2010-07-01→2024-12-31 | 2026-10-17 13:31 | 1 | yes | yes | no |
| dir_trend60 | FAIL at CONTROLS | 2 | 765 | 4.45 | 1.22 | 2.03 | 8/14 | 2010-07-01→2024-12-31 | 2026-10-17 13:31 | 2 | yes | yes | no |
| dir_vwap_pb | FAIL at CONTROLS | 2 | 398 | 4.95 | 1.2 | 1.35 | 6/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:31 | 1 | yes | yes | no |
| dpa_10 | FAIL at SCREEN | 0 | 358 | -1.04 | 0.96 | -0.34 | 3/9 | 2010-07-01→2024-12-31 | 2026-10-18 14:51 | 1 | yes | yes | no |
| dpa_20 | FAIL at WALK-FORWARD | 1 | 403 | 0.56 | 1.02 | 0.17 | 4/8 | 2010-07-01→2024-12-31 | 2026-10-18 14:51 | 1 | yes | yes | no |
| dpa_5 | FAIL at SCREEN | 0 | 304 | -2.63 | 0.85 | -1.07 | 1/12 | 2010-07-01→2024-12-31 | 2026-10-18 14:51 | 1 | yes | yes | no |
| dp_base | FAIL at CONTROLS | 2 | 1599 | 3.72 | 1.2 | 2.71 | 10/14 | 2010-07-01→2024-12-31 | 2026-10-18 13:37 | 1 | yes | yes | no |
| dpf10_notstraight_confirm | PASS | 5 | 119 | 29.44 | 2.12 | 3.75 | 5/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:51 | 1 | yes | yes | no |
| dpf10_straight_compl | FAIL at WALK-FORWARD | 1 | 17 | 14.46 | 1.56 | 0.79 | 3/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:51 | 1 | yes | yes | no |
| dp_phase | FAIL at CONTROLS | 2 | 475 | 1.43 | 1.08 | 0.59 | 7/14 | 2010-07-01→2024-12-31 | 2026-10-18 13:53 | 1 | yes | yes | no |
| dp_phase_not | FAIL at CONTROLS | 2 | 548 | 2.2 | 1.15 | 1.13 | 10/14 | 2010-07-01→2024-12-31 | 2026-10-18 13:53 | 1 | yes | yes | no |
| ema_asia | FAIL at SCREEN | 0 | 475 | 0.38 | 1.03 | 0.29 | 2/5 | 2010-07-01→2024-12-31 | 2026-10-17 14:11 | 1 | yes | yes | no |
| ema_cross_5m | FAIL at SCREEN | 0 | 2987 | -1.72 | 0.92 | -1.74 | 1/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 7 | yes | yes | no |
| ema_cross_vwap | PASS | 5 | 956 | 6.79 | 1.23 | 2.54 | 5/8 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| ema_cross_vwap__tape_trend | FAIL at CONTROLS | 2 | 1201 | 3.39 | 1.18 | 1.93 | 7/15 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| ema_ldn | FAIL at SCREEN | 0 | 277 | -5.31 | 0.65 | -3.3 | 0/5 | 2010-07-01→2024-12-31 | 2026-10-17 14:11 | 1 | yes | yes | no |
| ema_ny | FAIL at SCREEN | 0 | 934 | 0.08 | 1 | 0.05 | 5/9 | 2010-07-01→2024-12-31 | 2026-10-17 14:11 | 1 | yes | yes | no |
| ema_pullback_60 | FAIL at WALK-FORWARD | 1 | 3973 | 1 | 1.04 | 1.05 | 5/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 7 | yes | yes | no |
| ema_pullback_htf | FAIL at CONTROLS | 2 | 1669 | 2.12 | 1.08 | 1.13 | 5/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| eqbox_break_of | FAIL at CONTROLS | 2 | 321 | 5.92 | 1.17 | 1.19 | 3/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| eqbox_break_price | FAIL at WALK-FORWARD | 1 | 1522 | 0.86 | 1.03 | 0.49 | 5/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| eqbox_sweep_of | FAIL at SCREEN | 0 | 91 | -11.89 | 0.63 | -1.89 | 2/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| eqbox_sweep_price | FAIL at SCREEN | 0 | 417 | -4.81 | 0.8 | -2.05 | 0/8 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| es_cascade | PASS | 5 | 2131 | 1.1 | 1.2 | 2.97 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| es_cascade__exits | FAIL — no exit rule passes | 0 | 2131 | 1.1 | 1.2 |  |  | none→none | 2026-10-16 13:44 | 1 | derived (es_cascade) | yes | no |
| es_cascade_gap_agree | PASS | 5 | 854 | 1.97 | 1.42 | 3.69 | 14/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| es_gap_or_retest | PASS | 5 | 1129 | 1.45 | 1.24 | 2.78 | 7/12 | 2010-07-01→2024-12-31 | 2026-10-17 15:05 | 1 | yes | yes | no |
| es_weak_day | PASS | 5 | 690 | 3.38 | 1.34 | 2.27 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 15:14 | 1 | yes | yes | no |
| eth_cascade | FAIL at WALK-FORWARD | 1 | 837 | -3.11 | 0.95 | -0.61 | 3/6 | 2020-01-06→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| exhaustion_fade | FAIL at CONTROLS | 2 | 125 | 4.49 | 1.24 | 0.88 | 6/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| exhaustion_fade__tape_rev | FAIL at WALK-FORWARD | 1 | 114 | -1.87 | 0.9 | -0.41 | 5/15 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| fomc_fade | FAIL at WALK-FORWARD | 1 | 28 | 9.01 | 1.19 | 0.32 | 4/9 | 2010-07-01→2024-12-31 | 2026-10-17 13:14 | 2 | yes | yes | no |
| fomc_predrift | FAIL at SCREEN | 0 | 52 | -1.7 | 0.92 | -0.22 | 5/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 3 | yes | yes | no |
| friday_derisk | FAIL at SCREEN | 0 | 300 | -2.69 | 0.79 | -1.11 | 4/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 3 | yes | yes | no |
| gap_breakout | PASS | 5 | 1268 | 7.43 | 1.33 | 3.73 | 11/14 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| gap_ema_pb | FAIL at SCREEN | 0 | 1115 | -0.48 | 0.98 | -0.28 | 4/10 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| gap_fade_open | FAIL at CONTROLS | 2 | 1510 | 2.41 | 1.09 | 1.16 | 8/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 7 | yes | yes | no |
| gap_fade_open__tape_rev | FAIL at CONTROLS | 2 | 991 | 2.94 | 1.1 | 1 | 5/16 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| gap_fail | FAIL at CONTROLS | 2 | 250 | 1.16 | 1.05 | 0.3 | 5/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 7 | yes | yes | no |
| gap_fib38 | FAIL at SCREEN | 0 | 975 | -1.2 | 0.95 | -0.61 | 5/12 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| gap_market | FAIL at CONTROLS | 2 | 1215 | 2.32 | 1.09 | 1.18 | 6/10 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| gap_or_retest | PASS | 5 | 1220 | 9.03 | 1.43 | 4.49 | 10/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| gap_or_retest_eff | PASS | 5 | 686 | 11.19 | 1.61 | 4.36 | 12/14 | 2010-07-01→2024-12-31 | 2026-10-18 14:02 | 1 | yes | yes | no |
| gap_or_retest_eff_not | FAIL at CONTROLS | 2 | 600 | 7.77 | 1.3 | 2.45 | 7/10 | 2010-07-01→2024-12-31 | 2026-10-18 14:02 | 1 | yes | yes | no |
| gap_or_retest__exits | PASS (open_run) | 5 | 1220 | 9.27 | 1.52 |  |  | none→none | 2026-10-18 14:03 | 5 | derived (gap_or_retest) | yes | no |
| gap_or_retest__meta_page | PASS | 5 | 644 | 15.66 | 1.66 |  |  | none→none | 2026-10-18 12:17 | 1 | derived (gap_or_retest) | yes | yes |
| gap_or_retest_pd | FAIL at CONTROLS | 2 | 113 | 4.86 | 1.23 | 0.85 | 3/10 | 2010-07-01→2024-12-31 | 2026-10-18 13:01 | 1 | yes | yes | no |
| gap_or_retest_pd_compl | PASS | 5 | 1191 | 8.94 | 1.42 | 4.35 | 10/13 | 2010-07-01→2024-12-31 | 2026-10-18 13:01 | 1 | yes | yes | no |
| gap_or_retest__tape_trend | PASS | 5 | 952 | 5.98 | 1.3 | 2.81 | 9/14 | 2010-07-01→2024-12-31 | 2026-10-17 14:50 | 1 | yes | yes | no |
| gap_trend60 | FAIL at CONTROLS | 2 | 1552 | 3.73 | 1.16 | 2.11 | 9/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| gap_vwap_pb | FAIL at CONTROLS | 2 | 1428 | 1.61 | 1.06 | 0.91 | 7/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| gor_flow | PASS | 5 | 215 | 14.18 | 1.34 | 1.85 | 3/4 | 2023-03-01→2026-10-16 | 2026-10-17 15:20 | 1 | yes | yes | no |
| gor_flow_twin | PASS | 5 | 252 | 13.57 | 1.31 | 1.85 | 3/4 | 2023-03-01→2026-10-16 | 2026-10-17 15:20 | 1 | yes | yes | no |
| gor_gap | PASS | 5 | 1243 | 7.86 | 1.36 | 3.91 | 13/14 | 2010-07-01→2024-12-31 | 2026-10-17 15:20 | 1 | yes | yes | no |
| gor_or30 | PASS | 5 | 1037 | 8.49 | 1.42 | 4.03 | 9/13 | 2010-07-01→2024-12-31 | 2026-10-17 15:20 | 1 | yes | yes | no |
| gor_or5 | PASS | 5 | 1384 | 7.35 | 1.33 | 3.86 | 12/14 | 2010-07-01→2024-12-31 | 2026-10-17 15:20 | 1 | yes | yes | no |
| grid_cascade | FAIL at CONTROLS | 2 | 2683 | 4.08 | 1.19 | 2.8 | 12/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| grid_weak_day_1200 | FAIL at CONTROLS | 2 | 847 | 1.62 | 1.06 | 0.56 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| grid_weak_day_1400 | FAIL at CONTROLS | 2 | 850 | 2.74 | 1.09 | 0.71 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| grid_weak_day_1555 | PASS | 5 | 878 | 8 | 1.22 | 1.96 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| gx_orb15_break | FAIL at SCREEN | 0 | 194 | -1.38 | 0.85 | -0.84 | 0/2 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| gx_orb15_sweep | FAIL at SCREEN | 0 | 639 | -0.95 | 0.85 | -1.37 | 1/6 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| gx_orb5_break | FAIL at SCREEN | 0 | 348 | -1.46 | 0.87 | -0.92 | 0/2 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| half_hour_last | FAIL at SCREEN | 0 | 527 | -0.89 | 0.94 | -0.52 | 1/6 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 6 | yes | yes | no |
| highvix_vwap_fade | FAIL at WALK-FORWARD | 1 | 333 | -5.32 | 0.82 | -1.47 | 2/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| if_mom_vband_compl | FAIL at WALK-FORWARD | 1 | 18 | 20.12 | 1.64 | 0.94 | 2/3 | 2019-01-01→2024-12-31 | 2026-10-18 14:44 | 1 | yes | yes | no |
| if_mom_vband_confirm | PASS | 5 | 137 | 19.95 | 1.67 | 2.66 | 5/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:44 | 1 | yes | yes | no |
| iw_cont | FAIL at CONTROLS | 2 | 47 | 1.98 | 1.12 | 0.29 | 6/11 | 2010-07-01→2024-12-31 | 2026-10-18 14:02 | 1 | yes | yes | no |
| iw_rev | FAIL at SCREEN | 0 | 26 | 8.7 | 2.09 | 1.34 | 5/6 | 2010-07-01→2024-12-31 | 2026-10-18 14:02 | 1 | yes | yes | no |
| last_hour | FAIL at ROBUSTNESS | 3 | 1208 | 2.88 | 1.2 | 2.05 | 6/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 7 | yes | yes | no |
| last_hour_all | FAIL at ROBUSTNESS | 3 | 1420 | 1.86 | 1.12 | 1.45 | 4/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 6 | yes | yes | no |
| late_so | FAIL at CONTROLS | 2 | 1218 | 3.15 | 1.15 | 1.6 | 8/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 7 | yes | yes | no |
| ldnbox_break_of | FAIL at WALK-FORWARD | 1 | 317 | -3.08 | 0.92 | -0.62 | 2/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| ldnbox_break_price | FAIL at SCREEN | 0 | 1337 | -0.79 | 0.97 | -0.45 | 2/10 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| ldnbox_sweep_of | FAIL at SCREEN | 0 | 98 | -8.3 | 0.8 | -0.98 | 0/3 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| ldnbox_sweep_price | FAIL at SCREEN | 0 | 1403 | 0.97 | 1.05 | 0.75 | 6/10 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| ldn_orb15_break | FAIL at SCREEN | 0 | 892 | -0.91 | 0.94 | -0.79 | 4/6 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| ldn_orb15_sweep | FAIL at WALK-FORWARD | 1 | 1112 | -0.99 | 0.94 | -0.85 | 2/9 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| ldn_orb5_break | FAIL at SCREEN | 0 | 554 | -0.71 | 0.96 | -0.45 | 2/4 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| ldr_es | FAIL at CONTROLS | 2 | 1021 | 2.85 | 1.14 | 1.47 | 9/13 | 2010-07-01→2024-12-31 | 2026-10-17 15:20 | 1 | yes | yes | no |
| london_open_drive | FAIL at SCREEN | 0 | 545 | -3.09 | 0.76 | -2.22 | 2/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 7 | yes | yes | no |
| m1_10am_all | PASS | 5 | 367 | 13.67 | 1.66 | 3.46 | 11/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:59 | 1 | yes | yes | no |
| m1dir_bos_ema | FAIL at CONTROLS | 2 | 215 | 10.81 | 1.64 | 2.56 | 11/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:23 | 1 | yes | yes | no |
| m1dir_bos_ema_gap | FAIL at SCREEN | 0 | 53 | 20.22 | 1.97 | 1.99 | 8/8 | 2010-07-01→2024-12-31 | 2026-10-17 21:23 | 1 | yes | yes | no |
| m1dir_choch_ema | FAIL at SCREEN | 0 | 53 | 8.15 | 1.4 | 0.91 | 6/10 | 2010-07-01→2024-12-31 | 2026-10-17 21:23 | 1 | yes | yes | no |
| m1dir_ema | FAIL at CONTROLS | 2 | 318 | 4.71 | 1.26 | 1.43 | 8/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:23 | 1 | yes | yes | no |
| midday_break | FAIL at CONTROLS | 2 | 934 | 3.58 | 1.16 | 1.45 | 7/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 7 | yes | yes | no |
| mobox_break_of | FAIL at SCREEN | 0 | 27 | 18.3 | 1.7 | 1.16 | 4/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| mobox_break_price | FAIL at CONTROLS | 2 | 247 | 3.97 | 1.25 | 1.19 | 7/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| mobox_sweep_of | FAIL at SCREEN | 0 | 2 | 156.91 | 99 | 4.72 | 2/2 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| mobox_sweep_price | FAIL at CONTROLS | 2 | 175 | 2.84 | 1.15 | 0.72 | 6/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| mom_scan_1 | FAIL at CONTROLS | 2 | 2567 | 2.82 | 1.14 | 2.27 | 11/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:40 | 1 | yes | yes | no |
| mom_scan_3 | FAIL at CONTROLS | 2 | 3820 | 2.32 | 1.11 | 2.3 | 7/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:40 | 1 | yes | yes | no |
| mom_scan_3_ema | FAIL at CONTROLS | 2 | 2557 | 2.09 | 1.09 | 1.6 | 6/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:43 | 1 | yes | yes | no |
| mom_scan_3_ema14 | FAIL at CONTROLS | 2 | 2255 | 1.61 | 1.08 | 1.23 | 9/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:45 | 1 | yes | yes | no |
| mom_scan_3_pb | PASS | 5 | 2103 | 3.09 | 1.15 | 2.25 | 8/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:40 | 1 | yes | yes | no |
| mom_scan_3_pb__exits | FAIL — no exit rule passes | 0 | 2103 | 3.09 | 1.15 |  |  | none→none | 2026-10-18 12:31 | 2 | derived (mom_scan_3_pb) | yes | no |
| mom_scan_3_pb__meta_page | FAIL at RANDOM SKIP | 1 | 908 | 4.77 | 1.2 |  |  | none→none | 2026-10-18 12:17 | 1 | derived (mom_scan_3_pb) | yes | yes |
| monday_fade | FAIL at CONTROLS | 2 | 143 | 6.12 | 1.17 | 0.57 | 4/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 7 | yes | yes | no |
| monday_long | PASS | 5 | 372 | 9.37 | 1.34 | 1.84 | 9/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:14 | 2 | yes | yes | no |
| monday_long_1100 | FAIL at CONTROLS | 2 | 362 | 7.48 | 1.36 | 1.8 | 7/12 | 2010-07-01→2024-12-31 | 2026-10-17 13:14 | 2 | yes | yes | no |
| month_end_rebalance | FAIL at CONTROLS | 2 | 173 | 8.03 | 1.26 | 0.96 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 3 | yes | yes | no |
| nd_open | FAIL at SCREEN | 0 | 333 | -6.52 | 0.69 | -2.76 | 1/8 | 2010-07-01→2024-12-31 | 2026-10-17 13:36 | 1 | yes | yes | no |
| nd_range | FAIL at SCREEN | 0 | 554 | -1.96 | 0.84 | -1.59 | 3/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:36 | 1 | yes | yes | no |
| nd_vwap | FAIL at SCREEN | 0 | 384 | -4.27 | 0.76 | -2.12 | 1/9 | 2010-07-01→2024-12-31 | 2026-10-17 13:36 | 1 | yes | yes | no |
| news_fade | FAIL at CONTROLS | 2 | 171 | 6.68 | 1.16 | 0.66 | 5/12 | 2010-07-01→2024-12-31 | 2026-10-17 13:14 | 2 | yes | yes | no |
| npocbox_break_of | FAIL at WALK-FORWARD | 1 | 130 | -5.24 | 0.84 | -0.88 | 1/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| npocbox_break_price | FAIL at WALK-FORWARD | 1 | 896 | -1.46 | 0.94 | -0.74 | 5/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| npocbox_sweep_of | FAIL at WALK-FORWARD | 1 | 55 | 1.2 | 1.04 | 0.13 | 2/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| npocbox_sweep_price | FAIL at SCREEN | 0 | 501 | -1.87 | 0.94 | -0.61 | 4/10 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| nq_es_diverge | FAIL at SCREEN | 0 | 343 | -2.44 | 0.83 | -1.31 | 2/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 3 | yes | yes | no |
| onbox_break_of | FAIL at SCREEN | 0 | 189 | -2.55 | 0.94 | -0.38 | 1/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| onbox_break_price | FAIL at WALK-FORWARD | 1 | 1732 | 0.53 | 1.02 | 0.32 | 5/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| onbox_sweep_of | FAIL at CONTROLS | 2 | 122 | 3.31 | 1.11 | 0.52 | 2/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| onbox_sweep_price | FAIL at WALK-FORWARD | 1 | 1015 | -0.61 | 0.98 | -0.3 | 5/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| on_overshoot_fade | FAIL at WALK-FORWARD | 1 | 338 | 0.28 | 1.01 | 0.07 | 7/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| open_momentum_all | FAIL at WALK-FORWARD | 1 | 2701 | 1.22 | 1.04 | 0.6 | 6/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 6 | yes | yes | no |
| opex_fade | FAIL at SCREEN | 0 | 26 | -2.48 | 0.87 | -0.3 | 4/10 | 2010-07-01→2026-10-16 | 2026-10-17 13:14 | 2 | yes | yes | no |
| opex_friday_short | FAIL at CONTROLS | 2 | 77 | 23.73 | 2.13 | 2.1 | 9/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:14 | 2 | yes | yes | no |
| orb | FAIL at CONTROLS | 2 | 2775 | 1.49 | 1.06 | 1.01 | 7/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 7 | yes | yes | no |
| orb15 | FAIL at CONTROLS | 2 | 2136 | 2.48 | 1.08 | 1.27 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb15_break_of | FAIL at CONTROLS | 2 | 434 | 2.77 | 1.09 | 0.77 | 4/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:14 | 2 | yes | yes | no |
| orb15_break_price | FAIL at CONTROLS | 2 | 1393 | 2.39 | 1.07 | 1.06 | 5/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb15_of | FAIL at CONTROLS | 2 | 396 | 4.09 | 1.1 | 0.77 | 2/4 | 2023-03-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb15_of_dev__exits | FAIL — no exit rule passes | 0 | 396 | 4.09 | 1.1 |  |  | none→none | 2026-10-16 14:05 | 1 | derived (orb15_of_dev) | yes | no |
| orb15_rangeflow | FAIL at CONTROLS | 2 | 350 | 6.37 | 1.16 | 1.15 | 3/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb15_sweep_of | FAIL at SCREEN | 0 | 43 | 3.57 | 1.11 | 0.28 | 1/2 | 2023-03-01→2026-03-19 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb15_sweep_price | FAIL at SCREEN | 0 | 1562 | -2.47 | 0.87 | -1.98 | 2/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb5 | FAIL at CONTROLS | 2 | 2267 | 1.52 | 1.06 | 0.99 | 7/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb5_liqpull | FAIL at CONTROLS | 2 | 400 | 6.52 | 1.17 | 1.3 | 4/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb5_of | FAIL at CONTROLS | 2 | 433 | 5.49 | 1.14 | 1.1 | 2/4 | 2023-03-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb5_of_dev__exits | FAIL — no exit rule passes | 0 | 433 | 5.49 | 1.14 |  |  | none→none | 2026-10-16 14:05 | 1 | derived (orb5_of_dev) | yes | no |
| orb5_rangeflow | FAIL at CONTROLS | 2 | 404 | 7.28 | 1.18 | 1.36 | 3/3 | 2023-03-01→2026-03-19 | 2026-10-17 13:15 | 2 | yes | yes | no |
| orb5_sweep_price | FAIL at SCREEN | 0 | 1006 | -1.42 | 0.94 | -0.7 | 3/10 | 2010-07-01→2024-12-31 | 2026-10-17 21:08 | 1 | yes | yes | no |
| orb__tape_trend | FAIL at WALK-FORWARD | 1 | 2659 | 0.8 | 1.03 | 0.59 | 7/16 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| overnight_asia | FAIL at WALK-FORWARD | 1 | 1714 | 0.81 | 1.04 | 0.5 | 8/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| overnight_drift | FAIL at CONTROLS | 2 | 1952 | 1.97 | 1.08 | 0.93 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| overnight_trend_open | FAIL at CONTROLS | 2 | 644 | 2.04 | 1.08 | 0.67 | 9/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 3 | yes | yes | no |
| pat_3bar | PASS | 5 | 1757 | 2.51 | 1.1 | 1.69 | 6/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_3bar_casc | FAIL at CONTROLS | 2 | 1581 | 1.17 | 1.07 | 0.93 | 6/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_double | FAIL at CONTROLS | 2 | 1996 | 1.52 | 1.08 | 1.14 | 7/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_double_casc | PASS | 5 | 1797 | 3.15 | 1.17 | 2.35 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_engulf | FAIL at SCREEN | 0 | 1489 | -0.84 | 0.96 | -0.55 | 2/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_engulf_casc | FAIL at CONTROLS | 2 | 1243 | 2.71 | 1.13 | 1.62 | 6/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_engulf_mtf | FAIL at CONTROLS | 2 | 1156 | 2.45 | 1.11 | 1.35 | 3/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_engulf_mtf_casc | FAIL at CONTROLS | 2 | 1076 | 2.41 | 1.12 | 1.33 | 6/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_flag | FAIL at CONTROLS | 2 | 200 | 1.37 | 1.09 | 0.42 | 7/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_flag_casc | FAIL at CONTROLS | 2 | 199 | 3.12 | 1.21 | 0.96 | 8/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_inside | FAIL at SCREEN | 0 | 857 | -2.55 | 0.85 | -1.65 | 2/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_inside_casc | FAIL at CONTROLS | 2 | 653 | 1.21 | 1.07 | 0.63 | 4/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_outside | FAIL at SCREEN | 0 | 557 | -4.08 | 0.84 | -1.53 | 3/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_outside_casc | FAIL at WALK-FORWARD | 1 | 661 | -0.36 | 0.98 | -0.18 | 7/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_pin | FAIL at SCREEN | 0 | 1262 | -2.63 | 0.88 | -1.62 | 4/11 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_pin_casc | FAIL at WALK-FORWARD | 1 | 1201 | 0.4 | 1.02 | 0.24 | 5/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pat_structure | FAIL at WALK-FORWARD | 1 | 1594 | 1.07 | 1.05 | 0.66 | 5/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| pat_structure_casc | FAIL at CONTROLS | 2 | 1939 | 2.16 | 1.11 | 1.6 | 8/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:15 | 2 | yes | yes | no |
| pdbox_break_of | FAIL at WALK-FORWARD | 1 | 123 | -5.78 | 0.84 | -0.84 | 0/3 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| pdbox_break_price | FAIL at WALK-FORWARD | 1 | 1029 | 0.31 | 1.01 | 0.17 | 6/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| pdbox_sweep_of | FAIL at WALK-FORWARD | 1 | 40 | -6.14 | 0.78 | -0.71 | 2/3 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| pdbox_sweep_price | FAIL at SCREEN | 0 | 787 | -2.07 | 0.9 | -1.12 | 4/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| pdm_cascade_confirm | FAIL at ROBUSTNESS | 3 | 176 | 17.99 | 1.69 | 3.12 | 3/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:16 | 1 | yes | yes | no |
| pdm_gap_confirm | PASS | 5 | 163 | 28.26 | 2.09 | 4.26 | 5/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:16 | 1 | yes | yes | no |
| pdm_gap_confirm__exits | FAIL — no exit rule passes | 0 | 163 | 28.26 | 2.09 |  |  | none→none | 2026-10-18 14:47 | 1 | derived (pdm_gap_confirm) | yes | no |
| pdm_mom_confirm | PASS | 5 | 170 | 22.16 | 1.79 | 3.36 | 5/5 | 2019-01-01→2024-12-31 | 2026-10-18 14:16 | 1 | yes | yes | no |
| pdm_mom_confirm__exits | FAIL — no exit rule passes | 0 | 170 | 22.16 | 1.79 |  |  | none→none | 2026-10-18 14:47 | 1 | derived (pdm_mom_confirm) | yes | no |
| pdx_es_confirm | PASS | 5 | 179 | 4.86 | 1.74 | 3.28 | 4/5 | 2019-01-01→2024-12-31 | 2026-10-18 15:01 | 1 | yes | yes | no |
| pdx_late_confirm | FAIL at CONTROLS | 2 | 81 | 9.89 | 1.36 | 1.2 | 1/4 | 2019-01-01→2024-12-31 | 2026-10-18 15:01 | 1 | yes | yes | no |
| pm_drive | FAIL at WALK-FORWARD | 1 | 1052 | -0.65 | 0.96 | -0.5 | 5/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| postclose_drift | FAIL at CONTROLS | 2 | 129 | 1.88 | 1.22 | 0.75 | 2/6 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| pre_holiday | FAIL at CONTROLS | 2 | 125 | 6.72 | 1.24 | 0.75 | 8/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 3 | yes | yes | no |
| quiet_1135_m1 | FAIL at CONTROLS | 2 | 129 | 2.72 | 1.16 | 0.61 | 7/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:49 | 1 | yes | yes | no |
| quiet_1135_open | FAIL at SCREEN | 0 | 254 | -0 | 1 | -0 | 3/7 | 2010-07-01→2024-12-31 | 2026-10-17 21:49 | 1 | yes | yes | no |
| quiet_1135_vwap | FAIL at SCREEN | 0 | 343 | 0.11 | 1.01 | 0.04 | 2/10 | 2010-07-01→2024-12-31 | 2026-10-17 21:49 | 1 | yes | yes | no |
| quiet_pm_break | FAIL at WALK-FORWARD | 1 | 393 | 0.5 | 1.03 | 0.19 | 5/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| range_absorption | FAIL at SCREEN | 0 | 69 | -2.34 | 0.93 | -0.29 | 1/3 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| range_day_fade | FAIL at WALK-FORWARD | 1 | 488 | 0.72 | 1.03 | 0.26 | 4/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| range_day_fade__tape_rev | FAIL at WALK-FORWARD | 1 | 529 | -0.78 | 0.96 | -0.32 | 5/16 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| rdir_absorb | FAIL at SCREEN | 0 | 1 | 66 | 99 | 0 | 1/1 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 | yes | yes | no |
| rdir_delta | FAIL at CONTROLS | 2 | 170 | 14.8 | 1.39 | 1.91 | 3/3 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 | yes | yes | no |
| rdir_m1 | PASS | 5 | 90 | 34.81 | 2.1 | 3.02 | 4/4 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 | yes | yes | no |
| rdir_m1_delta | PASS | 5 | 68 | 37.29 | 2.22 | 2.91 | 4/4 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 | yes | yes | no |
| rdir_ondelta | FAIL at SCREEN | 0 | 77 | -6.61 | 0.85 | -0.66 | 1/4 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 | yes | yes | no |
| rdir_price | FAIL at CONTROLS | 2 | 433 | 7.03 | 1.15 | 1.26 | 3/4 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 | yes | yes | no |
| rdir_price_delta | FAIL at CONTROLS | 2 | 158 | 16.36 | 1.42 | 1.94 | 3/3 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 | yes | yes | no |
| rdir_price_delta_compl | FAIL at CONTROLS | 2 | 391 | 8.15 | 1.18 | 1.4 | 3/4 | 2023-03-01→2026-03-19 | 2026-10-17 21:29 | 1 | yes | yes | no |
| rd_open | FAIL at WALK-FORWARD | 1 | 108 | 0.07 | 1 | 0.02 | 6/13 | 2010-07-01→2024-12-31 | 2026-10-18 14:58 | 1 | yes | yes | no |
| rd_range | FAIL at SCREEN | 0 | 432 | -2.35 | 0.82 | -1.39 | 4/13 | 2010-07-01→2024-12-31 | 2026-10-18 14:58 | 1 | yes | yes | no |
| rd_vwap | FAIL at WALK-FORWARD | 1 | 529 | -2.78 | 0.85 | -1.37 | 4/14 | 2010-07-01→2024-12-31 | 2026-10-18 14:57 | 1 | yes | yes | no |
| rg_ema_asia | FAIL at SCREEN | 0 | 552 | -0.66 | 0.94 | -0.59 | 1/5 | 2010-07-01→2024-12-31 | 2026-10-17 14:11 | 1 | yes | yes | no |
| rg_ema_ldn | FAIL at SCREEN | 0 | 254 | -1.44 | 0.87 | -0.94 | 1/6 | 2010-07-01→2024-12-31 | 2026-10-17 14:11 | 1 | yes | yes | no |
| rg_ema_ny | FAIL at WALK-FORWARD | 1 | 1841 | -0.08 | 1 | -0.07 | 8/13 | 2010-07-01→2024-12-31 | 2026-10-17 14:11 | 1 | yes | yes | no |
| rg_fade_asia | FAIL at SCREEN | 0 | 675 | -0.44 | 0.96 | -0.38 | 3/6 | 2010-07-01→2024-12-31 | 2026-10-17 14:11 | 1 | yes | yes | no |
| rg_fade_ldn | FAIL at SCREEN | 0 | 238 | -3 | 0.85 | -1.03 | 1/5 | 2010-07-01→2024-12-31 | 2026-10-17 14:11 | 1 | yes | yes | no |
| rg_fade_ny | FAIL at SCREEN | 0 | 387 | -5.3 | 0.71 | -3.02 | 1/7 | 2010-07-01→2024-12-31 | 2026-10-17 14:11 | 1 | yes | yes | no |
| rgm_F_acr | FAIL at CONTROLS | 2 | 1202 | 1.13 | 1.06 | 0.7 | 6/14 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_acr_not | FAIL at SCREEN | 0 | 677 | -1.23 | 0.93 | -0.7 | 2/12 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_all | FAIL at SCREEN | 0 | 1210 | -2.77 | 0.86 | -2.02 | 2/12 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_eff | FAIL at SCREEN | 0 | 186 | -4.63 | 0.8 | -1.09 | 2/11 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_eff_not | FAIL at WALK-FORWARD | 1 | 1238 | -1.54 | 0.91 | -1.23 | 4/12 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_gap | FAIL at SCREEN | 0 | 999 | -2.18 | 0.88 | -1.4 | 3/11 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_gap_not | FAIL at WALK-FORWARD | 1 | 284 | 0.33 | 1.03 | 0.15 | 3/13 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_onr | FAIL at WALK-FORWARD | 1 | 454 | -1.72 | 0.92 | -0.74 | 6/13 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_onr_not | FAIL at SCREEN | 0 | 809 | -2.37 | 0.88 | -1.34 | 3/13 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_orw | FAIL at SCREEN | 0 | 768 | -2.69 | 0.84 | -1.72 | 4/12 | 2010-07-01→2024-12-31 | 2026-10-18 10:58 | 1 | yes | yes | no |
| rgm_F_orw_not | FAIL at CONTROLS | 2 | 557 | 2.07 | 1.1 | 0.89 | 6/11 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_F_pdt | FAIL at SCREEN | 0 | 446 | -2.57 | 0.88 | -1.1 | 3/11 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_F_pdt_not | FAIL at WALK-FORWARD | 1 | 730 | -1.02 | 0.94 | -0.57 | 6/12 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_F_vix | FAIL at WALK-FORWARD | 1 | 473 | 0.45 | 1.02 | 0.14 | 6/8 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_F_vix_not | FAIL at SCREEN | 0 | 578 | -2.42 | 0.78 | -2.23 | 2/11 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_T_acr | FAIL at CONTROLS | 2 | 1353 | 4.2 | 1.15 | 1.83 | 8/10 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_T_acr_not | FAIL at CONTROLS | 2 | 1474 | 3.91 | 1.19 | 2.19 | 7/12 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_T_all | FAIL at CONTROLS | 2 | 1499 | 3.77 | 1.14 | 1.74 | 6/10 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_T_eff | FAIL at CONTROLS | 2 | 1365 | 6.01 | 1.25 | 2.79 | 9/13 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_T_eff_not | FAIL at CONTROLS | 2 | 1408 | 2.62 | 1.09 | 1.21 | 6/9 | 2010-07-01→2024-12-31 | 2026-10-18 10:59 | 1 | yes | yes | no |
| rgm_T_gap | FAIL at CONTROLS | 2 | 1603 | 4.2 | 1.18 | 2.22 | 7/13 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rgm_T_gap_not | FAIL at SCREEN | 0 | 160 | -11.25 | 0.56 | -2.57 | 1/11 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rgm_T_onr | FAIL at WALK-FORWARD | 1 | 464 | 0.65 | 1.02 | 0.18 | 7/10 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rgm_T_onr_not | FAIL at CONTROLS | 2 | 1017 | 2.6 | 1.11 | 1.11 | 5/13 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rgm_T_orw | FAIL at CONTROLS | 2 | 841 | 1.67 | 1.07 | 0.68 | 5/11 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rgm_T_orw_not | FAIL at CONTROLS | 2 | 701 | 5.67 | 1.22 | 1.79 | 8/13 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rgm_T_pdt | FAIL at SCREEN | 0 | 487 | -2.78 | 0.9 | -0.79 | 3/12 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rgm_T_pdt_not | FAIL at CONTROLS | 2 | 992 | 5.54 | 1.28 | 2.46 | 8/14 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rgm_T_vix | FAIL at WALK-FORWARD | 1 | 556 | 1.13 | 1.03 | 0.29 | 3/10 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rgm_T_vix_not | FAIL at CONTROLS | 2 | 1174 | 2.41 | 1.13 | 1.34 | 8/13 | 2010-07-01→2024-12-31 | 2026-10-18 11:00 | 1 | yes | yes | no |
| rnbox_break_of | FAIL at CONTROLS | 2 | 333 | 12.39 | 1.37 | 2.44 | 3/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| rnbox_break_price | FAIL at WALK-FORWARD | 1 | 1427 | -0.94 | 0.97 | -0.51 | 4/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| rnbox_sweep_of | FAIL at SCREEN | 0 | 108 | -17.08 | 0.58 | -2.52 | 1/3 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| rnbox_sweep_price | FAIL at SCREEN | 0 | 860 | -2.81 | 0.84 | -1.93 | 3/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| scalp_daily_1005 | FAIL at SCREEN | 0 | 525 | -0.18 | 0.98 | -0.18 | 2/5 | 2010-07-01→2024-12-31 | 2026-10-17 21:52 | 1 | yes | yes | no |
| scalp_m1_1000 | PASS | 5 | 312 | 2.46 | 1.47 | 2.59 | 10/13 | 2010-07-01→2024-12-31 | 2026-10-17 21:52 | 1 | yes | yes | no |
| scalp_mom_vwap | FAIL at SCREEN | 0 | 636 | -1 | 0.9 | -1.16 | 1/6 | 2010-07-01→2024-12-31 | 2026-10-17 21:52 | 1 | yes | yes | no |
| smc_fvg_dp | FAIL at CONTROLS | 2 | 1225 | 2.43 | 1.2 | 2.25 | 9/14 | 2010-07-01→2024-12-31 | 2026-10-18 13:38 | 1 | yes | yes | no |
| smc_fvg_fill | FAIL at SCREEN | 0 | 350 | -3.11 | 0.8 | -1.32 | 1/6 | 2010-07-01→2024-12-31 | 2026-10-18 13:01 | 1 | yes | yes | no |
| smc_fvg_fill_lux | FAIL at WALK-FORWARD | 1 | 1366 | -1.65 | 0.9 | -1.3 | 5/12 | 2010-07-01→2024-12-31 | 2026-10-18 13:32 | 1 | yes | yes | no |
| smc_ob_dp | FAIL at CONTROLS | 2 | 746 | 0.97 | 1.07 | 0.63 | 7/14 | 2010-07-01→2024-12-31 | 2026-10-18 13:37 | 1 | yes | yes | no |
| smc_ob_retest | FAIL at WALK-FORWARD | 1 | 1798 | 0.59 | 1.03 | 0.49 | 3/12 | 2010-07-01→2024-12-31 | 2026-10-18 13:01 | 1 | yes | yes | no |
| smc_ob_retest_lux | FAIL at SCREEN | 0 | 923 | -0.62 | 0.96 | -0.4 | 4/14 | 2010-07-01→2024-12-31 | 2026-10-18 13:32 | 1 | yes | yes | no |
| so_cascade | PASS | 5 | 2322 | 4.8 | 1.22 | 3.27 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 8 | yes | yes | no |
| so_cascade_dev__exits | PASS (vwap_stall) | 5 | 2318 | 5.8 | 1.41 |  |  | none→none | 2026-10-16 14:05 | 1 | derived (so_cascade_dev) | yes | no |
| so_cascade_dev__meta | PASS | 5 | 1134 | 8.22 | 1.3 |  |  | none→none | 2026-10-16 14:05 | 1 | derived (so_cascade_dev) | yes | no |
| so_cascade_dev__meta_book | FAIL at SHUFFLED LABELS | 2 | 1145 | 7.98 | 1.29 |  |  | none→none | 2026-10-16 14:05 | 1 | derived (so_cascade_dev) | yes | no |
| so_cascade_eff | FAIL at CONTROLS | 2 | 1898 | 4.24 | 1.22 | 2.99 | 11/14 | 2010-07-01→2024-12-31 | 2026-10-18 14:02 | 1 | yes | yes | no |
| so_cascade_eff_not | FAIL at CONTROLS | 2 | 680 | 4.83 | 1.26 | 2.11 | 11/14 | 2010-07-01→2024-12-31 | 2026-10-18 14:02 | 1 | yes | yes | no |
| so_cascade__exits | PASS (vwap_stall) | 5 | 2322 | 5.82 | 1.41 |  |  | none→none | 2026-10-18 14:03 | 7 | derived (so_cascade) | yes | no |
| so_cascade__meta | FAIL at SHUFFLED LABELS | 2 | 1132 | 8.29 | 1.3 |  |  | none→none | 2026-10-17 15:57 | 4 | derived (so_cascade) | yes | yes |
| so_cascade__meta_page | FAIL at ROBUSTNESS | 3 | 1085 | 9.96 | 1.38 |  |  | none→none | 2026-10-18 12:17 | 1 | derived (so_cascade) | yes | yes |
| so_cascade_nofomc | FAIL at CONTROLS | 2 | 2295 | 4.64 | 1.21 | 3.13 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 7 | yes | yes | no |
| so_cascade__tape_trend | FAIL at CONTROLS | 2 | 1905 | 3.04 | 1.13 | 1.88 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| strong_open | PASS | 5 | 1115 | 4.18 | 1.17 | 1.85 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 9 | yes | yes | no |
| strong_open__tape_trend | FAIL at CONTROLS | 2 | 793 | 4.02 | 1.17 | 1.61 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| trend_box2 | FAIL at CONTROLS | 2 | 448 | 2.5 | 1.16 | 1.15 | 5/10 | 2010-07-01→2024-12-31 | 2026-10-17 15:56 | 1 | yes | yes | no |
| ts_vwap30_confirm | FAIL at CONTROLS | 2 | 336 | 1.02 | 1.07 | 0.62 | 2/3 | 2023-01-01→2026-10-16 | 2026-10-18 15:50 | 1 | yes | yes | no |
| tuesday_turnaround | FAIL at CONTROLS | 2 | 151 | 11.12 | 1.36 | 1.29 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 3 | yes | yes | no |
| turn_of_month | FAIL at CONTROLS | 2 | 463 | 5.84 | 1.15 | 0.95 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 7 | yes | yes | no |
| union_2r | PASS | 5 | 2481 | 4.01 | 1.19 | 3.11 | 12/14 | 2010-07-01→2024-12-31 | 2026-10-17 21:53 | 1 | yes | yes | no |
| union_2r__exits | FAIL — no exit rule passes | 0 | 2481 | 4.01 | 1.19 |  |  | none→none | 2026-10-18 12:31 | 2 | derived (union_2r) | yes | no |
| union_2r__meta_page | FAIL at ROBUSTNESS | 3 | 1117 | 8.24 | 1.34 |  |  | none→none | 2026-10-18 12:17 | 1 | derived (union_2r) | yes | yes |
| union_scalp | FAIL at SCREEN | 0 | 298 | -0.24 | 0.98 | -0.17 | 2/5 | 2010-07-01→2024-12-31 | 2026-10-17 21:53 | 1 | yes | yes | no |
| union_scalp_3 | FAIL at SCREEN | 0 | 793 | -0.13 | 0.99 | -0.16 | 2/3 | 2010-07-01→2024-12-31 | 2026-10-17 21:53 | 1 | yes | yes | no |
| vabox_break_of | FAIL at WALK-FORWARD | 1 | 134 | -9.41 | 0.76 | -1.43 | 1/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| vabox_break_price | FAIL at SCREEN | 0 | 956 | -1.11 | 0.95 | -0.59 | 5/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| vabox_sweep_of | FAIL at WALK-FORWARD | 1 | 56 | -14.19 | 0.62 | -1.58 | 1/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| vabox_sweep_price | FAIL at WALK-FORWARD | 1 | 741 | -2.37 | 0.91 | -1.02 | 3/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| vol_target_delever | FAIL at SCREEN | 0 | 196 | -15.92 | 0.63 | -2.12 | 2/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 3 | yes | yes | no |
| vwap_fade | FAIL at WALK-FORWARD | 1 | 2241 | -1.04 | 0.94 | -1.06 | 5/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 7 | yes | yes | no |
| vwap_fade__tape_rev | FAIL at WALK-FORWARD | 1 | 2291 | -0.16 | 0.99 | -0.18 | 6/16 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| vwap_follow | FAIL at CONTROLS | 2 | 3391 | 1.72 | 1.08 | 1.66 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 7 | yes | yes | no |
| vwap_hold_trend | FAIL at WALK-FORWARD | 1 | 716 | -0.21 | 0.99 | -0.11 | 4/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| vwap_reclaim | FAIL at SCREEN | 0 | 1736 | -0.52 | 0.97 | -0.44 | 5/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| vwap_side_ema | FAIL at CONTROLS | 2 | 2542 | 2.24 | 1.07 | 1.46 | 5/8 | 2010-07-01→2024-12-31 | 2026-10-17 21:43 | 1 | yes | yes | no |
| vwbbox_break_of | FAIL at CONTROLS | 2 | 320 | 2.99 | 1.09 | 0.64 | 2/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| vwbbox_break_price | FAIL at SCREEN | 0 | 1040 | -0.95 | 0.97 | -0.41 | 4/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| vwbbox_sweep_of | FAIL at SCREEN | 0 | 173 | -2.08 | 0.94 | -0.39 | 1/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| vwbbox_sweep_price | FAIL at SCREEN | 0 | 643 | -5.3 | 0.67 | -3.54 | 0/10 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| weak_day_flight | PASS | 5 | 209 | 29.8 | 1.74 | 2.78 | 10/13 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| weak_day_flight_compl | FAIL at CONTROLS | 2 | 134 | 27.49 | 1.44 | 1.18 | 6/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| weak_day_gapdown | FAIL at CONTROLS | 2 | 208 | 13.98 | 1.36 | 1.33 | 11/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| weak_day_gapup | FAIL at CONTROLS | 2 | 334 | 5.56 | 1.14 | 0.76 | 8/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| weak_day_long | PASS | 5 | 824 | 13.2 | 1.36 | 2.5 | 10/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 7 | yes | yes | no |
| weak_day_long__exits | FAIL — no exit rule passes | 0 | 824 | 13.2 | 1.36 |  |  | none→none | 2026-10-16 14:39 | 1 | derived (weak_day_long) | yes | no |
| weak_day_long__meta | FAIL at RANDOM SKIP | 1 | 496 | 20.19 | 1.4 |  |  | none→none | 2026-10-16 14:39 | 1 | derived (weak_day_long) | yes | no |
| weak_day_long__tape_rev | FAIL at CONTROLS | 2 | 598 | 5.79 | 1.14 | 0.89 | 9/16 | 2010-07-01→2026-10-16 | 2026-10-17 14:50 | 1 | yes | yes | no |
| weak_day_ts | FAIL at CONTROLS | 2 | 222 | 25.59 | 1.5 | 1.64 | 12/15 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| weak_day_usd | FAIL at CONTROLS | 2 | 65 | 16.25 | 1.34 | 0.85 | 5/9 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| weak_day_usd_compl | FAIL at CONTROLS | 2 | 296 | 22.36 | 1.45 | 1.89 | 9/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| weekend_gap_fade | FAIL at CONTROLS | 2 | 96 | 9.49 | 1.78 | 1.59 | 6/12 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| wkbox_break_of | FAIL at SCREEN | 0 | 45 | -6.15 | 0.82 | -0.56 | 1/4 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| wkbox_break_price | FAIL at CONTROLS | 2 | 426 | 2.51 | 1.12 | 0.86 | 8/16 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| wkbox_sweep_of | FAIL at SCREEN | 0 | 12 | -0.7 | 0.98 | -0.03 | 1/2 | 2023-03-01→2026-03-19 | 2026-10-17 13:16 | 2 | yes | yes | no |
| wkbox_sweep_price | FAIL at WALK-FORWARD | 1 | 515 | -2.4 | 0.88 | -1.12 | 5/14 | 2010-07-01→2026-10-16 | 2026-10-17 13:16 | 2 | yes | yes | no |
| wk_breakout | FAIL at WALK-FORWARD | 1 | 420 | 0.74 | 1.03 | 0.23 | 5/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| wk_ema_pb | FAIL at WALK-FORWARD | 1 | 558 | 0.62 | 1.02 | 0.19 | 4/14 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| wk_fib38 | FAIL at WALK-FORWARD | 1 | 431 | -0.19 | 0.99 | -0.05 | 2/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| wk_market | FAIL at CONTROLS | 2 | 511 | 1.92 | 1.06 | 0.56 | 4/14 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| wk_or_retest | FAIL at CONTROLS | 2 | 426 | 4.14 | 1.16 | 1.15 | 7/13 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| wk_trend60 | FAIL at WALK-FORWARD | 1 | 540 | -1.57 | 0.93 | -0.59 | 4/14 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| wk_vwap_pb | FAIL at WALK-FORWARD | 1 | 615 | 1.24 | 1.04 | 0.4 | 5/14 | 2010-07-01→2024-12-31 | 2026-10-17 13:33 | 1 | yes | yes | no |
| wy_range | FAIL at CONTROLS | 2 | 719 | 3.34 | 1.1 | 0.84 | 6/14 | 2010-07-01→2024-12-31 | 2026-10-18 13:52 | 1 | yes | yes | no |
| wy_range_vol | FAIL at CONTROLS | 2 | 351 | 4.33 | 1.18 | 0.98 | 6/12 | 2010-07-01→2024-12-31 | 2026-10-18 13:52 | 1 | yes | yes | no |
| wy_trend | FAIL at CONTROLS | 2 | 505 | 2.49 | 1.09 | 0.65 | 5/11 | 2010-07-01→2024-12-31 | 2026-10-18 13:52 | 1 | yes | yes | no |

### Specs with no verdict (26)

| id | spec mtime (UTC) | name |
|---|---|---|
| etf_base | 2026-10-19 11:07 | cascade 09:50 market entry |
| etf_brk_1 | 2026-10-19 11:07 | etf_brk_1 |
| etf_brk_15 | 2026-10-19 11:07 | etf_brk_15 |
| etf_brk_5 | 2026-10-19 11:07 | etf_brk_5 |
| etf_pb_1 | 2026-10-19 11:07 | etf_pb_1 |
| etf_pb_15 | 2026-10-19 11:07 | etf_pb_15 |
| etf_pb_5 | 2026-10-19 11:07 | etf_pb_5 |
| flag1m_gx | 2026-10-19 05:25 | 1-min flag after a good move, flag1m_gx |
| flag1m_ny | 2026-10-19 05:25 | 1-min flag after a good move, flag1m_ny |
| gap_fade_m1 | 2026-10-19 11:51 | gap_fade_m1 |
| gap_fade_m2 | 2026-10-19 11:51 | gap_fade_m2 |
| gx_ema_follow | 2026-10-19 05:07 | Globex EMA stack follow, pullback to 5-min EMA20, max 3/day |
| gx_ema_run | 2026-10-19 05:09 | Globex EMA stack follow, EMA20 pullback, no target, trail after +1R, max 3/day |
| gx_ema_run1 | 2026-10-19 05:09 | Globex EMA stack follow, first EMA20 pullback, no target, trail after +1R |
| if_gap | 2026-10-18 14:43 | Indicator filters on the path × direction trade — gap side |
| if_mom | 2026-10-18 14:43 | Indicator filters on the path × direction trade — 45-minute momentum side |
| ldn_pd | 2026-10-18 15:22 | London-open path × direction: the Asia move side, joined at the 03:30 candle close on a straight 03:00–03:30 path |
| london_fade | 2026-10-19 10:37 | Fade the London first hour (03:00→04:00 ET) |
| nq_es_follow | 2026-10-18 19:15 | NQ leads ES → trade NQ the way it leads |
| pd_hold | 2026-10-19 11:51 | PDH/PDL break that holds 30 min |
| pdm_cascade | 2026-10-18 14:16 | Path × direction map — the 09:50 cascade side |
| pdm_gap | 2026-10-18 14:16 | Path × direction map — The overnight gap side |
| pdm_mom | 2026-10-18 14:16 | Path × direction map — The 45-minute momentum side |
| pdx_fade | 2026-10-19 12:00 | Fade the first break of yesterday high / low |
| quad_close | 2026-10-18 19:16 | Quarterly rebalance day: trade the 14:00-15:00 move into the close |
| trend_day_pb | 2026-10-19 04:39 | Confirmed trend day (09:50), pullback to VWAP / EMA20, max 3/day |
