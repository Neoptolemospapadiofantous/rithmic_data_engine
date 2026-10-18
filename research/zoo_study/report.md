# Model-zoo validation + feature selection — results

Pre-registration: `research/preregistered/2026-10-18_zoo_study.md`. Run 2026-10-18T08:57-04:00 → 2026-10-18T09:16-04:00 (19 min), harness `bot/python/research/zoo_study.py`, settings: shuffled refits 20, permutation seeds 10, fixed 120 boosting rounds, DAYS=40.

## Verdicts

| model | OOS metric | baseline | shuffled pct beaten | verdict | features that work |
|---|---|---|---|---|---|
| day_type | acc 0.472 | 0.480 | 1.00 | FAIL | on_range, on_vol, on_eff, gap, prev_range, prev_close_pos, prev_trend_score, dow, on_minutes |
| trade_filter | AUC 0.509 | 0.500 | 0.75 | NOT VALIDATED (short history) — exploratory weekly: rule not met | orb_range_pts, entry_min_after_open, pre5_volume, pre5_ticks, overnight_range_pts, sl_points, direction=SHORT, mtf:tf1_ret10, mtf:tf1_atr14, mtf:tf1_ema9_21, mtf:tf1_ema21_slope, mtf:tf1_vol_z20, mtf:tf1_vwap_dist, mtf:tf5_ret3, mtf:tf5_atr14, mtf:tf5_ema9_21, mtf:tf60_ret1, mtf:tf60_ret3, mtf:tf60_rvol10, mtf:tf60_atr14, mtf:tf60_range_pos20, mtf:tf60_vol_z20, mtf:tf60_vwap_dist, mtf:overnight_range, mtf:open_gap, mtf:tick_rate_5m, mtf:prev_day_range, mtf:dist_pdh_atr, mtf:es_rel_ret_15m_bp, mtf:min_to_news, mtf:bbo_imb_open15 |
| adaptive_risk | pinball0.8 1.859 | 1.779 | 0.00 | NOT VALIDATED (short history) — exploratory weekly: rule not met | orb_range_pts, entry_min_after_open, pre5_volume, pre15_range_pts, overnight_range_pts, is_long |
| breakout_failure | — | — | — | NOT VALIDATED — 114 cross events (113 failed) — below the model's own minimum (60 with ≥10 of each) | — |
| variant_bandit | — | — | — | NOT VALIDATED — Thompson ranking: no supervised label and no features; judging a ranking needs months of forward weeks, and its day_table mixes every paper label (replay labels of the same ids count as pulls) | — |
| exit_model | AUC 0.499 | 0.500 | 0.40 | NOT VALIDATED (short history) — exploratory weekly: rule not met | unreal, mfe, mae, giveback, sl_points, trail_step |
| regime | — | — | — | NOT VALIDATED — unsupervised k-means (hourly bar features): no label to shuffle or score | — |
| fill_quality | MAE 0.929 | 2.878 | 1.00 | NOT VALIDATED (short history) — exploratory weekly: rule met | entry_min_after_open, pre5_volume, pre5_range_pts, orb_range_pts |
| policy_search | — | — | — | NOT VALIDATED — decision grid with no features or labels — not a supervised model; orientation below | — |
| sequence | AUC 0.505 | 0.500 | 1.00 | PASS | ret, range, vol_z |
| exit_optimizer | — | — | — | NOT VALIDATED — decision rule (trigger/step/delay) with no features — not a supervised model; orientation below | — |
| signal_meta | AUC 0.456 | 0.500 | 0.35 | NOT VALIDATED (short history) — exploratory weekly: rule not met | hour, eng_orb, eng_trend |
| overlay_uplift | MAE 18.809 | 21.973 | 1.00 | NOT VALIDATED (short history) — exploratory weekly: rule met | spread, hour, eng_orb, eng_trend |
| fill_cost | MAE 0.880 | 0.885 | 1.00 | NOT VALIDATED (short history) — exploratory weekly: rule met | spread_entry, spread_exit, hour |

PASS = beats ≥ 95 % of shuffled-label refits AND the trivial baseline under the registered walk-forward. NOT VALIDATED (short history) = the data spans < 7 months, so only the exploratory weekly walk-forward ran; "rule met" there is orientation, not evidence.

## day_type

### day_type (primary)

- rows 3971, span 2010-07-02 → 2026-10-16, scheme **yearly**, folds 14, features 9
- OOS metric (multiclass): **0.472** vs baseline 0.480 (does not beat it); trend-vs-rest AUC 0.509
- shuffled-label refits (n=20): min 0.440 · median 0.451 · p95 0.464 · max 0.465 → real beats **1.00** of them
- verdict: **FAIL**
- refit on working features only: 0.472 (all features: 0.472)
- per fold: 2013 n=248 0.435/0.435; 2014 n=246 0.472/0.484; 2015 n=233 0.429/0.442; 2016 n=254 0.441/0.437; 2017 n=253 0.407/0.423; 2018 n=253 0.474/0.478; 2019 n=254 0.429/0.425; 2020 n=254 0.465/0.484; 2021 n=254 0.531/0.555; 2022 n=254 0.571/0.587; 2023 n=253 0.514/0.522; 2024 n=255 0.463/0.467; 2025 n=253 0.482/0.482; 2026 n=74 0.527/0.527
- class_counts: `{"range": 968, "mixed": 1090, "trend": 1913}`

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| prev_close_pos | +0.0091 | 2/3 | -0.0010 | **yes** |
| on_eff | +0.0081 | 3/3 | -0.0006 | **yes** |
| gap | +0.0077 | 3/3 | -0.0018 | **yes** |
| on_minutes | +0.0062 | 2/3 | -0.0018 | **yes** |
| prev_trend_score | +0.0054 | 2/3 | -0.0015 | **yes** |
| on_vol | +0.0037 | 2/3 | +0.0004 | **yes** |
| dow | +0.0036 | 2/3 | -0.0018 | **yes** |
| prev_range | +0.0025 | 2/3 | +0.0004 | **yes** |
| on_range | +0.0021 | 2/3 | +0.0011 | **yes** |

## trade_filter

*short history: 111871 rows over 23 days (needs ≥ 6 training months for the monthly scheme); exploratory weekly result shown for orientation only*

### trade_filter.p_win (primary)

- rows 111871, span 2026-09-21 → 2026-10-14, scheme **weekly** (exploratory), folds 2, features 93
- OOS metric (binary): **0.509** vs baseline 0.500 (beats it)
- shuffled-label refits (n=20): min 0.443 · median 0.493 · p95 0.520 · max 0.552 → real beats **0.75** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 0.550 (all features: 0.509)
- per fold: 2026-W41 n=41170 0.538/0.500; 2026-W42 n=38206 0.509/0.500

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| mtf:prev_day_range | +0.0202 | 2/3 | -0.0001 | **yes** |
| mtf:tf60_range_pos20 | +0.0084 | 2/3 | -0.0000 | **yes** |
| mtf:overnight_eff | +0.0076 | 0/3 | +0.0005 | no |
| mtf:dist_pdh_atr | +0.0060 | 2/3 | +0.0001 | **yes** |
| entry_min_after_open | +0.0057 | 3/3 | -0.0015 | **yes** |
| pre5_ticks | +0.0056 | 3/3 | +0.0009 | **yes** |
| mtf:tf1_vwap_dist | +0.0045 | 3/3 | +0.0017 | **yes** |
| mtf:tf1_ret10 | +0.0045 | 3/3 | -0.0014 | **yes** |
| mtf:tf1_ema9_21 | +0.0032 | 2/3 | -0.0012 | **yes** |
| sl_points | +0.0020 | 2/3 | -0.0003 | **yes** |
| mtf:tf1_atr14 | +0.0017 | 2/3 | +0.0012 | **yes** |
| mtf:tf1_ema21_slope | +0.0017 | 3/3 | -0.0009 | **yes** |
| pre5_volume | +0.0015 | 3/3 | +0.0004 | **yes** |
| mtf:tf1_vol_z20 | +0.0015 | 2/3 | +0.0012 | **yes** |
| mtf:bbo_spread_ticks_1m | +0.0013 | 1/3 | -0.0018 | no |
| mtf:open_gap | +0.0012 | 2/3 | +0.0004 | **yes** |
| mtf:tick_rate_5m | +0.0011 | 2/3 | +0.0003 | **yes** |
| mtf:tf5_ema9_21 | +0.0010 | 2/3 | -0.0003 | **yes** |
| mtf:tf60_rvol10 | +0.0010 | 2/3 | +0.0004 | **yes** |
| mtf:tf5_ret3 | +0.0009 | 3/3 | -0.0018 | **yes** |
| mtf:overnight_range | +0.0008 | 2/3 | +0.0001 | **yes** |
| mtf:tf5_atr14 | +0.0008 | 3/3 | -0.0000 | **yes** |
| mtf:min_to_news | +0.0007 | 2/3 | -0.0001 | **yes** |
| mtf:tf15_ret10 | +0.0006 | 1/3 | -0.0009 | no |
| mtf:tf60_ret3 | +0.0006 | 3/3 | +0.0003 | **yes** |
| open_gap_pts | +0.0006 | 3/3 | +0.0006 | no |
| engine=trend | +0.0005 | 1/3 | -0.0003 | no |
| mtf:bbo_imb_open15 | +0.0005 | 2/3 | -0.0001 | **yes** |
| mtf:tf60_vol_z20 | +0.0005 | 2/3 | -0.0006 | **yes** |
| orb_range_pts | +0.0004 | 3/3 | -0.0014 | **yes** |
| mtf:tf15_ret3 | +0.0003 | 2/3 | +0.0016 | no |
| mtf:tf60_ret1 | +0.0002 | 2/3 | -0.0007 | **yes** |
| mtf:hour_et | +0.0002 | 1/3 | +0.0002 | no |
| mtf:tf60_vwap_dist | +0.0002 | 2/3 | -0.0006 | **yes** |
| direction=LONG | +0.0001 | 1/3 | -0.0002 | no |
| mtf:tf60_atr14 | +0.0001 | 2/3 | -0.0008 | **yes** |
| mtf:tf5_rvol10 | +0.0001 | 2/3 | +0.0008 | no |
| mtf:es_rel_ret_15m_bp | +0.0001 | 2/3 | -0.0003 | **yes** |
| mtf:bbo_imb_persist_open15 | +0.0001 | 2/3 | +0.0002 | no |
| direction=SHORT | +0.0001 | 2/3 | +0.0000 | **yes** |
| mtf:tf15_ema9_21 | +0.0001 | 2/3 | +0.0001 | no |
| overnight_range_pts | +0.0000 | 2/3 | -0.0002 | **yes** |
| mtf:tf15_ema21_slope | +0.0000 | 0/3 | -0.0001 | no |
| mtf:tf15_vol_z20 | +0.0000 | 1/3 | +0.0008 | no |
| orb_minutes | +0.0000 | 0/3 | +0.0000 | no |
| hour_et | +0.0000 | 0/3 | +0.0000 | no |
| source=live | +0.0000 | 0/3 | +0.0000 | no |
| source=paper | +0.0000 | 0/3 | +0.0000 | no |
| mtf:tf1_bars | +0.0000 | 0/3 | -0.0003 | no |
| mtf:tf5_bars | +0.0000 | 0/3 | +0.0000 | no |
| mtf:tf15_bars | +0.0000 | 0/3 | +0.0000 | no |
| mtf:dow | +0.0000 | 0/3 | +0.0001 | no |
| mtf:rth_min_remaining | -0.0001 | 1/3 | -0.0001 | no |
| mtf:tf5_ret1 | -0.0001 | 1/3 | +0.0022 | no |
| mtf:tf15_atr14 | -0.0001 | 1/3 | +0.0004 | no |
| mtf:tf60_bars | -0.0002 | 3/3 | +0.0001 | no |
| mtf:tf60_ret10 | -0.0002 | 1/3 | -0.0002 | no |
| mtf:tf1_ret3 | -0.0003 | 1/3 | +0.0023 | no |
| mtf:tf5_range_pos20 | -0.0004 | 1/3 | +0.0005 | no |
| mtf:delta_share_5m | -0.0005 | 0/3 | +0.0020 | no |
| mtf:bbo_microprice_dev_ticks | -0.0005 | 0/3 | +0.0016 | no |
| mtf:tf5_ema21_slope | -0.0007 | 1/3 | +0.0006 | no |
| mtf:tf15_range_pos20 | -0.0007 | 1/3 | -0.0006 | no |
| mtf:tf1_ret1 | -0.0008 | 2/3 | +0.0001 | no |
| dow | -0.0008 | 1/3 | +0.0004 | no |
| mtf:bbo_imbalance_1m | -0.0008 | 0/3 | -0.0011 | no |
| mtf:bbo_updates_5m | -0.0009 | 1/3 | -0.0000 | no |
| mtf:tf1_rvol10 | -0.0009 | 1/3 | +0.0019 | no |
| mtf:tf60_ema9_21 | -0.0009 | 1/3 | -0.0009 | no |
| mtf:tf1_range_pos20 | -0.0009 | 1/3 | +0.0005 | no |
| engine=mtf_scalper | -0.0010 | 2/3 | -0.0003 | no |
| mtf:tf5_vwap_dist | -0.0010 | 1/3 | -0.0028 | no |
| day_range_at_entry_pts | -0.0010 | 0/3 | -0.0001 | no |
| mtf:tf15_rvol10 | -0.0011 | 1/3 | +0.0019 | no |
| mtf:min_after_open | -0.0012 | 1/3 | +0.0010 | no |
| mtf:dist_pdl_atr | -0.0014 | 1/3 | +0.0015 | no |
| mtf:tf60_ema21_slope | -0.0014 | 1/3 | -0.0002 | no |
| pre15_range_pts | -0.0014 | 1/3 | +0.0030 | no |
| mtf:tf5_vol_z20 | -0.0015 | 1/3 | +0.0013 | no |
| pre5_range_pts | -0.0016 | 1/3 | -0.0001 | no |
| mtf:tf15_ret1 | -0.0018 | 1/3 | +0.0003 | no |
| mtf:bbo_spread_rel_1h | -0.0018 | 0/3 | +0.0008 | no |
| mtf:tf5_ret10 | -0.0020 | 2/3 | +0.0005 | no |
| engine=orb | -0.0021 | 1/3 | +0.0004 | no |
| mtf:day_range_so_far | -0.0022 | 1/3 | +0.0010 | no |
| mtf:day_pos_at_entry | -0.0025 | 1/3 | -0.0010 | no |
| mtf:tf15_vwap_dist | -0.0026 | 1/3 | -0.0007 | no |
| mtf:tf5_adx14 | -0.0030 | 1/3 | -0.0026 | no |
| mtf:tf5_vwap_slope_atr | -0.0039 | 0/3 | +0.0014 | no |
| mtf:tf5_bb_width_atr | -0.0048 | 1/3 | +0.0008 | no |
| mtf:prev_close_pos | -0.0053 | 2/3 | +0.0003 | no |
| trail_step | -0.0056 | 0/3 | -0.0009 | no |
| dist_from_range_pts | -0.0073 | 1/3 | +0.0018 | no |

### trade_filter.exp_pnl

- rows 111871, span 2026-09-21 → 2026-10-14, scheme **weekly** (exploratory), folds 2, features 93
- OOS metric (l1): **18.301** vs baseline 18.118 (does not beat it)
- shuffled-label refits (n=20): min 18.118 · median 18.118 · p95 18.118 · max 18.118 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 18.159 (all features: 18.301)
- per fold: 2026-W41 n=41170 17.067/16.947; 2026-W42 n=38206 19.630/19.381
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| sl_points | +0.2504 | 3/3 | +0.0000 | **yes** |
| engine=mtf_scalper | +0.1505 | 3/3 | +0.0000 | **yes** |
| trail_step | +0.0375 | 3/3 | +0.0000 | **yes** |
| mtf:tf5_vwap_slope_atr | +0.0303 | 2/3 | +0.0000 | **yes** |
| overnight_range_pts | +0.0238 | 2/3 | +0.0000 | **yes** |
| mtf:tf1_vwap_dist | +0.0201 | 2/3 | +0.0000 | **yes** |
| mtf:tf1_rvol10 | +0.0193 | 2/3 | +0.0000 | **yes** |
| day_range_at_entry_pts | +0.0191 | 1/3 | +0.0000 | no |
| mtf:tf1_vol_z20 | +0.0159 | 2/3 | +0.0000 | **yes** |
| mtf:tf1_ema21_slope | +0.0138 | 1/3 | +0.0000 | no |
| pre5_range_pts | +0.0130 | 1/3 | +0.0000 | no |
| mtf:tf5_vol_z20 | +0.0121 | 2/3 | +0.0000 | **yes** |
| mtf:min_to_news | +0.0081 | 2/3 | +0.0000 | **yes** |
| mtf:tf15_ret1 | +0.0081 | 2/3 | +0.0000 | **yes** |
| entry_min_after_open | +0.0074 | 3/3 | +0.0000 | **yes** |
| mtf:tf15_atr14 | +0.0071 | 3/3 | +0.0000 | **yes** |
| mtf:tf1_ret1 | +0.0067 | 3/3 | +0.0000 | **yes** |
| mtf:tf60_ret10 | +0.0065 | 3/3 | +0.0000 | **yes** |
| mtf:tf60_ema9_21 | +0.0060 | 2/3 | +0.0000 | **yes** |
| mtf:tf15_rvol10 | +0.0051 | 2/3 | +0.0000 | **yes** |
| pre5_ticks | +0.0050 | 3/3 | +0.0000 | **yes** |
| mtf:tf60_ret1 | +0.0045 | 2/3 | +0.0000 | **yes** |
| mtf:open_gap | +0.0044 | 2/3 | +0.0000 | **yes** |
| mtf:tf15_vwap_dist | +0.0028 | 2/3 | +0.0000 | **yes** |
| mtf:overnight_range | +0.0019 | 2/3 | +0.0000 | **yes** |
| mtf:tf60_ret3 | +0.0018 | 2/3 | +0.0000 | **yes** |
| engine=orb | +0.0018 | 1/3 | +0.0000 | no |
| mtf:dist_pdl_atr | +0.0016 | 2/3 | +0.0000 | **yes** |
| mtf:bbo_microprice_dev_ticks | +0.0016 | 2/3 | +0.0000 | **yes** |
| mtf:tf5_ema21_slope | +0.0014 | 3/3 | +0.0000 | **yes** |
| mtf:prev_day_range | +0.0013 | 1/3 | +0.0000 | no |
| mtf:tf15_range_pos20 | +0.0013 | 2/3 | +0.0000 | **yes** |
| mtf:tf5_adx14 | +0.0007 | 3/3 | +0.0000 | **yes** |
| mtf:tf5_vwap_dist | +0.0005 | 2/3 | +0.0000 | **yes** |
| direction=LONG | +0.0005 | 1/3 | +0.0000 | no |
| mtf:rth_min_remaining | +0.0004 | 2/3 | +0.0000 | **yes** |
| mtf:tf60_vwap_dist | +0.0000 | 1/3 | +0.0000 | no |
| dow | +0.0000 | 0/3 | +0.0000 | no |
| hour_et | +0.0000 | 0/3 | +0.0000 | no |
| source=live | +0.0000 | 0/3 | +0.0000 | no |
| source=paper | +0.0000 | 0/3 | +0.0000 | no |
| mtf:tf1_bars | +0.0000 | 0/3 | +0.0000 | no |
| mtf:tf5_bars | +0.0000 | 0/3 | +0.0000 | no |
| mtf:tf15_bars | +0.0000 | 0/3 | +0.0000 | no |
| mtf:dow | +0.0000 | 0/3 | +0.0000 | no |
| mtf:min_after_open | -0.0000 | 2/3 | +0.0000 | no |
| orb_minutes | -0.0001 | 0/3 | +0.0000 | no |
| mtf:hour_et | -0.0001 | 2/3 | +0.0000 | no |
| mtf:tf60_vol_z20 | -0.0001 | 1/3 | +0.0000 | no |
| direction=SHORT | -0.0001 | 1/3 | +0.0000 | no |
| engine=trend | -0.0002 | 1/3 | +0.0000 | no |
| mtf:tf15_ema21_slope | -0.0002 | 2/3 | +0.0000 | no |
| open_gap_pts | -0.0004 | 1/3 | +0.0000 | no |
| mtf:tf15_ema9_21 | -0.0004 | 1/3 | +0.0000 | no |
| mtf:tf1_ret10 | -0.0005 | 1/3 | +0.0000 | no |
| dist_from_range_pts | -0.0006 | 2/3 | +0.0000 | no |
| mtf:tf60_bars | -0.0006 | 0/3 | +0.0000 | no |
| mtf:tf5_ema9_21 | -0.0006 | 2/3 | +0.0000 | no |
| mtf:bbo_imb_persist_open15 | -0.0007 | 1/3 | +0.0000 | no |
| mtf:bbo_imbalance_1m | -0.0008 | 1/3 | +0.0000 | no |
| mtf:day_pos_at_entry | -0.0009 | 1/3 | +0.0000 | no |
| mtf:tf60_atr14 | -0.0011 | 0/3 | +0.0000 | no |
| mtf:bbo_imb_open15 | -0.0014 | 1/3 | +0.0000 | no |
| orb_range_pts | -0.0018 | 0/3 | +0.0000 | no |
| mtf:tf5_range_pos20 | -0.0018 | 1/3 | +0.0000 | no |
| mtf:delta_share_5m | -0.0021 | 1/3 | +0.0000 | no |
| mtf:tf15_vol_z20 | -0.0029 | 1/3 | +0.0000 | no |
| mtf:tf5_bb_width_atr | -0.0036 | 2/3 | +0.0000 | no |
| mtf:tf5_rvol10 | -0.0036 | 1/3 | +0.0000 | no |
| mtf:tf5_ret10 | -0.0037 | 1/3 | +0.0000 | no |
| mtf:tf1_ret3 | -0.0039 | 1/3 | +0.0000 | no |
| mtf:bbo_spread_rel_1h | -0.0041 | 2/3 | +0.0000 | no |
| mtf:tf15_ret10 | -0.0041 | 0/3 | +0.0000 | no |
| mtf:tf60_range_pos20 | -0.0042 | 1/3 | +0.0000 | no |
| mtf:day_range_so_far | -0.0055 | 1/3 | +0.0000 | no |
| mtf:tf15_ret3 | -0.0071 | 1/3 | +0.0000 | no |
| mtf:tf1_range_pos20 | -0.0074 | 1/3 | +0.0000 | no |
| mtf:bbo_updates_5m | -0.0079 | 2/3 | +0.0000 | no |
| mtf:tf5_ret3 | -0.0091 | 0/3 | +0.0000 | no |
| mtf:tf1_ema9_21 | -0.0091 | 1/3 | +0.0000 | no |
| mtf:bbo_spread_ticks_1m | -0.0092 | 1/3 | +0.0000 | no |
| mtf:dist_pdh_atr | -0.0095 | 1/3 | +0.0000 | no |
| mtf:prev_close_pos | -0.0099 | 0/3 | +0.0000 | no |
| mtf:tick_rate_5m | -0.0099 | 0/3 | +0.0000 | no |
| pre15_range_pts | -0.0101 | 0/3 | +0.0000 | no |
| mtf:tf60_rvol10 | -0.0115 | 1/3 | +0.0000 | no |
| mtf:es_rel_ret_15m_bp | -0.0119 | 1/3 | +0.0000 | no |
| mtf:tf1_atr14 | -0.0148 | 1/3 | +0.0000 | no |
| pre5_volume | -0.0178 | 0/3 | +0.0000 | no |
| mtf:tf60_ema21_slope | -0.0202 | 0/3 | +0.0000 | no |
| mtf:overnight_eff | -0.0274 | 0/3 | +0.0000 | no |
| mtf:tf5_atr14 | -0.0380 | 1/3 | +0.0000 | no |
| mtf:tf5_ret1 | -0.0449 | 0/3 | +0.0000 | no |

## adaptive_risk

*short history: 111989 rows over 25 days (needs ≥ 6 training months for the monthly scheme); exploratory weekly result shown for orientation only*

### adaptive_risk.stop_q80 (primary)

- rows 111989, span 2026-09-21 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 10
- OOS metric (quantile): **1.859** vs baseline 1.779 (does not beat it)
- shuffled-label refits (n=20): min 1.773 · median 1.787 · p95 1.803 · max 1.804 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 1.822 (all features: 1.859)
- per fold: 2026-W41 n=41198 1.794/1.678; 2026-W42 n=38285 1.929/1.887

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| pre15_range_pts | +0.0416 | 3/3 | -0.0022 | **yes** |
| entry_min_after_open | +0.0356 | 2/3 | +0.0019 | **yes** |
| orb_range_pts | +0.0117 | 2/3 | -0.0013 | **yes** |
| overnight_range_pts | +0.0013 | 3/3 | -0.0007 | **yes** |
| is_long | -0.0001 | 2/3 | -0.0002 | **yes** |
| open_gap_pts | -0.0004 | 2/3 | -0.0003 | no |
| pre5_volume | -0.0004 | 2/3 | -0.0014 | **yes** |
| day_range_at_entry_pts | -0.0022 | 2/3 | -0.0019 | no |
| dow | -0.0024 | 0/3 | -0.0007 | no |
| pre5_range_pts | -0.0093 | 1/3 | -0.0001 | no |

### adaptive_risk.mfe

- rows 111989, span 2026-09-21 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 10
- OOS metric (l1): **7.529** vs baseline 7.490 (does not beat it)
- shuffled-label refits (n=20): min 7.480 · median 7.490 · p95 7.503 · max 7.505 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 7.503 (all features: 7.529)
- per fold: 2026-W41 n=41198 6.350/6.285; 2026-W42 n=38285 8.797/8.787
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| overnight_range_pts | +0.0290 | 3/3 | +0.0007 | **yes** |
| pre5_range_pts | +0.0226 | 3/3 | +0.0019 | **yes** |
| pre5_volume | +0.0172 | 1/3 | -0.0001 | no |
| pre15_range_pts | +0.0125 | 1/3 | -0.0021 | no |
| is_long | +0.0119 | 3/3 | -0.0014 | **yes** |
| entry_min_after_open | +0.0077 | 1/3 | +0.0001 | no |
| open_gap_pts | +0.0030 | 2/3 | -0.0001 | **yes** |
| day_range_at_entry_pts | +0.0007 | 2/3 | -0.0005 | **yes** |
| dow | -0.0049 | 1/3 | +0.0003 | no |
| orb_range_pts | -0.0084 | 0/3 | +0.0003 | no |

## breakout_failure

*114 cross events (113 failed) — below the model's own minimum (60 with ≥10 of each)*

## variant_bandit

*Thompson ranking: no supervised label and no features; judging a ranking needs months of forward weeks, and its day_table mixes every paper label (replay labels of the same ids count as pulls)*

## exit_model

*short history: 5211 rows over 25 days (needs ≥ 6 training months for the monthly scheme); exploratory weekly result shown for orientation only*

- paths: 1473
- samples: 5211
### exit_model.p_remaining_positive (primary)

- rows 5211, span 2026-09-21 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 11
- OOS metric (binary): **0.499** vs baseline 0.500 (does not beat it)
- shuffled-label refits (n=20): min 0.415 · median 0.512 · p95 0.547 · max 0.561 → real beats **0.40** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 0.536 (all features: 0.499)
- per fold: 2026-W41 n=1628 0.675/0.500; 2026-W42 n=2396 0.442/0.500
- stall_rule_oos: `{"trades": 1072, "flattened": 266, "pnl_per_trade_usd": -2.216874378731355, "trail_pnl_per_trade_usd": -0.9805467798507461}`

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| mfe | +0.0384 | 3/3 | +0.0035 | **yes** |
| giveback | +0.0121 | 2/3 | +0.0043 | **yes** |
| unreal | +0.0119 | 2/3 | -0.0068 | **yes** |
| sl_points | +0.0033 | 2/3 | -0.0021 | **yes** |
| unreal_over_mfe | +0.0021 | 1/3 | +0.0051 | no |
| age_s | +0.0020 | 1/3 | +0.0110 | no |
| mae | +0.0012 | 3/3 | -0.0037 | **yes** |
| orb_range_pts | -0.0018 | 2/3 | +0.0044 | no |
| is_long | -0.0018 | 1/3 | +0.0012 | no |
| trail_step | -0.0038 | 2/3 | -0.0055 | **yes** |
| pre5_range_pts | -0.0112 | 1/3 | +0.0028 | no |

## regime

*unsupervised k-means (hourly bar features): no label to shuffle or score*

## fill_quality

*short history: 85 rows over 25 days (needs ≥ 6 training months for the monthly scheme); exploratory weekly result shown for orientation only*

### fill_quality.slip_ticks (primary)

- rows 85, span 2026-09-21 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 7
- OOS metric (l1): **0.929** vs baseline 2.878 (beats it)
- shuffled-label refits (n=20): min 1.713 · median 2.864 · p95 3.625 · max 3.738 → real beats **1.00** of them
- verdict: **exploratory** — rule met
- refit on working features only: 0.947 (all features: 0.929)
- per fold: 2026-W41 n=41 0.929/2.878

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| pre5_range_pts | +0.2645 | 3/3 | -0.0079 | **yes** |
| entry_min_after_open | +0.2616 | 3/3 | -0.0744 | **yes** |
| pre5_volume | +0.2568 | 3/3 | +0.0102 | **yes** |
| orb_range_pts | +0.0609 | 3/3 | -0.0597 | **yes** |
| pre5_ticks | +0.0165 | 3/3 | +0.0463 | no |
| is_long | +0.0014 | 2/3 | +0.0284 | no |
| hour_et | +0.0000 | 0/3 | +0.0048 | no |

### fill_quality.fill_ms

- rows 85, span 2026-09-21 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 7
- OOS metric (l1): **97213702354.680** vs baseline 1223355365625.219 (beats it)
- shuffled-label refits (n=20): min 659969568309.973 · median 1251057975002.335 · p95 1403112207346.101 · max 1407589681728.202 → real beats **1.00** of them
- verdict: **exploratory** — rule met
- refit on working features only: 102008022372.767 (all features: 97213702354.680)
- per fold: 2026-W41 n=41 97213702354.680/1223355365625.219
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| entry_min_after_open | +400870299482.1987 | 3/3 | +29578376427.2463 | **yes** |
| orb_range_pts | +201570761900.6759 | 3/3 | +16565295576.8108 | **yes** |
| pre5_range_pts | +18674953323.1070 | 3/3 | +2303798242.0082 | **yes** |
| pre5_volume | +17755864642.8072 | 3/3 | -23840581405.2872 | **yes** |
| pre5_ticks | +13870516645.4433 | 3/3 | +8439954023.4227 | **yes** |
| hour_et | +3611880847.2432 | 3/3 | +19961097457.5590 | no |
| is_long | +1503040351.8413 | 2/3 | +2364714215.9289 | no |

## policy_search

*decision grid with no features or labels — not a supervised model; orientation below*

### orientation (not validated)

```json
{
 "n_paths": 1473,
 "grid": 64,
 "weeks": [
  {
   "week": "2026-W41",
   "n_test": 459,
   "policy": {
    "trail_step": 15.0,
    "be_trigger": 3.0,
    "time_stop_min": null
   },
   "chosen_pnl_per_trade_usd": -0.8050108932461874,
   "actual_pnl_per_trade_usd": -4.4988236862745135,
   "random_policy_pnl_per_trade_usd": {
    "min": -1.593681917211329,
    "median": -1.0392156862745097,
    "max": -0.6143790849673203
   }
  },
  {
   "week": "2026-W42",
   "n_test": 613,
   "policy": {
    "trail_step": 15.0,
    "be_trigger": 3.0,
    "time_stop_min": 10
   },
   "chosen_pnl_per_trade_usd": 0.9836867862969005,
   "actual_pnl_per_trade_usd": 1.6538563197389917,
   "random_policy_pnl_per_trade_usd": {
    "min": -0.8270799347471451,
    "median": 0.5314029363784665,
    "max": 2.400489396411093
   }
  }
 ],
 "caveat": "Replays every trade's real tick path under a grid of trail / break-even / time-stop policies. \u2014 paths truncate at the real exit (the model's own caveat)"
}
```

## sequence

### sequence_gru (primary)

- rows 1503140, span 2022-01-01 → 2026-10-16, scheme **yearly**, folds 3, features 4
- OOS metric (binary): **0.505** vs baseline 0.500 (beats it); accuracy 0.503 vs majority 0.503
- shuffled-label refits (n=5): min 0.494 · median 0.497 · p95 0.500 · max 0.501 → real beats **1.00** of them
- verdict: **PASS**
- refit on working features only: 0.505 (all features: 0.505)
- per fold: 2024 n=351721 0.509/0.500; 2025 n=348913 0.505/0.500; 2026 n=100488 0.507/0.500
- note: reduced protocol (pre-registered): 5 shuffled refits per fold, ≤6 epochs, train capped at the most recent 200k frames, early stopping on the last 10 % of the training frames
- note: time-box: permutation seeds 5 (not 10) and shuffled-importance refits 3 — the smoke run's permutation pass on ~800k test frames took ~8 min at 2 seeds
- note: the training rows' times are synthetic (minute index from 2022-01-01) — only their order matters for the yearly folds; test-row times are the bar times

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| vol_z | +0.0039 | 3/3 | -0.0008 | **yes** |
| ret | +0.0009 | 3/3 | -0.0001 | **yes** |
| range | +0.0004 | 3/3 | -0.0000 | **yes** |
| minute_of_day | +0.0001 | 1/3 | -0.0002 | no |

## exit_optimizer

*decision rule (trigger/step/delay) with no features — not a supervised model; orientation below*

### orientation (not validated)

```json
{
 "n_paths": 598,
 "rules": 70,
 "weeks": [
  {
   "week": "2026-W41",
   "n_test": 198,
   "rule": {
    "trigger": 20.0,
    "step": 15.0,
    "delay": 0
   },
   "chosen_usd_per_trade": -6.621212121212121,
   "live_rule_usd_per_trade": -2.1464646464646466,
   "actual_usd_per_trade": -8.900303030303025,
   "random_rule_usd_per_trade": {
    "min": -12.72979797979798,
    "median": -4.7891414141414135,
    "max": -1.3156565656565657
   }
  },
  {
   "week": "2026-W42",
   "n_test": 254,
   "rule": {
    "trigger": 12.0,
    "step": 15.0,
    "delay": 0
   },
   "chosen_usd_per_trade": 4.344488188976378,
   "live_rule_usd_per_trade": 5.1141732283464565,
   "actual_usd_per_trade": 2.9347540866141735,
   "random_rule_usd_per_trade": {
    "min": -1.6456692913385826,
    "median": 3.125,
    "max": 26.915354330708663
   }
  }
 ],
 "sim_vs_actual_mae_usd": 21.912997428093647
}
```

## signal_meta

*short history: 19801 rows over 23 days (needs ≥ 6 training months for the monthly scheme); exploratory weekly result shown for orientation only*

### signal_meta.p_win (primary)

- rows 19801, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 11
- OOS metric (binary): **0.456** vs baseline 0.500 (does not beat it)
- shuffled-label refits (n=20): min 0.411 · median 0.470 · p95 0.502 · max 0.517 → real beats **0.35** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 0.534 (all features: 0.456)
- per fold: 2026-W41 n=6744 0.433/0.500; 2026-W42 n=6413 0.493/0.500
- gate_backtest_oos: `{"0.5": {"taken": 2880, "of": 13157, "pnl_per_taken": -8.609980227777775, "pnl_per_all": -4.536521427757086}, "0.55": {"taken": 2219, "of": 13157, "pnl_per_taken": -7.449724587201441, "pnl_per_all": -4.536521427757086}, "0.6": {"taken": 1684, "of": 13157, "pnl_per_taken": -7.085215159738715, "pnl_per_all": -4.536521427757086}}`

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_trend | +0.0155 | 2/3 | -0.0009 | **yes** |
| spread_ticks | +0.0087 | 1/3 | +0.0039 | no |
| min_after_open | +0.0051 | 2/3 | +0.0079 | no |
| eng_orb | +0.0030 | 2/3 | +0.0001 | **yes** |
| hour | +0.0012 | 2/3 | -0.0013 | **yes** |
| eng_mtf | +0.0004 | 2/3 | +0.0014 | no |
| imbalance | -0.0004 | 2/3 | +0.0010 | no |
| microprice_dev | -0.0010 | 3/3 | +0.0055 | no |
| is_long | -0.0079 | 1/3 | -0.0005 | no |
| spread_rel | -0.0220 | 0/3 | +0.0046 | no |
| dow | -0.0455 | 1/3 | +0.0028 | no |

## overlay_uplift

*short history: 3612 rows over 23 days (needs ≥ 6 training months for the monthly scheme); exploratory weekly result shown for orientation only*

- pairs_per_overlay: {'rvol15': 10675, 'rg_nochase': 10710, 'rg_trend': 9379, 'rg_range': 9533, 'lv_conf': 5750, 'rg_fade': 8730, 'imb': 3611, 'all': 3612, 'micro': 2805, 'sg': 1397, 'inv': 2447, 'rg_expand': 11357, 'lv_room': 5832, 'rvolq': 462, 'rvol25': 482, 'day_x15': 761, 'am_x15': 572, 'am_inv': 42, 'open_inv': 96, 'inv_nc': 29, 'inv_tr': 26, 'lv_stop': 5871, 'tr25': 13229, 'lv_tgt': 5819, 'bx': 4996, 'btp': 5565, 'bbe': 5119, 'sz': 5383, 'wait': 5270, 'wide': 13, 'open': 766, 'am': 472, 'vt1000': 724, 'tp3': 1021, 'tp2': 1015, 'tp15': 1013, 'tp1': 1008, 'vt2000': 721, 'pm': 66, 'sl1atr': 1, 'am_ts': 108, 'am_chand': 108, 'am_x15b': 28, 'day_x15_m8': 31, 'am_htf': 83, 'rx25_6_15': 280, 'rx25_6_0': 284, 'rx25_4_15': 280, 'rx25_4_0': 284, 'rx15_6_15': 280, 'rx15_6_0': 284, 'rx15_4_15': 280, 'rx15_4_0': 284, 'vixany': 2, 'notcalm': 1}
### overlay_uplift.all (primary)

- rows 3612, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **18.809** vs baseline 21.973 (beats it)
- shuffled-label refits (n=20): min 19.237 · median 19.287 · p95 19.380 · max 19.440 → real beats **1.00** of them
- verdict: **exploratory** — rule met
- refit on working features only: 19.342 (all features: 18.809)
- per fold: 2026-W41 n=946 16.999/20.298; 2026-W42 n=1770 19.776/22.868

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_trend | +2.1542 | 3/3 | +0.0001 | **yes** |
| eng_orb | +2.0271 | 3/3 | +0.0003 | **yes** |
| hour | +0.3283 | 2/3 | -0.0016 | **yes** |
| spread | +0.2272 | 2/3 | -0.0036 | **yes** |
| imbalance | -0.0336 | 1/3 | +0.0001 | no |

### overlay_uplift.am

- rows 472, span 2026-09-28 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **0.381** vs baseline 0.381 (does not beat it)
- shuffled-label refits (n=20): min 0.381 · median 0.381 · p95 0.381 · max 0.381 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 0.381)
- per fold: 2026-W42 n=132 0.381/0.381
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0000 | 0/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.am_x15

- rows 572, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **23.473** vs baseline 16.906 (does not beat it)
- shuffled-label refits (n=20): min 14.834 · median 14.834 · p95 15.909 · max 22.276 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 14.764 (all features: 23.473)
- per fold: 2026-W42 n=208 23.473/16.906
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| hour | +1.4120 | 1/3 | -0.0497 | no |
| imbalance | +0.1209 | 2/3 | -0.1067 | **yes** |
| spread | +0.0000 | 0/3 | +0.0000 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.bbe

- rows 5119, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **15.284** vs baseline 18.411 (beats it)
- shuffled-label refits (n=20): min 14.915 · median 14.915 · p95 14.915 · max 14.915 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 15.284 (all features: 15.284)
- per fold: 2026-W41 n=1546 16.262/19.914; 2026-W42 n=1629 14.357/16.985
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_orb | +3.2092 | 3/3 | +0.0000 | **yes** |
| eng_trend | +2.7843 | 3/3 | +0.0000 | **yes** |
| hour | +0.5980 | 3/3 | +0.0000 | **yes** |
| spread | +0.0481 | 2/3 | +0.0000 | **yes** |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.btp

- rows 5565, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **17.365** vs baseline 19.698 (beats it)
- shuffled-label refits (n=20): min 17.329 · median 17.339 · p95 17.392 · max 17.453 → real beats **0.20** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 17.365 (all features: 17.365)
- per fold: 2026-W41 n=1590 18.577/20.258; 2026-W42 n=1643 16.192/19.157
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_orb | +0.0418 | 3/3 | -0.0007 | **yes** |
| eng_trend | +0.0155 | 2/3 | -0.0003 | **yes** |
| spread | +0.0085 | 2/3 | -0.0004 | **yes** |
| hour | +0.0044 | 2/3 | -0.0007 | **yes** |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.bx

- rows 4996, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **23.035** vs baseline 24.098 (beats it)
- shuffled-label refits (n=20): min 21.585 · median 21.896 · p95 22.169 · max 22.222 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 23.602 (all features: 23.035)
- per fold: 2026-W41 n=1539 23.572/27.264; 2026-W42 n=1618 22.524/21.086
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_orb | +4.9589 | 3/3 | +0.0235 | **yes** |
| eng_trend | +4.7730 | 3/3 | -0.0010 | **yes** |
| hour | +0.5146 | 3/3 | -0.0117 | **yes** |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| spread | -0.0340 | 1/3 | +0.0583 | no |

### overlay_uplift.day_x15

- rows 761, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **27.219** vs baseline 21.002 (does not beat it)
- shuffled-label refits (n=20): min 18.359 · median 18.564 · p95 19.101 · max 25.947 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 27.219)
- per fold: 2026-W42 n=309 27.219/21.002
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.1121 | 1/3 | -0.0052 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |
| hour | -0.0144 | 1/3 | -0.0058 | no |
| imbalance | -0.5219 | 1/3 | +0.0017 | no |

### overlay_uplift.imb

- rows 3611, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **18.977** vs baseline 21.983 (beats it)
- shuffled-label refits (n=20): min 19.281 · median 19.335 · p95 19.377 · max 19.382 → real beats **1.00** of them
- verdict: **exploratory** — rule met
- refit on working features only: 19.090 (all features: 18.977)
- per fold: 2026-W41 n=933 17.066/20.363; 2026-W42 n=1770 19.984/22.836
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_trend | +3.2819 | 3/3 | -0.0014 | **yes** |
| eng_orb | +2.8030 | 3/3 | -0.0004 | **yes** |
| hour | +0.3322 | 2/3 | -0.0027 | **yes** |
| spread | +0.2599 | 2/3 | +0.0044 | **yes** |
| imbalance | -0.1429 | 1/3 | -0.0075 | no |

### overlay_uplift.inv

- rows 2447, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **18.819** vs baseline 22.347 (beats it)
- shuffled-label refits (n=20): min 18.729 · median 18.797 · p95 18.891 · max 19.053 → real beats **0.35** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 18.333 (all features: 18.819)
- per fold: 2026-W41 n=568 22.036/28.895; 2026-W42 n=1100 17.158/18.966
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_orb | +3.3954 | 3/3 | +0.0018 | **yes** |
| eng_trend | +3.1930 | 3/3 | +0.0043 | **yes** |
| imbalance | +0.0079 | 2/3 | -0.0015 | **yes** |
| hour | -0.1271 | 1/3 | +0.0052 | no |
| spread | -0.1430 | 1/3 | +0.0028 | no |

### overlay_uplift.lv_conf

- rows 5750, span 2026-10-11 → 2026-10-16, scheme **weekly** (exploratory), folds 0, features 5
- not validated: no walk-forward fold possible under the weekly scheme

### overlay_uplift.lv_room

- rows 5832, span 2026-10-11 → 2026-10-16, scheme **weekly** (exploratory), folds 0, features 5
- not validated: no walk-forward fold possible under the weekly scheme

### overlay_uplift.lv_stop

- rows 5871, span 2026-10-11 → 2026-10-16, scheme **weekly** (exploratory), folds 0, features 5
- not validated: no walk-forward fold possible under the weekly scheme

### overlay_uplift.lv_tgt

- rows 5819, span 2026-10-11 → 2026-10-16, scheme **weekly** (exploratory), folds 0, features 5
- not validated: no walk-forward fold possible under the weekly scheme

### overlay_uplift.micro

- rows 2805, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **21.705** vs baseline 22.482 (beats it)
- shuffled-label refits (n=20): min 20.265 · median 20.370 · p95 20.454 · max 20.467 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 20.252 (all features: 21.705)
- per fold: 2026-W41 n=591 18.532/24.824; 2026-W42 n=1583 22.890/21.608
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_trend | +0.0061 | 2/3 | +0.0002 | **yes** |
| hour | -0.0006 | 2/3 | +0.0017 | no |
| spread | -0.0199 | 0/3 | +0.0048 | no |
| imbalance | -0.0564 | 1/3 | +0.0031 | no |
| eng_orb | -0.2318 | 0/3 | -0.0003 | no |

### overlay_uplift.open

- rows 766, span 2026-09-28 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **2.692** vs baseline 2.692 (does not beat it)
- shuffled-label refits (n=20): min 2.692 · median 2.692 · p95 2.692 · max 2.692 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 2.692)
- per fold: 2026-W42 n=292 2.692/2.692
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0000 | 0/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.rg_expand

- rows 11357, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **6.478** vs baseline 7.947 (beats it)
- shuffled-label refits (n=20): min 6.314 · median 6.314 · p95 6.314 · max 6.314 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 6.257 (all features: 6.478)
- per fold: 2026-W42 n=5235 6.478/7.947
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_orb | +0.7060 | 2/3 | +0.0000 | **yes** |
| eng_trend | +0.4188 | 2/3 | +0.0000 | **yes** |
| imbalance | +0.0597 | 2/3 | +0.0000 | **yes** |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| spread | -0.1256 | 0/3 | +0.0000 | no |

### overlay_uplift.rg_fade

- rows 8730, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **18.722** vs baseline 20.651 (beats it)
- shuffled-label refits (n=20): min 18.087 · median 18.188 · p95 18.744 · max 20.937 → real beats **0.05** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 17.981 (all features: 18.722)
- per fold: 2026-W42 n=5142 18.722/20.651
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_orb | +0.9725 | 2/3 | +0.0001 | **yes** |
| eng_trend | +0.7228 | 2/3 | +0.0005 | **yes** |
| imbalance | -0.0143 | 1/3 | +0.0237 | no |
| hour | -0.2076 | 0/3 | +0.0584 | no |
| spread | -0.2773 | 0/3 | +0.0278 | no |

### overlay_uplift.rg_nochase

- rows 10710, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **11.795** vs baseline 12.914 (beats it)
- shuffled-label refits (n=20): min 12.071 · median 12.071 · p95 12.071 · max 12.071 → real beats **1.00** of them
- verdict: **exploratory** — rule met
- refit on working features only: 11.763 (all features: 11.795)
- per fold: 2026-W42 n=5183 11.795/12.914
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| imbalance | +0.2570 | 3/3 | +0.0000 | **yes** |
| spread | +0.0544 | 2/3 | +0.0000 | **yes** |
| eng_orb | +0.0239 | 2/3 | +0.0000 | **yes** |
| eng_trend | +0.0065 | 1/3 | -0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.rg_range

- rows 9533, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **17.061** vs baseline 18.686 (beats it)
- shuffled-label refits (n=20): min 16.787 · median 16.790 · p95 16.836 · max 16.890 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 17.077 (all features: 17.061)
- per fold: 2026-W42 n=5144 17.061/18.686
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| imbalance | +0.4101 | 3/3 | +0.0002 | **yes** |
| eng_orb | +0.3135 | 2/3 | -0.0006 | **yes** |
| eng_trend | +0.0896 | 2/3 | +0.0000 | **yes** |
| spread | +0.0651 | 2/3 | -0.0001 | **yes** |
| hour | +0.0115 | 1/3 | +0.0004 | no |

### overlay_uplift.rg_trend

- rows 9379, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **14.950** vs baseline 16.550 (beats it)
- shuffled-label refits (n=20): min 14.456 · median 14.478 · p95 14.544 · max 14.727 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 14.478 (all features: 14.950)
- per fold: 2026-W42 n=5182 14.950/16.550
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_orb | +0.2104 | 2/3 | +0.0000 | **yes** |
| eng_trend | +0.1479 | 2/3 | +0.0000 | **yes** |
| imbalance | -0.0895 | 1/3 | -0.0001 | no |
| hour | -0.2311 | 0/3 | +0.0097 | no |
| spread | -0.3836 | 0/3 | +0.0007 | no |

### overlay_uplift.rvol15

- rows 10675, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **14.080** vs baseline 15.668 (beats it)
- shuffled-label refits (n=20): min 13.956 · median 13.959 · p95 13.972 · max 13.978 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 14.154 (all features: 14.080)
- per fold: 2026-W42 n=5785 14.080/15.668
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| imbalance | +0.4341 | 3/3 | -0.0004 | **yes** |
| eng_orb | +0.2176 | 2/3 | +0.0000 | **yes** |
| hour | +0.0004 | 1/3 | +0.0026 | no |
| spread | +0.0000 | 0/3 | +0.0003 | no |
| eng_trend | -0.0435 | 1/3 | +0.0001 | no |

### overlay_uplift.rvol25

- rows 482, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **13.696** vs baseline 16.408 (beats it)
- shuffled-label refits (n=20): min 13.516 · median 13.671 · p95 13.950 · max 13.960 → real beats **0.35** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 13.696 (all features: 13.696)
- per fold: 2026-W42 n=239 13.696/16.408
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| imbalance | +0.1524 | 2/3 | -0.0371 | **yes** |
| spread | +0.0000 | 0/3 | -0.0059 | no |
| hour | +0.0000 | 0/3 | -0.0130 | no |
| eng_orb | +0.0000 | 0/3 | -0.0003 | no |
| eng_trend | +0.0000 | 0/3 | +0.0034 | no |

### overlay_uplift.rvolq

- rows 462, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **14.661** vs baseline 16.897 (beats it)
- shuffled-label refits (n=20): min 14.273 · median 14.275 · p95 14.362 · max 14.577 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: 14.661 (all features: 14.661)
- per fold: 2026-W42 n=241 14.661/16.897
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| imbalance | +0.0574 | 2/3 | -0.0041 | **yes** |
| spread | +0.0000 | 0/3 | -0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0097 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0008 | no |

### overlay_uplift.sg

- rows 1397, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **16.483** vs baseline 26.084 (beats it)
- shuffled-label refits (n=20): min 15.717 · median 15.752 · p95 26.842 · max 31.598 → real beats **0.15** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 16.483)
- per fold: 2026-W41 n=61 23.068/35.634; 2026-W42 n=1298 16.173/25.635
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0000 | 0/3 | -0.0057 | no |
| hour | +0.0000 | 0/3 | -0.0677 | no |
| eng_orb | +0.0000 | 0/3 | +0.2651 | no |
| eng_trend | +0.0000 | 0/3 | -0.2084 | no |
| imbalance | -0.0377 | 2/3 | +0.0015 | no |

### overlay_uplift.sz

- rows 5383, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **3.969** vs baseline 5.323 (beats it)
- shuffled-label refits (n=20): min 3.969 · median 3.969 · p95 3.969 · max 3.969 → real beats **0.05** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 3.969)
- per fold: 2026-W41 n=1679 5.154/6.373; 2026-W42 n=1673 2.779/4.268
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0000 | 0/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.tp1

- rows 1008, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **10.519** vs baseline 12.514 (beats it)
- shuffled-label refits (n=20): min 10.519 · median 10.519 · p95 10.519 · max 10.519 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 10.519)
- per fold: 2026-W42 n=362 10.519/12.514
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0000 | 0/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.tp15

- rows 1013, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **9.404** vs baseline 11.144 (beats it)
- shuffled-label refits (n=20): min 9.404 · median 9.404 · p95 9.404 · max 9.404 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 9.404)
- per fold: 2026-W42 n=363 9.404/11.144
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0000 | 0/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.tp2

- rows 1015, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **8.152** vs baseline 9.884 (beats it)
- shuffled-label refits (n=20): min 8.152 · median 8.152 · p95 8.152 · max 8.152 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 8.152)
- per fold: 2026-W42 n=363 8.152/9.884
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0000 | 0/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.tp3

- rows 1021, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **6.767** vs baseline 7.416 (beats it)
- shuffled-label refits (n=20): min 6.767 · median 6.767 · p95 6.767 · max 6.767 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 6.767)
- per fold: 2026-W42 n=363 6.767/7.416
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0000 | 0/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.tr25

- rows 13229, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **3.418** vs baseline 4.005 (beats it)
- shuffled-label refits (n=20): min 3.418 · median 3.418 · p95 3.418 · max 3.418 → real beats **0.00** of them
- verdict: **exploratory** — rule not met
- refit on working features only: — (all features: 3.418)
- per fold: 2026-W42 n=5409 3.418/4.005
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0000 | 0/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| eng_orb | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

### overlay_uplift.vt1000

- rows 724, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **14.286** vs baseline 16.563 (beats it)
- shuffled-label refits (n=20): min 14.328 · median 14.328 · p95 14.328 · max 14.331 → real beats **1.00** of them
- verdict: **exploratory** — rule met
- refit on working features only: 14.286 (all features: 14.286)
- per fold: 2026-W42 n=252 14.286/16.563
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_trend | +0.2933 | 3/3 | -0.0000 | **yes** |
| eng_orb | +0.2462 | 3/3 | +0.0000 | **yes** |
| spread | +0.0000 | 0/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | -0.0008 | no |

### overlay_uplift.vt2000

- rows 721, span 2026-09-29 → 2026-10-16, scheme **weekly** (exploratory), folds 1, features 5
- OOS metric (l1): **32.192** vs baseline 36.066 (beats it)
- shuffled-label refits (n=20): min 32.277 · median 32.277 · p95 32.277 · max 32.277 → real beats **1.00** of them
- verdict: **exploratory** — rule met
- refit on working features only: 32.192 (all features: 32.192)
- per fold: 2026-W42 n=249 32.192/36.066
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| eng_trend | +0.5958 | 3/3 | +0.0001 | **yes** |
| eng_orb | +0.5322 | 3/3 | +0.0000 | **yes** |
| spread | +0.0000 | 0/3 | -0.0002 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0001 | no |

### overlay_uplift.wait

- rows 5270, span 2026-09-23 → 2026-10-16, scheme **weekly** (exploratory), folds 2, features 5
- OOS metric (l1): **3.616** vs baseline 4.591 (beats it)
- shuffled-label refits (n=20): min 3.618 · median 3.618 · p95 3.618 · max 3.618 → real beats **1.00** of them
- verdict: **exploratory** — rule met
- refit on working features only: 3.618 (all features: 3.616)
- per fold: 2026-W41 n=1645 2.730/4.039; 2026-W42 n=1658 4.495/5.138
- note: secondary task: permutation seeds 5, shuffled-importance refits 3 (time-box)

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread | +0.0031 | 2/3 | +0.0000 | **yes** |
| eng_orb | +0.0008 | 1/3 | +0.0000 | no |
| imbalance | +0.0000 | 0/3 | +0.0000 | no |
| hour | +0.0000 | 0/3 | +0.0000 | no |
| eng_trend | +0.0000 | 0/3 | +0.0000 | no |

## fill_cost

*short history: 1215737 rows over 40 days (needs ≥ 6 training months for the monthly scheme); exploratory weekly result shown for orientation only*

- rows_without_spread: 451496
- labels: every non-audit label in range, as the model's dataset() selects
### fill_cost.cost_usd (primary)

- rows 1215737, span 2026-09-08 → 2026-10-18, scheme **weekly** (exploratory), folds 4, features 14
- OOS metric (l1): **0.880** vs baseline 0.885 (beats it)
- shuffled-label refits (n=20): min 0.892 · median 0.892 · p95 0.892 · max 0.892 → real beats **1.00** of them
- verdict: **exploratory** — rule met
- refit on working features only: 0.833 (all features: 0.880)
- per fold: 2026-W39 n=283175 0.825/0.825; 2026-W40 n=245024 1.136/1.312; 2026-W41 n=200003 1.167/1.056; 2026-W42 n=268774 0.491/0.430

| feature | importance | thirds (+) | shuffled imp. | works |
|---|---|---|---|---|
| spread_entry | +0.0496 | 3/3 | +0.0000 | **yes** |
| spread_exit | +0.0335 | 3/3 | +0.0000 | **yes** |
| hold_min | +0.0000 | 1/3 | +0.0000 | no |
| hour | +0.0000 | 2/3 | +0.0000 | **yes** |
| bbo_spread_1m | +0.0000 | 0/3 | +0.0000 | no |
| bbo_imbalance_1m | +0.0000 | 0/3 | +0.0000 | no |
| bbo_micro_dev | +0.0000 | 0/3 | +0.0000 | no |
| bbo_updates_5m | +0.0000 | 0/3 | +0.0000 | no |
| bbo_spread_rel_1h | +0.0000 | 0/3 | +0.0000 | no |
| eng_mtf | +0.0000 | 0/3 | +0.0000 | no |
| is_long | -0.0008 | 1/3 | +0.0000 | no |
| min_after_open | -0.0017 | 0/3 | +0.0000 | no |
| eng_trend | -0.0047 | 2/3 | +0.0000 | no |
| eng_orb | -0.0053 | 2/3 | +0.0000 | no |
