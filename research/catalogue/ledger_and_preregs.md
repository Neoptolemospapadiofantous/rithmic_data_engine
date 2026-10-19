# A — Research ledger + pre-registrations catalogue (rithmic_engine, as of 2026-10-19)

Sources: `research/README.md` (ledger sections for 2026-10-17 and 2026-10-18/19) and all 118 files in `research/preregistered/*.md`
(2026-10-16 … 2026-10-19). One row per separately-judged test. Points are NQ points per trade unless stated; "WF" = edge_lab
walk-forward; eras usually 2010–18 / 2019–22(24) / 2023–26. Verdict = the verdict written in the pre-registration file
(ADOPTED / NOT ADOPTED where the file applies an adoption rule; PASS / FAIL where it is a gate verdict; OPEN = sent to forward/paper
record as a hypothesis; DESCRIPTION = no pass/fail rule; NO RESULT = no result recorded in the file).

| id | date | family | question (≤ 15 words) | verdict | key numbers | source file |
|---|---|---|---|---|---|---|
| 16-01 | 2026-10-16 | overnight/sessions | Does the cascade work in the first 2 h of Asia / Tokyo / London / NY? | FAIL | asia_2h passes gates (+4.32, 304) but not validated (holdout −1.47/−0.64); others fail; NY 11:30 exit +2.17 FAIL controls | 2026-10-16_cascade_all_sessions.md |
| 16-02a | 2026-10-16 | direction/NY-open | Cascade re-entry (≤ 2 trades/day) better than base? | FAIL | +3.62 vs base +4.80 pts/trade, 3,511 trades | 2026-10-16_cascade_refinements.md |
| 16-02b | 2026-10-16 | exits/stops | Cascade pyramid (add a unit at +1R)? | FAIL | +4.62/trade, random-day control 76 % | 2026-10-16_cascade_refinements.md |
| 16-02c | 2026-10-16 | exits/stops | Cascade trail after +1R instead of 2R target? | FAIL | +2.56 vs +4.80 | 2026-10-16_cascade_refinements.md |
| 16-02d | 2026-10-16 | direction/NY-open | First cascade check at 09:45 / 09:55 / 10:00 instead of 09:50? | FAIL | +3.57 / +4.42 / +3.98, all positive; 09:50 stays | 2026-10-16_cascade_refinements.md |
| 16-02e | 2026-10-16 | sizing/risk/prop-sim | 2 units on calm (VIX < 20) days for the cascade? | FAIL | per unit +1.02/+7.42/+10.29 vs flat +1.04/+7.70/+8.56; better only 2023–26 | 2026-10-16_cascade_refinements.md |
| 16-03a | 2026-10-16 | cross-market (ES/VIX/macro) | Does cascade_gap_agree replicate on ES? | PASS | +1.97 ES pts/trade (854, t 3.69) vs ES cascade +1.10; eras all positive | 2026-10-16_confirm_gap.md |
| 16-03b | 2026-10-16 | calendar/news | Fade the move on options-expiry Fridays (pinning)? | FAIL | screen: 26 trades, −2.48 pts/trade | 2026-10-16_confirm_gap.md |
| 16-03c | 2026-10-16 | calendar/news | Skip opex days in the cascade? | NOT ADOPTED | +4.50 without opex; opex days alone +16.36 on 41 | 2026-10-16_confirm_gap.md |
| 16-03d | 2026-10-16 | exits/stops | VWAP thesis exit on cascade_gap_agree? | ADOPTED | PASS: +10.05 vs +8.59, max DD 508 vs 1,152 → paper so_cascade_2r_gapagree_vwap | 2026-10-16_confirm_gap.md |
| 16-03e | 2026-10-16 | sizing/risk/prop-sim | Double size on gap-agree days speeds Flex/Daily 50K payout? | NOT ADOPTED | Flex 17.8 vs 16.0 wks (slower); Daily ~2–4 wks faster but +1 eval | 2026-10-16_confirm_gap.md |
| 16-04 | 2026-10-16 | calendar/news | Cascade stronger on big-tech earnings reaction mornings? | FAIL | +7.04 on 217 (t 1.2), controls 74/33/63 %; CPI not testable | 2026-10-16_earnings_mornings.md |
| 16-05a | 2026-10-16 | cross-market (ES/VIX/macro) | Cascade only when VVIX ≥ 100? | FAIL | +9.35 on 816 vs complement +2.51; FAIL controls; sizing hypothesis only | 2026-10-16_free_macro_filters.md |
| 16-05b | 2026-10-16 | cross-market (ES/VIX/macro) | Cascade only after a ≥ 7 bp 10-year yield move? | FAIL | +5.56 on 371 vs complement +4.91; FAIL controls | 2026-10-16_free_macro_filters.md |
| 16-05c | 2026-10-16 | cross-market (ES/VIX/macro) | Weak-day long only when the 10-year yield fell? | NOT ADOPTED | PASS gates +29.80 on 209, but complement +27.49 | 2026-10-16_free_macro_filters.md |
| 16-05d | 2026-10-16 | cross-market (ES/VIX/macro) | Weak-day long only after dollar ≥ +0.3 %? | FAIL | +16.25 on 65 vs complement +22.36; FAIL controls | 2026-10-16_free_macro_filters.md |
| 16-06 | 2026-10-16 | pipeline/validation | Frozen-setting holdout of every winner (choose 2010–18/2010–22, test later)? | DESCRIPTION | so_cascade holdout +6.89 / +10.03 (t ≈ 2.4); weak_day +21.22 / +67.15; ema_cross_vwap no dev edge | 2026-10-16_frozen_holdout.md |
| 16-07 | 2026-10-16 | order-flow | 10 more liquidity boxes × sweep/break with order flow (2023–26)? | FAIL | nothing passes; best 100-pt band break +12.4 (333, dir 99 %), Asia sweep +10.4 (dir 96 %) | 2026-10-16_liquidity_boxes_all.md |
| 16-08 | 2026-10-16 | order-flow | ORB15 / overnight / prior-day box sweep or break with order flow? | FAIL | all 6 fail (−6.14 … +3.57); price-only 2010–26 −2.5 … +2.4 | 2026-10-16_liquidity_boxes.md |
| 16-09a | 2026-10-16 | pipeline/validation | 2,800-variant cascade grid beats the current setting in both frozen holdouts? | NOT ADOPTED | WF +4.08 (FAIL controls) vs 54-variant +4.80; holdout B pick +7.64 < current +8.66; every variant positive | 2026-10-16_mega_grid.md |
| 16-09b | 2026-10-16 | mean-reversion | Weak-day grid (IBS/VIX/stop/target, exits 12/14/15:55) beats current setting? | NOT ADOPTED | 15:55 WF +8.05 PASS; pick VIX ≥ 25 better/trade but less total; 12:00/14:00 FAIL | 2026-10-16_mega_grid.md |
| 16-10a | 2026-10-16 | cross-market (ES/VIX/macro) | Cascade only when ES agrees? | FAIL | +4.35 vs removed +4.94 | 2026-10-16_new_angles.md |
| 16-10b | 2026-10-16 | direction/NY-open | Cascade only when RTH rvol ≥ 1.2? | FAIL | +4.55 vs removed +4.92 | 2026-10-16_new_angles.md |
| 16-10c | 2026-10-16 | direction/NY-open | Cascade only when open move agrees with the gap (cascade_gap_agree)? | PASS | +8.59 (897); holdouts +16.35 / +14.32; complement +3.35 → paper sizing candidate, not a filter | 2026-10-16_new_angles.md |
| 16-10d | 2026-10-16 | direction/NY-open | Cascade only when yesterday closed at the extreme in trade direction? | FAIL | +1.20; complement passes +7.49 (opposite of prediction) → paper so_cascade_2r_notext | 2026-10-16_new_angles.md |
| 16-10e | 2026-10-16 | overnight/sessions | Afternoon drive from 12:00 anchor (pm_drive)? | FAIL | −0.65 pts/trade | 2026-10-16_new_angles.md |
| 16-10f | 2026-10-16 | mean-reversion | 14:00 exhaustion fade of > 1 ATR days? | FAIL | +4.49 on 125, FAIL controls | 2026-10-16_new_angles.md |
| 16-11a | 2026-10-16 | order-flow | Flow-flip exit on orb5_of / orb15_of / so_cascade? | FAIL | +6.52/+5.99/+4.87 vs +5.49/+4.09/+4.80; fired ≈ 20 trades, one era | 2026-10-16_orb_order_flow.md |
| 16-11b | 2026-10-16 | order-flow | ORB-5/15 break only when range-window flow agrees? | FAIL | +7.28 (404) / +6.37 (350); controls 96/96/82 % and 86/85/76 % | 2026-10-16_orb_order_flow.md |
| 16-11c | 2026-10-16 | order-flow | ORB-5 break after resting liquidity pulled on far side? | FAIL | +6.52 (400), every year positive; controls 96/58/70 %; L2 not entitled live | 2026-10-16_orb_order_flow.md |
| 16-11d | 2026-10-16 | order-flow | Absorption fade at the 15-min range edge? | FAIL | −2.34 on 69, screen | 2026-10-16_orb_order_flow.md |
| 16-11e | 2026-10-16 | models/ML | Cascade meta model with flow/book inputs beats plain meta? | FAIL | +7.98 vs +8.22; shuffled labels 92 % | 2026-10-16_orb_order_flow.md |
| 16-12a | 2026-10-16 | cross-market (ES/VIX/macro) | CTA trigger: close crossing 50/100/200-day MA, enter 18:00 or 09:30? | FAIL | 18:00 +7.37 (214) FAIL controls; 09:30 −1.11 FAIL screen | 2026-10-16_owned_data_ideas.md |
| 16-12b | 2026-10-16 | direction/NY-open | Reverse a failed (stopped) 09:50 cascade signal? | FAIL | +5.47 on 255, direction 92 %, 2023–26 −5.05 | 2026-10-16_owned_data_ideas.md |
| 16-12c | 2026-10-16 | cross-market (ES/VIX/macro) | Trade NQ toward ES on intraday NQ–ES divergence? | FAIL | −2.44 on 343, screen | 2026-10-16_owned_data_ideas.md |
| 16-12d | 2026-10-16 | cross-market (ES/VIX/macro) | BTC weekend move predicts NQ Monday / Sunday night? | FAIL | Monday +8.09 (86) FAIL controls; Sunday −3.05 FAIL WF | 2026-10-16_owned_data_ideas.md |
| 16-12e | 2026-10-16 | other | The NQ cascade on BTC / ETH 1m (10 bp cost)? | FAIL | −2.93 / −3.11 bp, FAIL walk-forward | 2026-10-16_owned_data_ideas.md |
| 16-12f | 2026-10-16 | sizing/risk/prop-sim | 4 MNQ on high-ATR days (vol_regime_sizing) in payout sim? | FAIL | payout months 26 % vs fixed 2 MNQ 49 %; evals/yr 2.3 vs 0.4 | 2026-10-16_owned_data_ideas.md |
| 16-13a | 2026-10-16 | price-action/patterns | 9 candle/chart patterns standalone and in cascade direction? | FAIL | pat_3bar (+2.51) and pat_double_casc (+3.15) pass gates but not validated in holdout; rest fail | 2026-10-16_patterns_and_conditions.md |
| 16-13b | 2026-10-16 | pipeline/validation | Condition matrix: 4 families × 7 conditions (VIX, VVIX, SMA50, FOMC)? | FAIL | no combination validated; plain cascade fails 99.8 % bar (day 96 %); VIX<20 cascade best-looking | 2026-10-16_patterns_and_conditions.md |
| 16-14a | 2026-10-16 | direction/NY-open | Is cascade_gap_agree robust to gap threshold 0.05–0.3 ATR? | PASS | +8.97 / +8.59 / +9.10 / +7.47, all > +4.80 ("REAL") | 2026-10-16_round3.md |
| 16-14b | 2026-10-16 | overnight/sessions | Cascade only when the open move broke the overnight range? | FAIL | +4.58, complement +4.96 | 2026-10-16_round3.md |
| 16-14c | 2026-10-16 | mean-reversion | Weak-day long only after gap down (or gap up)? | FAIL | gap-down +13.98 (208), gap-up +5.56, both FAIL controls | 2026-10-16_round3.md |
| 16-14d | 2026-10-16 | exits/stops | Limit entries 1 tick better (paper forward twin)? | OPEN | paper twin so_cascade_2r_lim1 from 10-19; ≥ 40 signals rule | 2026-10-16_round3.md |
| 16-14e | 2026-10-16 | sizing/risk/prop-sim | Account mix: 1 Flex vs 2 Flex vs Flex+Daily (resampled)? | DESCRIPTION | 2 accounts double money not frequency; Flex+Daily 4.5 paid months/yr, $7,106 | 2026-10-16_round3.md |
| 16-14f | 2026-10-16 | sizing/risk/prop-sim | Worst case of cascade + VWAP/stall at 8 MNQ? | DESCRIPTION | max DD $15,547; worst day −$2,882; $2k DD ~12×/yr | 2026-10-16_round3.md |
| 16-15a | 2026-10-16 | cross-market (ES/VIX/macro) | Cascade only in VIX backwardation (vts ≥ 1)? | FAIL | −1.75 on 144, FAIL WF — hypothesis backwards | 2026-10-16_vix_term_structure.md |
| 16-15b | 2026-10-16 | cross-market (ES/VIX/macro) | Cascade only in contango (vts < 1)? | FAIL | +5.13 vs +4.80; random day 97 % (need 99) | 2026-10-16_vix_term_structure.md |
| 16-15c | 2026-10-16 | cross-market (ES/VIX/macro) | Weak-day long gated by term structure instead of VIX ≥ 20? | FAIL | +25.59 on 222, direction 94 %; 2024 −912 | 2026-10-16_vix_term_structure.md |
| 16-16a | 2026-10-16 | VWAP | VWAP hold on trend days / VWAP reclaim? | FAIL | −0.21 (716) FAIL WF / −0.52 (1,736) FAIL screen | 2026-10-16_vwap_ema.md |
| 16-16b | 2026-10-16 | EMA/indicators | 5-min EMA20 pullback with 60-min trend (ema_pullback_htf)? | FAIL | +2.12 on 1,669, FAIL controls | 2026-10-16_vwap_ema.md |
| 16-16c | 2026-10-16 | EMA/indicators | EMA9×21 cross in day direction (ema_cross_vwap)? | PASS | +6.79 (956) PASS gates but NOT validated by frozen holdout (no edge pre-2019) | 2026-10-16_vwap_ema.md |
| 16-16d | 2026-10-16 | EMA/indicators | Cascade only when 60-min EMA trend agrees? | FAIL | +3.67 < +4.80, FAIL controls | 2026-10-16_vwap_ema.md |
| 16-17a | 2026-10-16 | exits/stops | Thesis exits make the weak-day long prop-safe? | FAIL | vwap +1.36, open +2.31 vs base +13.20; stall +14.01 timing 97 % | 2026-10-16_weak_day_fixes.md |
| 16-17b | 2026-10-16 | models/ML | Meta take/skip on the weak-day long? | FAIL | +20.19 vs +19.02; random skip 62 %, shuffled 79 % | 2026-10-16_weak_day_fixes.md |
| 17-01a | 2026-10-17 | price-action/patterns | Break → box → go at OR15 / prior day / overnight levels? | FAIL | bb_or +0.73 (281) controls, bb_pd +0.31 screen, bb_on +2.87 controls | 2026-10-17_break_box.md |
| 17-01b | 2026-10-17 | price-action/patterns | Break → box → go only on the gap's side? | PASS | bb_on_gap +7.31 (127, PF 1.53); holdout 22 trades +8.02 t 0.4 → forward candidate (1 of 6) | 2026-10-17_break_box.md |
| 17-02 | 2026-10-17 | sizing/risk/prop-sim | Stop trading for the day at a $ profit lock (25K plans)? | NOT ADOPTED | NQ 2 micros $8,044/yr → $100 lock $1,521, $500 lock $6,840 | 2026-10-17_daily_lock.md |
| 17-03 | 2026-10-17 | direction/NY-open | Does a 12:00 "any move" cascade check let it trade every day? | NO RESULT | no Results in file; data/edge_lab/reports/cascade_noon.md says PASS; 12:00-trade judgement (2) not recorded | 2026-10-17_daily_trade.md |
| 17-04 | 2026-10-17 | pipeline/validation | Day-engine map + pre-registered T1–T5 (meta+flow, EMA14, 2nd-leg box, trade cap, full engine) | NO RESULT | §1 is a map of earlier results; T1–T5 have no results in this file | 2026-10-17_day_engine_plan.md |
| 17-05 | 2026-10-17 | direction/NY-open | After a 10:00 direction, do fleet-style entries beat a market entry? | NO RESULT | no Results in file; reports: dir_market PASS, the 6 entries FAIL (controls/screen) — cited in day_engine_plan | 2026-10-17_direction_first.md |
| 17-06a | 2026-10-17 | direction/NY-open | Gap direction + fleet entries (breakout / OR retest) beat gap market entry? | PASS | gap_or_retest +9.03 (1,220), gap_breakout +7.43; holdout retest +7.15 t 0.6, breakout +1.01 collapsed | 2026-10-17_direction_sources.md |
| 17-06b | 2026-10-17 | mean-reversion | Weak-day direction without VIX + fleet entries? | FAIL | every entry fails; wk_market +1.92, 2010–18 −5.5 | 2026-10-17_direction_sources.md |
| 17-07 | 2026-10-17 | EMA/indicators | EMA-14 hand trade: 4 entries × 3 stops × 8 exits? | FAIL | all 96 negative in R; WF −0.028 R; holdout −0.001 R | 2026-10-17_ema_entries_trails.md |
| 17-08a | 2026-10-17 | EMA/indicators | Direction source / permission for hand-traded EMA-14 pullbacks? | ADOPTED | plan dir +0.78 vs random bar +1.01; VWAP+open side +1.72 vs −1.11 → Plan page permission; full rule ≈ 0.03 R | 2026-10-17_ema_permission.md |
| 17-08b | 2026-10-17 | direction/NY-open | Any condition makes the live ORB rules positive (2010–24)? | FAIL | −1.23 pts/trade; best split gap agrees −0.59 | 2026-10-17_ema_permission.md |
| 17-08c | 2026-10-17 | overnight/sessions | Any condition makes mtf_london_v5 positive? | FAIL | ≈ −3.3 … −4.7 after commission; forward 133 trades −1.48 | 2026-10-17_ema_permission.md |
| 17-09 | 2026-10-17 | exits/stops | Smaller / structural stops (swing, pivot, EMA, bar low) for EMA-14 trade? | FAIL | all 125 negative in R; WF −0.031 R; holdout −0.038 R | 2026-10-17_ema_stops.md |
| 17-10 | 2026-10-17 | EMA/indicators | EMA length × timeframe × pullback × trend/momentum filters (2,880)? | FAIL | WF +0.021 R, negative every year 2013–18; holdout +0.006 R | 2026-10-17_ema_sweep.md |
| 17-11 | 2026-10-17 | behaviour (descriptive) | Fleet: are entries or exits the problem? (not pre-registered) | DESCRIPTION | own exits −0.04 before cost; open-direction twin beats fleet entries; hold to 15:55 +4.69 vs +7.22 | 2026-10-17_fleet_entries_exits.md |
| 17-12 | 2026-10-17 | models/ML | Does fleet consensus (many roots same side) carry an edge? | FAIL | best of 192 cells t 1.85 (bar 3.3); RTH follow +4.70; fades negative | 2026-10-17_fleet_consensus.md |
| 17-13 | 2026-10-17 | sizing/risk/prop-sim | Can the 0.5R sprint be traded by hand (late entry, worse fill, skips, news, BE, 2nd chance)? | FAIL | nothing meets rule; 1 min late → 0 R; 5 pts worse fill −0.079 R, payout ≤ 20 d 24.6 → 10.1 % | 2026-10-17_manual_execution.md |
| 17-14 | 2026-10-17 | EMA/indicators | Momentum definitions × entries (90 cells) — replace mom_dir_30_ema20? | NOT ADOPTED | WF +0.008 R vs current +0.079; M-EFF45 × VWAP +0.118 R all eras → paper candidates | 2026-10-17_momentum_entries.md |
| 17-15 | 2026-10-17 | EMA/indicators | 12,000-setting momentum+EMA optimisation (walk-forward, plateau, ES)? | NOT ADOPTED | WF +0.168 vs base +0.079 R, but neighbourhood +0.072 < +0.079; ES −0.042 | 2026-10-17_momentum_optimize.md |
| 17-16a | 2026-10-17 | cross-market (ES/VIX/macro) | Does the frozen 1-min momentum rule replicate on ES? | FAIL | ES any-bar −0.062 R (−0.114 / +0.008 / +0.028) | 2026-10-17_momentum_round2.md |
| 17-16b | 2026-10-17 | overnight/sessions | 1-min momentum rule in London / Asia (NQ, ES)? | FAIL | NQ London −0.11, Asia −0.13; ES −0.14 / −0.16 R | 2026-10-17_momentum_round2.md |
| 17-16c | 2026-10-17 | exits/stops | Momentum exits (BE levels, 2R, trails, time)? | NOT ADOPTED | WF picked frozen BE +1R hold every year (+0.060 vs +0.060) | 2026-10-17_momentum_round2.md |
| 17-16d | 2026-10-17 | behaviour (descriptive) | Momentum splits by time / plan agreement; engine replay all years? | DESCRIPTION | ≤ 11:30 +0.043 vs later 0; engine 2010-18 −$1.33 / −$1.67 per MNQ trade | 2026-10-17_momentum_round2.md |
| 17-17a | 2026-10-17 | cross-market (ES/VIX/macro) | Weak-day long rule on ES? | PASS | +3.38 ES pts/trade (690), eras −0.18 / +5.05 / +9.71 | 2026-10-17_more_trades.md |
| 17-17b | 2026-10-17 | sizing/risk/prop-sim | Take both cascade and gap retest when they agree (25K plans)? | ADOPTED | NQ: Flex paid $2,781 → $3,957/$3,492, blown 53 → 46/49 %; NQ+ES not adopted | 2026-10-17_more_trades.md |
| 17-18 | 2026-10-17 | direction/NY-open | 1-min direction source + 5-min entry (48 cells)? | OPEN | D-MOM + EMA-20 +0.082 R (only cell meeting rule); random-bar twin +0.04–0.05 → forward-paper candidate, not a rule | 2026-10-17_mtf_1m_dir_5m_entry.md |
| 17-19 | 2026-10-17 | mean-reversion | Fades on no-direction days (range / VWAP / open)? | FAIL | all six FAIL screen: −1.96 / −4.27 / −6.52; every era negative | 2026-10-17_no_direction_mr.md |
| 17-20 | 2026-10-17 | EMA/indicators | Opening regime (5/15/30 min) tells when the EMA-14 trade works? | FAIL | 3 of 66 cells all-era positive (< chance); narrow-OR15 WF +0.130 R, holdout −0.305 R (37) | 2026-10-17_open_regime_ema.md |
| 17-21 | 2026-10-17 | direction/NY-open | Re-entries (2nd/3rd trade in the day's direction) after any exit / after a win? | ADOPTED | after a win 2nd/3rd +5.66 vs twin −0.82; per day +5.0 → +5.2; eval pass 37 → 33 % — optional plan rule | 2026-10-17_reentry.md |
| 17-22 | 2026-10-17 | models/ML | Walk-forward regime optimisation, 9,408 rules over NY/London/Asia? | FAIL | NY top-10 +3.34 vs baseline +1.59 but 2013–18 negative; London/Asia negative | 2026-10-17_regime_optimize.md |
| 17-23 | 2026-10-17 | EMA/indicators | 1-min tape regime switches EMA pullback (trend) / VWAP fade (revert) per session? | FAIL | all 9 FAIL; NY gated −0.08 vs ungated +0.08 | 2026-10-17_regime_switch.md |
| 17-24 | 2026-10-17 | exits/stops | Runner exit (vwap_run) on trending-tape days for cascade / gap retest? | NOT ADOPTED | cascade +5.58 vs +4.99 in-sample but holdout +13.35 vs +14.20; retest worse | 2026-10-17_regime_trail.md |
| 17-25 | 2026-10-17 | order-flow | 1–5 min tape autocorrelation as a filter on 16 strategy families? | FAIL | no fail became a pass; five passes became fails (cascade 4.80 → 3.04) | 2026-10-17_tape_filter_all.md |
| 17-26 | 2026-10-17 | calendar/news | Skip the 09:50 cascade check on 10:00 release days (JOLTS, ISM)? | NOT ADOPTED | +2.11 / +13.86 / −4.71 by era; holdout −26.4 (13) — rule not met, watch item | 2026-10-17_test_f_news10.md |
| 17-27a | 2026-10-17 | pipeline/validation | Gap retest robust to OR5/OR30 and gap threshold 0.05–0.3? | PASS | gor_or5 +7.35, gor_or30 +8.49, gor_gap +7.86 — ROBUST | 2026-10-17_tests_c_d_e.md |
| 17-27b | 2026-10-17 | order-flow | Cumulative delta agreeing as a filter on cascade / gap retest (2023–26)? | OPEN | casc_flow +18.50 vs twin +14.99; gor_flow +14.18 vs +13.57 — one era, forward candidate | 2026-10-17_tests_c_d_e.md |
| 17-27c | 2026-10-17 | cross-market (ES/VIX/macro) | Trade NQ in the leading direction vs ES at 10:30 (ldr_es)? | FAIL | +2.85, random day 66 % | 2026-10-17_tests_c_d_e.md |
| 17-28 | 2026-10-17 | EMA/indicators | Momentum add-ons: order flow, VIX sizing, scale-out, plan agreement? | NO RESULT | no Results in file; research/programs/out/momadd.txt: plan agrees +0.249 R (ledger: ADOPTED on paper as mom_dir_45_vwap_plan) | 2026-10-17_momentum_addons.md |
| 18-01a | 2026-10-18 | price-action/patterns | BDH (Wieland / D-Money) YouTube rules: ORB15 mid, ORB5, scalps, 14/50 EMA, retests, 15:50? | FAIL | ORB15 mid +2.92 / +3.97 FAIL controls; all others FAIL screen (−0.36 … −5.02) | 2026-10-18_bdh.md |
| 18-01b | 2026-10-18 | direction/NY-open | BDH ORB15 on the gap side only? | FAIL | +6.37 (74/yr), random direction 98 % vs 99.4 %; 2025+ hold −4.69 | 2026-10-18_bdh.md |
| 18-02 | 2026-10-18 | direction/NY-open | BDH ORB gap-side optimisation, 72 variants? | NOT ADOPTED | PASS +6.32 (13/14 yrs, plateau) but all 24 frozen variants negative on 2025+ | 2026-10-18_bdh_optimize.md |
| 18-03 | 2026-10-18 | sizing/risk/prop-sim | Book ablation: which setups to keep (pts + prop EV)? | ADOPTED | keep weakness long / cascade / gap retest; drop 10:00 gap + momentum (EV +7–13 %) | 2026-10-18_book_ablation.md |
| 18-04a | 2026-10-18 | sizing/risk/prop-sim | Gap rule + weakness long on one account (skip opposite days)? | DESCRIPTION | book +14,170 pts, max DD 566 vs WEAK 1,395 / GAP 304 | 2026-10-18_book_and_definition.md |
| 18-04b | 2026-10-18 | pipeline/validation | Path defined on 5-min closes instead of range? | PASS | 2019–24 +19.21 (t 3.59) = 85 % of +22.48 | 2026-10-18_book_and_definition.md |
| 18-04c | 2026-10-18 | calendar/news | Day-of-week rule for the gap rule? | DESCRIPTION | every weekday positive in both periods; no rule | 2026-10-18_book_and_definition.md |
| 18-05 | 2026-10-18 | exits/stops | Book management: BE at +0.5/+1R, flat 12:00/13:30, daily-path filter? | NOT ADOPTED | all lower pts or higher DD; path filter +1.14 vs +0.41 (2010–18) but −2 pts 2019–24 | 2026-10-18_book_management.md |
| 18-06 | 2026-10-18 | exits/stops | Book stop × target grid (0.25–0.5 ATR × 1.5–4R)? | NOT ADOPTED | pick 0.35/4R lower in 2019–24 (+8.57 vs +9.23); 0.35/2R stays (plateau) | 2026-10-18_book_stop_target.md |
| 18-07a | 2026-10-18 | price-action/patterns | BOS (1h vs prior 2h) then EMA pullback — NY / NY15 / London / Globex? | FAIL | +0.78 / +1.01 / −0.70 / −1.29 | 2026-10-18_bos_ema.md |
| 18-07b | 2026-10-18 | price-action/patterns | BOS + EMA pullback on the gap side only (bos_ema_gap)? | PASS | +4.03 (1,204, t 2.48); holdout +8.45 on 141 (t 0.9); weaker than gap_or_retest → paper candidate | 2026-10-18_bos_ema.md |
| 18-08 | 2026-10-18 | price-action/patterns | BOS entries 09:30–11:30, 2–3 a day, ± direction / path? | FAIL | all 24 variants < 0 in 2010–18 (best −0.56 pts/session) | 2026-10-18_bos_morning.md |
| 18-09 | 2026-10-18 | cross-market (ES/VIX/macro) | ES confirmation and prior VIX change as cascade size rules? | ADOPTED | shorts after VIX fell +1.58 / +12.81 vs +0.71 / +12.31; ES same sign every period → size rule | 2026-10-18_cascade_es_vix.md |
| 18-10 | 2026-10-18 | order-flow | Book imbalance + delta as a cascade filter (2023–26)? | FAIL | pick q4 +10.96 discovery; confirmation WF 50 trades +1.58, t 0.10; "book against" skip = hypothesis | 2026-10-18_cascade_orderbook.md |
| 18-11 | 2026-10-18 | price-action/patterns | Cause & effect (tight build-up) and effort vs result (rvol) on the cascade? | NOT ADOPTED | prior-day-mid +1.22 / +11.57 meets rule by the letter (1 of 9), 2025–26 +5.02 < +13.71 | 2026-10-18_cause_effect_effort.md |
| 18-12 | 2026-10-18 | sizing/risk/prop-sim | Greedy side-by-side book of 13 candidates? | DESCRIPTION | F + gap-down weakness long + Asia drive: +0.81 / +16.96 / +18.02; on Vest F stays (no-stop long breaks limit) | 2026-10-18_combine_many.md |
| 18-13 | 2026-10-18 | exits/stops | Scale-out 0.25–1R / without weakness long for a consistent daily book? | FAIL | no variant qualifies; green days up to 66 % but best 1 % of days 43–88 % of profit | 2026-10-18_consistency.md |
| 18-14a | 2026-10-18 | behaviour (descriptive) | 10-session daily path (not straight) as filter on the 10:00 gap rule? | ADOPTED | confirm 2019–24 +29.44 (119, t 3.75) vs +28.26; holdout 36 trades +1,485 → size rule | 2026-10-18_daily_path_direction.md |
| 18-14b | 2026-10-18 | direction/NY-open | Daily path × direction on its own (5/10/20 sessions)? | FAIL | −2.63 / −1.04 / +0.56 per trade | 2026-10-18_daily_path_direction.md |
| 18-15 | 2026-10-18 | direction/NY-open | A trade every day: 10:05 forced direction / 11:35 no-cascade fallbacks? | FAIL | daily_1005 +4.52 FAIL controls; quiet_1135 −0.00 / +0.11 / +2.72 | 2026-10-18_daily_trade.md |
| 18-16a | 2026-10-18 | direction/NY-open | Straight path (≥ 0.55) at entry as size/skip rule on cascade and gap retest? | NOT ADOPTED | gap_or_retest_eff PASS +11.19 (t 4.36), holdout +2,349 on 69, but 1.44 × complement (need 1.5); cascade nothing | 2026-10-18_direction_followups.md |
| 18-16b | 2026-10-18 | exits/stops | Runner (trail_path) on straight-path days? | FAIL | cascade +7,906 vs base +11,139; retest +8,875 vs +11,015 | 2026-10-18_direction_followups.md |
| 18-16c | 2026-10-18 | price-action/patterns | Intraday Wyckoff: midday range break with / against the morning move? | FAIL | iw_cont +1.98 (47) controls; iw_rev +8.70 (26) screen; ~5 setups/yr | 2026-10-18_direction_followups.md |
| 18-17a | 2026-10-18 | models/ML | Feature selection + take/skip refit over 33 features on 4 bases? | PASS | gap retest refit +15.66 vs +11.00 PASS; cascade / momentum / union refits fail a control | 2026-10-18_feature_study.md |
| 18-17b | 2026-10-18 | exits/stops | Regime-gated trail (trail_reg, reg_score ≥ 60)? | FAIL | cascade +4.42 vs +4.80; retest +4.74 vs +9.03 | 2026-10-18_feature_study.md |
| 18-18a | 2026-10-18 | cross-market (ES/VIX/macro) | Follow NQ's lead over ES (nq_es_follow)? | FAIL | WF 587 trades +2.36, FAIL controls | 2026-10-18_follow_and_quad_close.md |
| 18-18b | 2026-10-18 | calendar/news | Quarterly-rebalance close (quad_close)? | NO RESULT | NOT TESTABLE — NQ.c.0 cache has 0 RTH bars on expiry Fridays | 2026-10-18_follow_and_quad_close.md |
| 18-19 | 2026-10-18 | direction/NY-open | Gap rule checks: long/short legs, VIX buckets, ES agreement, correlation with weak-day | PASS | legs PASS (short +5.01 / +22.93); no VIX or ES rule; monthly corr with weak_day 0.19 | 2026-10-18_gap_rule_checks.md |
| 18-20 | 2026-10-18 | exits/stops | High-win-rate exits for the every-day book (targets 0.5–2R × stops)? | FAIL | every ≥ 60 % win form < 0 in 2010–18; 0.5R 68 % win +8.5 vs 2R +11.1 pts/session | 2026-10-18_high_winrate_book.md |
| 18-21 | 2026-10-18 | EMA/indicators | Course indicators (RSI, MACD, VWAP SD, Keltner, EMA, VA, CVD) as filters on path × direction? | FAIL | redundant (on almost every day); MOM VWAP-band confirm +19.95 < unfiltered +22.16 | 2026-10-18_indicator_filters.md |
| 18-22 | 2026-10-18 | sizing/risk/prop-sim | Are late cascade checks / afternoon retests worth keeping in book F? | NOT ADOPTED | cuts fail; current +15.05 pts/session 2019–24, pts/DD 0.0121 → late entries stay | 2026-10-18_late_entries.md |
| 18-23 | 2026-10-18 | exits/stops | Take profit at the nearest liquidity pool (liq / liq_run)? | FAIL | never better than 2R; retest twin beaten 89–92.5 % (need 99) | 2026-10-18_liquidity_exit.md |
| 18-24 | 2026-10-18 | models/ML | Claude (sonnet-5) as discretionary ORB trend / MR trader on 240 days? | NO RESULT | STOPPED during pilot (~12 days); snapshot gap bug found; no verdict | 2026-10-18_llm_judge.md |
| 18-25 | 2026-10-18 | sizing/risk/prop-sim | Every-day book of all confirmed setups, 1 NQ, pts/session (description) | DESCRIPTION | +10.6 pts/session at 2 pt (2019–26), median day 0, max DD 1,156; 2010–18 ≈ 0–2 | 2026-10-18_daily_book.md |
| 18-26a | 2026-10-18 | overnight/sessions | Path × direction at the London open (Asia side, 03:00–03:30 path)? | FAIL | discovery +0.25 on 237; 2015–18 −0.17 | 2026-10-18_london_path_direction.md |
| 18-26b | 2026-10-18 | sizing/risk/prop-sim | Gap size buckets; monthly sizing book (2/1/1/0.5) (description) | DESCRIPTION | no stable gap-size ordering; sizing book +9,150 vs flat +5,161 pts, DD 384 vs 401 | 2026-10-18_london_path_direction.md |
| 18-27 | 2026-10-18 | price-action/patterns | LuxAlgo SMC with LuxAlgo's own OB / FVG definitions? | FAIL | OB retest −0.62 (screen); FVG fill −1.65 (WF) | 2026-10-18_luxalgo_faithful.md |
| 18-28a | 2026-10-18 | price-action/patterns | SMC order-block retest / FVG fill entries? | FAIL | OB +0.59 on 1,798 (WF, direction 53 %); FVG −3.11 (screen) | 2026-10-18_luxalgo_smc.md |
| 18-28b | 2026-10-18 | price-action/patterns | Premium/discount gate on cascade and gap retest? | FAIL | keeps 7–10 trades/yr, lose 2010–22; complements = ungated base (+4.45 / +8.94) | 2026-10-18_luxalgo_smc.md |
| 18-28c | 2026-10-18 | exits/stops | LuxAlgo ORB 50 % targets (orbt) on cascade / gap retest? | FAIL | +4.53 vs +4.80; +8.72 vs +9.03; twin 72 / 86 % | 2026-10-18_luxalgo_smc.md |
| 18-29 | 2026-10-18 | price-action/patterns | 1-min direction then BOS / CHoCH then EMA pullback? | FAIL | BOS +10.81 (215) direction 97 %, 2025+ −7.73 on 25; CHoCH 3/yr; no-structure +4.71 | 2026-10-18_m1dir_structure_ema.md |
| 18-30a | 2026-10-18 | direction/NY-open | 1-min momentum scanned all session, ≤ 3/day, VWAP pullback entry? | PASS | mom_scan_3_pb +3.09 (128/yr); 2025+ +23.33 on 201; market entries fail controls | 2026-10-18_momentum_daily.md |
| 18-30b | 2026-10-18 | EMA/indicators | Momentum scan with EMA 9/20/14 pullback, or VWAP side + EMA? | FAIL | +2.09 / +1.61 / vwap_side_ema +2.24, all FAIL controls | 2026-10-18_momentum_daily.md |
| 18-31a | 2026-10-18 | sizing/risk/prop-sim | Sequential 2nd/3rd trade a day from the book's setups? | FAIL | 2nd fits on 0.08 sessions; B/D lower 2010–18; E (after win) adds nothing 2019–24 | 2026-10-18_multi_trade_day.md |
| 18-31b | 2026-10-18 | sizing/risk/prop-sim | Overlapping book F (all setups side by side)? | ADOPTED | +15.05 vs +6.66 pts/session 2019–24, DD 1,247; Vest $50 62.9 % in 24 days | 2026-10-18_multi_trade_day.md |
| 18-32 | 2026-10-18 | direction/NY-open | ORB-5/15 then trend or reverse by verified regime (7 reads)? | FAIL | no regime verified; trend on gap +4.20 vs −11.25, on straight +6.01 but random time 71 % | 2026-10-18_orb_regime.md |
| 18-33a | 2026-10-18 | direction/NY-open | Path × direction second chance at 10:30 / 11:00 / 11:30? | FAIL | confirm +9.89 on 81, controls 92 / 3 / 78 % | 2026-10-18_path_direction_extensions.md |
| 18-33b | 2026-10-18 | cross-market (ES/VIX/macro) | Path × direction gap rule on ES? | PASS | 2019–24 +4.86 ES pts (t 3.28); holdout 39 trades +7 pts (flat) | 2026-10-18_path_direction_extensions.md |
| 18-33c | 2026-10-18 | calendar/news | Is the gap rule worse on news days? | DESCRIPTION | no: 2019–24 +32.88 (29) vs +20.85 (185) | 2026-10-18_path_direction_extensions.md |
| 18-34 | 2026-10-18 | direction/NY-open | Path × direction map: gap / cascade / momentum side × straight path at 10:00? | ADOPTED | CONFIRMED pdm_gap +28.26 (t 4.26, holdout 50 trades +919), pdm_mom +22.16 (holdout +2,448); cascade side FAIL → paper pd_gap_1000 / pd_mom_1000 | 2026-10-18_path_direction_map.md |
| 18-35a | 2026-10-18 | pipeline/validation | Plateau of the confirmed path × direction rules (entry time, path, stop, target)? | PASS | every neighbour > 0; weakest 58 % / 60 % of the cell | 2026-10-18_path_direction_robustness.md |
| 18-35b | 2026-10-18 | exits/stops | Thesis exits on the path × direction rules? | FAIL | open_run / liq_run fail timing control (81 % / 28 %); 2R stays | 2026-10-18_path_direction_robustness.md |
| 18-35c | 2026-10-18 | sizing/risk/prop-sim | Both-agree (gap + momentum) days on 2010–18; 2 units when both agree? | OPEN | agree +4.54 (t 2.30) vs gap-only +1.89, mom-only −1.77 → paper hypothesis | 2026-10-18_path_direction_robustness.md |
| 18-36a | 2026-10-18 | sizing/risk/prop-sim | Stacking cascade + gap retest + momentum on Lightning 25K? | NOT ADOPTED | daily corr +0.59 … +0.72; C alone 8.4 % paid / 41.8 % blown vs C+G+M 1.6 / 59.2 % — PARKED | 2026-10-18_plan_checks.md |
| 18-36b | 2026-10-18 | calendar/news | News days (CPI/PPI/NFP/FOMC) for 6 strategies? | ADOPTED | cascade CPI/PPI ≈ $0, gap retest FOMC −$9.25 (52) → stand aside; gap_or_retest__nofomc | 2026-10-18_plan_checks.md |
| 18-36c | 2026-10-18 | cross-market (ES/VIX/macro) | Quiet-market stand-aside (prior VIX / ATR %)? | FAIL | fixes 2013–18 but total +16,644 / +17,307 vs +19,641 | 2026-10-18_plan_checks.md |
| 18-37 | 2026-10-18 | pipeline/validation | Validate every price-action test (code, pipeline, reproducibility, flips, cost)? | PASS | VALIDATED: 598/598 unit, 76/76 pipeline, 55/55 reproduce; detection limit ~5 pts | 2026-10-18_price_action_validation.md |
| 18-38a | 2026-10-18 | sizing/risk/prop-sim | Daily profit/loss stops on prop plans (payout_sim, blown accounts allowed)? | FAIL | chosen rule "none" on every plan; book_2r Select Daily 50K $1,801, Topstep $2,237 | 2026-10-18_prop_daily_stop.md |
| 18-38b | 2026-10-18 | sizing/risk/prop-sim | VestMarkets Silver 5K: eval / funded / multiple accounts (addendum) | DESCRIPTION | $50 risk 53 % pass; split setups per account → nearly independent; coin flip 13 % pass | 2026-10-18_prop_daily_stop.md |
| 18-39 | 2026-10-18 | mean-reversion | Fades on range days (daily path not straight + choppy morning)? | FAIL | rd_vwap −2.78, rd_range −2.35, rd_open +0.07; all negative 2023–26 | 2026-10-18_range_day_mr.md |
| 18-40a | 2026-10-18 | order-flow | Rithmic aggressor delta as the 10:00 direction (2023–26)? | FAIL | delta+price +16.36 (158) FAIL controls; overnight delta −6.61; absorption 1 trade | 2026-10-18_rithmic_direction.md |
| 18-40b | 2026-10-18 | direction/NY-open | 1-min momentum direction at 10:00 (rdir_m1), 2023–26 | PASS | +34.81 (90, 26/yr), controls 100 %; delta adds nothing (rdir_m1_delta) | 2026-10-18_rithmic_direction.md |
| 18-41a | 2026-10-18 | exits/stops | 1 trade/day, 1 NQ, 20–30 pt target (every day / momentum pullback / 10:00 momentum)? | PASS | only scalp_m1_1000 PASS +2.46 (19/yr, win 77 %); every-day −0.18, mom pullback −1.00 FAIL screen | 2026-10-18_scalp_daily.md |
| 18-41b | 2026-10-18 | direction/NY-open | First tested setup of the day (union_2r), 2R vs scalp target? | PASS | union_2r +4.01 (≈ 80 % of days, 12/14 yrs), 2025+ +5.46 on 249; scalp forms FAIL screen | 2026-10-18_scalp_daily.md |
| 18-42a | 2026-10-18 | direction/NY-open | NY ORB-15/5 state and London/Globex agreement as cascade filters? | NOT ADOPTED | orb15_beyond PASS +5.33 but 2025+ +9.24 vs complement +28.20; others FAIL controls | 2026-10-18_session_cockpit.md |
| 18-42b | 2026-10-18 | overnight/sessions | Globex / London ORB 5/15 break and fade, NY ORB-5 fade? | FAIL | −0.71 … −1.46 pts/trade (screen / WF) | 2026-10-18_session_cockpit.md |
| 18-42c | 2026-10-18 | order-flow | Asia sweep (flow) forward on collector sessions? | NO RESULT | not checkable — flow flag only 2023-03 → 2026-03 | 2026-10-18_session_cockpit.md |
| 18-43 | 2026-10-18 | sizing/risk/prop-sim | Add 10:00 momentum (M) / weakness long with stop (W) side by side with F? | NOT ADOPTED | F+M +18.73 but 2010–18 +0.11 < +0.15; W adds DD > 1.3× cap; every add lowers Vest pass | 2026-10-18_side_by_side_adds.md |
| 18-44 | 2026-10-18 | price-action/patterns | SMC retests with day direction + path (smc_ob_dp, smc_fvg_dp, dp_base)? | FAIL | all beat random direction, none beats random time (1 / 65 / 59 %) | 2026-10-18_smc_path_direction.md |
| 18-45 | 2026-10-18 | exits/stops | Daily trend scalp 1–3 trades of 10–30 pts (ts / tm / tb)? | FAIL | ts confirm +1.02/trade FAIL controls, 2023–24 −1.44 pts/session; tb all < 0 | 2026-10-18_trend_scalp.md |
| 18-46 | 2026-10-18 | cross-market (ES/VIX/macro) | Checks of the VIX-fell short size rule (other entries, 5-day, book)? | DESCRIPTION | consistent on retest shorts; 5-day mixed; 2× → pts +19 %, DD +35 %; prefer 1.5× | 2026-10-18_vix_short_checks.md |
| 18-47 | 2026-10-18 | mean-reversion | Path / direction confirmation or 10:00 exit on the weakness long? | NOT ADOPTED | every variant lower total; W0 +1,103 / +4,587 / +5,710 stays | 2026-10-18_weakness_path.md |
| 18-48 | 2026-10-18 | calendar/news | Weekly path skip and turn-of-month size-up on the cascade? | OPEN | skip WITH straight week −1.97 / −7.28 (2025–26 +32.52); TOM longs +1.77 / +13.43 → forward record only | 2026-10-18_weekly_and_tom.md |
| 18-49 | 2026-10-18 | calendar/news | Do the weekly skip / TOM rules replicate on ES and other NQ entries? | FAIL | neither replicates on both checks → forward record only; book +1.4 pts/session, higher DD | 2026-10-18_weekly_tom_checks.md |
| 18-50 | 2026-10-18 | price-action/patterns | Wyckoff phases (markup / markdown / ranges) as direction or filter? | FAIL | none beats random direction (74–86 %); each positive average in one era | 2026-10-18_wyckoff_phases.md |
| 18-51 | 2026-10-18 | models/ML | Model-zoo validation: which of 14 dashboard models carry OOS signal? | NO RESULT | "## Results (appended after the run)" with nothing after it | 2026-10-18_zoo_study.md |
| 19-01a | 2026-10-19 | overnight/sessions | Overnight holds on Vest: every day / weak close / weak close + VIX ≥ 20 (O3)? | ADOPTED | O3 +5.01 / +43.57 / +69.06 pts/trade; Vest op +$16,139 vs $15,566, up in all 4 cells; O1/O2 fail | 2026-10-19_attack_behaviour.md |
| 19-01b | 2026-10-19 | exits/stops | Targets by predicted range? | FAIL | +0.104 / +0.193 / +0.055 R vs 2R +0.090 / +0.198 / +0.086 | 2026-10-19_attack_behaviour.md |
| 19-01c | 2026-10-19 | sizing/risk/prop-sim | Start Vest evals only on predicted-wild days? | FAIL | $12,685 vs $13,494/yr (2 pts); 6 pts losing windows 25 vs 12 | 2026-10-19_attack_behaviour.md |
| 19-01d | 2026-10-19 | price-action/patterns | Fade the first break of yesterday's high / low? | FAIL | screen: −2.62 pts/trade, every era negative | 2026-10-19_attack_behaviour.md |
| 19-01e | 2026-10-19 | calendar/news | Skip Monday / Tuesday shorts? | NOT ADOPTED | skip rule met by letter but Vest op lower ($15,341 vs $16,139) | 2026-10-19_attack_behaviour.md |
| 19-01f | 2026-10-19 | sizing/risk/prop-sim | Anti-streak sizing (1.5× after a losing book month)? | NOT ADOPTED | own rule met (pts/DD 0.0121 → 0.0139) but Vest losing windows up 2019–25 — founder's call | 2026-10-19_attack_behaviour.md |
| 19-02a | 2026-10-19 | behaviour (descriptive) | Streaks / equity-curve filter on the book? | FAIL | monthly autocorr −0.235 / −0.027; 20-day filter +15.05 → +10.31 (2019–24) | 2026-10-19_behavior_part3.md |
| 19-02b | 2026-10-19 | exits/stops | Wider stops (0.5 / 0.7 ATR) for book F given 0.49 ATR trend pullbacks? | FAIL | Vest op $15,449 → $7,344 → $3,519; 0.35 ATR stays | 2026-10-19_behavior_part3.md |
| 19-02c | 2026-10-19 | behaviour (descriptive) | Gap fills, PDH/PDL sweeps, round numbers, afternoon, RVOL, order flow (2023+)? | DESCRIPTION | small gaps fill 74–86 %; PDH/PDL back in 30 min ~83 %; round numbers / RVOL nothing; delta mostly agrees | 2026-10-19_behavior_part3.md |
| 19-03 | 2026-10-19 | behaviour (descriptive) | Same behaviour measures per session (Asia / London / NY)? | DESCRIPTION | NY only follow-through: one side of VWAP 20 % vs 6–8 %; straight 1st hour → trend 60 % | 2026-10-19_behavior_per_session.md |
| 19-04 | 2026-10-19 | direction/NY-open | 1m / 5m / 15m break or pullback entries vs market entry for the cascade? | FAIL | none beats market per session (base +0.25 / +3.06; best brk 15m +0.10 / +2.57) | 2026-10-19_entry_timeframes.md |
| 19-05a | 2026-10-19 | calendar/news | Book F without expiry (rollwin) weeks? | NOT ADOPTED | +0.30 / +16.16 vs +0.31 / +16.11; 2010–18 lower by 0.01 | 2026-10-19_expiry_earnings.md |
| 19-05b | 2026-10-19 | calendar/news | Size up the cascade on big-tech earnings days? | NOT ADOPTED | size-up met by letter, ES agrees, but t 0.60; 2nd unit lowers pts/DD and Vest pass 44.3 → 38.6 % | 2026-10-19_expiry_earnings.md |
| 19-06 | 2026-10-19 | price-action/patterns | 1-min bull/bear flags after a straight pole (NY / Globex)? | FAIL | NY +2.44 (668) direction just < 99 %, 2025–26 −5.59; Globex ≤ 0 | 2026-10-19_flag_1m.md |
| 19-07a | 2026-10-19 | sizing/risk/prop-sim | Full risk ($145) only on strongest cascade days (path / gap / ES / VIX)? | FAIL | pick E 31.4 % discovery → 25.6 % vs base 24.8 % on 2019–26 | 2026-10-19_full_risk_best_days.md |
| 19-07b | 2026-10-19 | sizing/risk/prop-sim | Risk-size sweep on Vest eval (description) | DESCRIPTION | first trade 2R: $40 61.2 %, $120 44.5 %, $145 24.5 %; all F trades $40 74.6 % | 2026-10-19_full_risk_best_days.md |
| 19-07c | 2026-10-19 | overnight/sessions | "Follow the session move" one hour in, all sessions (description) | FAIL | Asia −4.15, London −8.93, NY 10:30 +6.16 / 09:50 +4.37 FAIL controls | 2026-10-19_full_risk_best_days.md |
| 19-08 | 2026-10-19 | sizing/risk/prop-sim | Full risk $145 + full target 3.33 / 4.33R on Vest eval (not pre-registered) | DESCRIPTION | target hit 4.5–5.1 %; pass 19.5–24.9 % at $160–205/pass vs $50/2R 53.6–64.1 % | 2026-10-19_full_risk_full_tp.md |
| 19-09a | 2026-10-19 | direction/NY-open | Gap retest only on gaps ≥ 0.25 ATR in book F? | FAIL | pts/session 2019–24 +16.11 → +14.50; pts/DD 0.0123 → 0.0097 | 2026-10-19_gaps_and_holds.md |
| 19-09b | 2026-10-19 | price-action/patterns | PDH/PDL break that holds 30 min (pd_hold)? | FAIL | +4.14 on 465, FAIL controls (direction 97 %); 2010–18 −0.55 | 2026-10-19_gaps_and_holds.md |
| 19-09c | 2026-10-19 | mean-reversion | Small-gap fade toward the prior close (gap_fade)? | FAIL | m2 passed gates +4.64 (343) but holdout −7.91 on 70; opposite side of the retest | 2026-10-19_gaps_and_holds.md |
| 19-10a | 2026-10-19 | EMA/indicators | Globex EMA 9/20/50 stack, pullback to 5-min EMA20, ≤ 3/night? | FAIL | screen: WF −0.57 on 2,066, gross +0.43 | 2026-10-19_globex_ema_follow.md |
| 19-10b | 2026-10-19 | EMA/indicators | Globex EMA follow with no target, trail after +1R (gx_ema_run / run1)? | FAIL | screen: −0.26 / −0.43; Vest $150 run1 25.3 % pass at $158/pass | 2026-10-19_globex_ema_follow.md |
| 19-11a | 2026-10-19 | models/ML | Model of NY range at 09:30 → size rule? | FAIL | range forecastable (corr +0.42–0.52) but book R flat across fifths → no size rule | 2026-10-19_intraday_models.md |
| 19-11b | 2026-10-19 | models/ML | Direction model from 09:50 / 10:30 beyond "go with the move"? | FAIL | 09:50 agree-filter worse 2014–18; 10:30 ridge disagrees | 2026-10-19_intraday_models.md |
| 19-12 | 2026-10-19 | overnight/sessions | Fade the London first hour (04:00 vs 03:00)? | FAIL | +0.76 on 665, random direction 79 %; holdout +2.34 on 113 | 2026-10-19_london_fade.md |
| 19-13 | 2026-10-19 | behaviour (descriptive) | Multi-day context (yesterday's trend, 5-day move) on cascade / gap retest? | FAIL | retest size-up by the letter (+2.86 / +24.61) but ES does not replicate → nothing adopted | 2026-10-19_multiday_context.md |
| 19-14 | 2026-10-19 | models/ML | Session model (trees + ridge, 28 features) → Asia / London / NY direction? | FAIL | hit below "always up" in 5 of 6 cells; corr 0.01–0.04 | 2026-10-19_session_model.md |
| 19-15a | 2026-10-19 | sizing/risk/prop-sim | Skip the book on predicted-wildest days (tails)? | FAIL | wildest fifth 1.5–1.6× the −2R days but earns ≥ average → skip rule not met | 2026-10-19_tails_and_es.md |
| 19-15b | 2026-10-19 | cross-market (ES/VIX/macro) | 5 NQ + 5 ES accounts instead of 10 NQ? | NOT ADOPTED | 5+5 $11,503 vs $15,566 (2019–25); losing windows 6 vs 5 in 2010–18 | 2026-10-19_tails_and_es.md |
| 19-16 | 2026-10-19 | direction/NY-open | Pullback entries (VWAP / EMA20) on a confirmed 09:50 trend day? | FAIL | +1.98 on 787, direction 73 %; 2023–26 −6.44; as add pts/DD 0.0121 → 0.0096 | 2026-10-19_trend_day_pullback.md |
| 19-17 | 2026-10-19 | sizing/risk/prop-sim | Vest adds: weakness long non-gap-down / 10:00 momentum / 1.5× VIX-fall shorts? | NOT ADOPTED | each lowers $40 pass 75.3 → 72.8–73.6 % | 2026-10-19_vest_adds.md |
| 19-18a | 2026-10-19 | overnight/sessions | Multi-day weakness book W1–W3 on Vest instead of O3? | NOT ADOPTED | W3 $16,884 vs O3 $16,139 but more losing windows in 2 cells; W2 stack breaches $5k | 2026-10-19_vest_holds_and_risk.md |
| 19-18b | 2026-10-19 | overnight/sessions | O3 Friday entry: Friday close vs Sunday 18:00? | NOT ADOPTED | Fri +21.25 / +67.58 vs Sun +9.35 / +145.97 — mixed → keep Friday | 2026-10-19_vest_holds_and_risk.md |
| 19-18c | 2026-10-19 | sizing/risk/prop-sim | First-year risk, regimes, live slippage of the Vest operation (description) | DESCRIPTION | $1,673/month 2019–25; losing first year ≈ 6 %; +1.25 pt slippage −$0.5–1.5k/yr | 2026-10-19_vest_holds_and_risk.md |
| 19-19a | 2026-10-19 | sizing/risk/prop-sim | Gap-down weakness long with a 0.5–1.5 ATR stop for Vest? | ADOPTED | 1.5 ATR +23.29 / +24.44 / +77.52 (≈ no-stop); F+W pts/session +16.48; Vest $40 74.6 → 75.3 % | 2026-10-19_weakness_long_prop.md |
| 19-19b | 2026-10-19 | exits/stops | Thesis exits on the book's own cascade (description follow-up) | FAIL | base +5.55; vwap +5.04, open +5.80, vwap+stall +5.24 — no rule passes | 2026-10-19_weakness_long_prop.md |
| 19-19c | 2026-10-19 | overnight/sessions | Asia open drive as a third Vest piece? | NOT ADOPTED | ≈ 0 pts; Vest $50 66.6 → 57.4 % | 2026-10-19_weakness_long_prop.md |
| 19-20a | 2026-10-19 | behaviour (descriptive) | Weekly / daily NQ profile (week high/low day, weekly carry)? | DESCRIPTION | week high Fri / low Mon ~30 %; Monday carry 42–51 %; no weekly direction rule | 2026-10-19_weekly_daily_profile.md |
| 19-20b | 2026-10-19 | calendar/news | Friday book trades at 1.5× in the Vest operation? | ADOPTED | +$15,530 → +$18,333/yr (2019–25), +$10,722 → +$13,108 (2010–18); but Fridays 2025–26 −16.79 on 75 | 2026-10-19_weekly_daily_profile.md |
| 19-21a | 2026-10-19 | sizing/risk/prop-sim | Vest funded $5k: which plan / risk / claim threshold? | DESCRIPTION | FW $40 claim ≥ $500 value $988/fee; adds don't help; $30 claim ≥ $1,000 $2,386/fee | 2026-10-19_vest_funded.md |
| 19-21b | 2026-10-19 | sizing/risk/prop-sim | Funded plan stress: 2010–18 starts and 3–4 pt costs? | DESCRIPTION | fixed 2 pts 2010–18 ≈ $0/fee; 2019+ 3–4 pts +$1,216 … +$2,017; scaled cost 2010–18 +$547 … +$873 | 2026-10-19_vest_funded.md |
| 19-21c | 2026-10-19 | sizing/risk/prop-sim | Full port ($120 / $145 first trade) in eval and/or funded? | NOT ADOPTED | full port funded ≤ +$229/fee; eval-only +$1,222 vs $30 throughout +$2,386 | 2026-10-19_vest_funded.md |
| 19-21d | 2026-10-19 | sizing/risk/prop-sim | Funded claim to a buffer; room-based sizing (funded and eval)? | ADOPTED | claim $1,000 keep $250: $3,365 vs $2,837/account, breaches 67 → 35 %; room-based: no gain | 2026-10-19_vest_funded.md |
| 19-21e | 2026-10-19 | sizing/risk/prop-sim | Portfolio of 10 Vest accounts, 82 twelve-month windows? | DESCRIPTION | $30/claim $1,000 +$6.2k/yr; $50/claim $500 keep $250 +$13.4k / +$12.3k, 4 / 13 losing | 2026-10-19_vest_funded.md |
| 19-21f | 2026-10-19 | sizing/risk/prop-sim | Portfolio eval policy (first trade $100–145 vs all setups, refill rate)? | ADOPTED | pick: $120 first trade, refill up to 10/day +$13,941 / +$12,727 (5 / 10 losing) | 2026-10-19_vest_funded.md |
| 19-21g | 2026-10-19 | pipeline/validation | Operation plan out of period (2010–18, cost scaled to ATR)? | PASS | HOLDS: +$10,722 / +$10,062/yr, 5/89 losing; fixed 3 pts −$3,771 | 2026-10-19_vest_funded.md |
| 19-21h | 2026-10-19 | sizing/risk/prop-sim | Operation plan cost sweep 2–30 pts (description) | DESCRIPTION | +$15.5k at 2 pts → +$8.2k at 10; break-even ≈ 15–20 pts round trip | 2026-10-19_vest_funded.md |
| 19-21i | 2026-10-19 | sizing/risk/prop-sim | High-cost plan choice at 10–20 pts (all setups vs fewer)? | DESCRIPTION | FW $50/claim $500 best at every cost: +$6,564 / +$3,196 / +$645 | 2026-10-19_vest_funded.md |

## Consistency problems

### 1. Pre-registrations with no results (or results never appended)
| file | state |
|---|---|
| `2026-10-17_daily_trade.md` | Rules only (8 lines), no Results. `data/edge_lab/reports/cascade_noon.md` says **PASS**, but the file's own judgement (2), "the 12:00 trades alone net > 0 in ≥ 2 of 3 eras", is never recorded. No ledger row. |
| `2026-10-17_direction_first.md` | Rules only, no Results. The reports exist (`dir_market` PASS; `dir_vwap_pb`, `dir_ema_pb`, `dir_breakout`, `dir_or_retest`, `dir_trend60` FAIL at controls; `dir_fib38` FAIL at screen). Only `2026-10-17_day_engine_plan.md` and `2026-10-17_direction_sources.md` quote them. No ledger row. |
| `2026-10-17_day_engine_plan.md` | §2 pre-registers T1–T5 (meta + flow on casc_flow, dir_ema14, 2nd-leg box, trade cap, full engine). No results in the file. `data/edge_lab/reports/dir_ema14.md` = FAIL at CONTROLS. There are no reports for T1, T3, T4 or T5. No ledger row. |
| `2026-10-17_momentum_addons.md` | Rules only, no Results. The numbers exist only in `research/programs/out/momadd.txt`, yet the ledger lists it as "adopted on paper (mom_dir_45_vwap_plan)". |
| `2026-10-18_zoo_study.md` | Ends with `## Results` / "(appended after the run)" and nothing after it. No ledger row. The harness is Python in another repo (`~/Desktop/bot/python/research/zoo_study.py`). |
| `2026-10-18_llm_judge.md` | Stopped during the pilot (~12 of 240 days) on the founder's redirect. Explicitly no verdict. No ledger row. |
| No `## Results` heading, but results are inline (not a defect, just a different format) | `2026-10-16_frozen_holdout.md`, `2026-10-17_fleet_entries_exits.md` (both "NOT pre-registered" / diagnostic), `2026-10-18_daily_book.md`, `2026-10-19_full_risk_full_tp.md` ("NOT pre-registered: rules written at run time") |
| Not a .md | `research/preregistered/2026-10-16/` is a directory of three JSON idea specs (`orb5_of_dev.json`, `orb15_of_dev.json`, `so_cascade_dev.json`) used by `2026-10-16_orb_order_flow.md` tests 1 and 5. |

### 2. Ledger ↔ pre-registration file mismatches
**Pre-registration files with no ledger row (33 of 118):**
- **All 17 files from 2026-10-16.** The ledger has no 2026-10-16 section: cascade_all_sessions, cascade_refinements, confirm_gap, earnings_mornings,
  free_macro_filters, frozen_holdout, liquidity_boxes, liquidity_boxes_all, mega_grid, new_angles, orb_order_flow, owned_data_ideas,
  patterns_and_conditions, round3, vix_term_structure, vwap_ema, weak_day_fixes. Among them are the findings later work relies on: the
  cascade_gap_agree PASS + ES confirmation, the VWAP exit ADOPTED on gap-agree days, and the weak-day "stays as is" decision.
- **14 files from 2026-10-17**, even though the section header says "every file in research/preregistered/2026-10-17_*": break_box,
  daily_lock, daily_trade, day_engine_plan, direction_first, direction_sources, fleet_consensus, more_trades (ADOPTED stacking rule),
  no_direction_mr, regime_switch, regime_trail, tape_filter_all, test_f_news10, tests_c_d_e. (14 files; 16 of the 30 are named.)
- **2 files from 2026-10-18:** llm_judge, zoo_study.
- 2026-10-19: every file has a ledger row.

**Ledger rows whose source is not a pre-registration file:**
- "1-min momentum at 10:00 only, all years (rdir_m1 without flow) — PASS +13.7 (22/yr, 11/14 years); 2025+ +59 on 50". No prereg file
  contains this result. The spec is `research/ideas/m1_10am_all.json` (mentioned in `2026-10-18_scalp_daily.md` Validation). There is also a
  naming clash: `2026-10-18_combine_many.md` reports `rdir_m1` with "0 trades before 2019", which is the 2023+ flow-window spec, not
  the all-years one.
- "News days vs the opening move (description, 2026-10-19; scratchpad news/)": no prereg file and no `research/programs/out` file.
- "Gamma exposure for ranges (… scratchpad sess/gex_out.txt)", "Pre-session levels (… sess/levels_*.txt)", "Trend vs chop day from
  the morning state (… sess/morning_odds.txt)", "VWAP behaviour (… research/programs/out/vwap_behaviour.txt)": these are descriptions
  without a prereg file. Copies exist in `research/programs/out/` (gex_out.txt, levels_main.txt, morning_odds.txt).
- "NY sizing book (description, same file)" sits under the `2026-10-18_follow_and_quad_close` row, but that file does not contain it.
  The numbers are in `2026-10-18_london_path_direction.md` ("Sizing book month by month").
- "Engine all-year replays (label mom_hist)" (two prose lines): no prereg. The momentum part matches `2026-10-17_momentum_round2.md`.

**Ledger rows that name a non-existent prereg file:** none. Every `2026-10-1x_<slug>` and short slug in README.md resolves to a file.

**Ledger structure:** the "2026-10-18 (session_cockpit.md)" heading covers rows from about 10 other files. All 2026-10-19 rows sit under
the "2026-10-18 (plan_checks.md)" heading, because there is no 2026-10-19 heading. Two prose lines (after the plan_checks rows) break
the table. The `daily_book` row has text after its closing pipe ("|; per-day at the measured 2-pt cost …").

### 3. Ledger verdict vs file verdict
No ledger verdict reverses its file's PASS / FAIL. The mismatches are wording or omission:
- **momentum_addons:** the ledger says "+0.249 R vs −0.001". In `momadd.txt`, −0.001 is "no plan direction yet"; "plan disagrees" is +0.103 R.
  The comparison is mislabelled, and the file has no results to check it against (see §1).
- **ema_permission:** the ledger groups it as "EMA pullback ≈ breakeven …". The file says the permission (plan direction + VWAP / open
  side + before 13:55) was **ADOPTED** for the Plan page. The ledger leaves the adoption out.
- **reentry:** the ledger says "no faster 25K payout". The file says ADOPTED as an optional plan rule. Not contradictory, but the
  adoption is not stated.
- **cascade_noon** (`2026-10-17_daily_trade.md`): the report says PASS, the prereg has no result, and there is no ledger row. Meanwhile
  `2026-10-18_daily_trade.md` (same question, same slug, next day) says "a trade every day … all FAIL".

### 4. Duplicate studies (same question tested more than once). The later file is authoritative unless noted.
- **"A trade every day"**: `2026-10-17_daily_trade.md` (cascade_noon, no result) → `2026-10-18_daily_trade.md` (FAIL). Same slug on two
  dates; the 10-18 file is authoritative.
- **NQ vs ES lead / divergence**: `2026-10-16_owned_data_ideas.md` nq_es_diverge (fade, FAIL) → `2026-10-17_tests_c_d_e.md` ldr_es (follow
  at 10:30, FAIL) → `2026-10-18_follow_and_quad_close.md` nq_es_follow (FAIL). The last is authoritative; all three agree.
- **ES agreement on the cascade**: `2026-10-16_new_angles.md` cascade_es_agree (FAIL) → `2026-10-18_cascade_es_vix.md` ES strong / weak /
  against (no rule). They agree.
- **VVIX ≥ 100 on the cascade**: `2026-10-16_free_macro_filters.md` and the condition matrix in `2026-10-16_patterns_and_conditions.md`.
  Both fail the gates, and both flag it as the best-looking unvalidated filter.
- **Cascade + aggressor delta agreeing**: `2026-10-17_tests_c_d_e.md` D (forward candidate) → `2026-10-18_rithmic_direction.md` (FAIL
  controls) → `2026-10-18_cascade_orderbook.md` q3 → `2026-10-19_behavior_part3.md` 8f (hypothesis). Four looks, never a pass. The
  status is "hypothesis" in every file.
- **Overnight session direction**: `2026-10-16_cascade_all_sessions.md`, `2026-10-17_regime_optimize.md` (London / Asia),
  `2026-10-17_momentum_round2.md` (London / Asia), `2026-10-18_session_cockpit.md` (Globex / London ORB),
  `2026-10-18_london_path_direction.md`, `2026-10-19_full_risk_best_days.md` ("follow the session move"), `2026-10-19_london_fade.md`,
  `2026-10-19_session_model.md`. All fail; `2026-10-19_session_model.md` and `2026-10-19_behavior_per_session.md` are the final word.
- **EMA pullbacks**: `2026-10-16_vwap_ema.md`, `2026-10-17_direction_first.md`, `2026-10-17_ema_permission.md` / `ema_stops` / `ema_entries_trails` / `ema_sweep` / `open_regime_ema`,
  `2026-10-17_regime_switch.md`, `2026-10-17_mtf_1m_dir_5m_entry.md`, `2026-10-17_momentum_optimize.md`, `2026-10-18_momentum_daily.md` (EMA 9 / 20 / 14),
  `2026-10-18_m1dir_structure_ema.md`, `2026-10-18_bos_ema.md`, `2026-10-19_trend_day_pullback.md`, `2026-10-19_globex_ema_follow.md`.
  About 14 looks at one idea; only bos_ema_gap passes (on the gap side, and weaker than the gap retest).
- **Wider stop for the book**: `2026-10-18_book_stop_target.md` (0.5 ATR worse) and `2026-10-19_behavior_part3.md` follow-up (0.5 / 0.7 worse). They agree.
- **Runner / trail exits**: `2026-10-16_cascade_refinements.md` cascade_trail, `2026-10-17_regime_trail.md`, `2026-10-18_direction_followups.md` trail_path,
  `2026-10-18_feature_study.md` trail_reg. All fail against 2R.
- **Fading yesterday's high / low**: `2026-10-16_liquidity_boxes.md` pdbox sweep (FAIL) and `2026-10-19_attack_behaviour.md` #4 (FAIL). They agree.
- **London first-move fade**: `2026-10-18_session_cockpit.md` ldn_orb15_sweep and `2026-10-19_london_fade.md`. Both fail.
- **High-win-rate / small targets**: `2026-10-17_manual_execution.md` (0.5R sprint), `2026-10-18_scalp_daily.md`, `2026-10-18_high_winrate_book.md`,
  `2026-10-18_consistency.md`, `2026-10-18_trend_scalp.md`. They agree: small fixed targets lose, except scalp_m1_1000.
- **Full risk on Vest**: `2026-10-19_full_risk_full_tp.md` → `2026-10-19_full_risk_best_days.md` → the full-port part of `2026-10-19_vest_funded.md`. They agree.

### 5. Adopted, then weakened or overturned later
| adopted in | adopted rule | later file | what happened |
|---|---|---|---|
| `2026-10-17_reentry.md` | 2nd / 3rd trade only after a winner (optional plan rule) | `2026-10-18_multi_trade_day.md` | Sequential 2nd trades are NOT adopted. Variant E ("2nd only after a win") adds nothing in 2019–24, and a 2nd trade fits on only 0.08 sessions. Replaced by the overlapping book F. |
| `2026-10-17_more_trades.md` | Take cascade + gap retest when they agree (Select / Flex 25K) | `2026-10-18_plan_checks.md`, `2026-10-18_multi_trade_day.md` | Stacking is worse on Lightning (PARKED; the file limits the scope to Select / Flex). Superseded by book F (every setup side by side). |
| `2026-10-16_confirm_gap.md` | VWAP thesis exit on cascade_gap_agree (paper so_cascade_2r_gapagree_vwap) | `2026-10-19_weakness_long_prop.md` follow-up | No exit passes on the book's cascade (vwap +5.04 vs base +5.55). The file notes the 10-16 pass was on gap-agree days only. Tension: `2026-10-18_feature_study.md` and `2026-10-18_luxalgo_smc.md` still say "so_cascade still PASS vwap_stall". |
| `2026-10-18_path_direction_map.md` | pdm_gap / pdm_mom CONFIRMED → paper pd_gap_1000 / pd_mom_1000 | `2026-10-18_book_ablation.md`, `2026-10-18_side_by_side_adds.md`, `2026-10-19_vest_adds.md` | The 10:00 gap rule (G) and momentum rule (M) are dropped from the one-trade book (redundant behind the cascade). M is not added to F, and not to Vest. Standalone they stay confirmed. |
| `2026-10-18_book_ablation.md` | Book = weakness long + cascade + gap retest (G and M dropped) | `2026-10-18_multi_trade_day.md` F and every 2026-10-19 file | Book F, adopted the same day, is "cascade + gap retest + **10:00 gap rule**", so it contains G, which the ablation dropped. Two different "books" are in use (one-trade vs overlapping), and the files do not reconcile them. |
| `2026-10-18_daily_path_direction.md` | Size rule: full size on the 10:00 gap rule only after a non-straight fortnight | `2026-10-18_book_ablation.md`, `2026-10-18_book_management.md` | The ablation drops the 10:00 gap rule from the one-trade book. The same daily-path filter applied to cascade / retest fails 2019–24 (−2 pts/session). |
| `2026-10-18_cascade_es_vix.md` | 2× size on cascade shorts after the VIX fell | `2026-10-18_vix_short_checks.md`, `2026-10-19_vest_adds.md` | 2× lowers return per drawdown; "prefer 1.5× (or keep 1×)". 1.5× V is NOT adopted for Vest (lowers the $40 pass rate). |
| `2026-10-17_ema_permission.md` | Plan page EMA permission | `2026-10-17_ema_stops.md`, `ema_entries_trails.md`, `ema_sweep.md`, `open_regime_ema.md` | The permitted EMA trade itself is ≈ breakeven or FAIL under every stop / entry / exit / filter. The permission limits losses; it is not an edge. |
| `2026-10-18_plan_checks.md` | Stand aside: cascade on CPI / PPI, gap retest on FOMC | `2026-10-17_manual_execution.md` (earlier), `2026-10-18_path_direction_extensions.md` | Skipping news days lowers sprint payout odds (24.6 → 20.8 %). The gap rule is better, not worse, on news days. Not a reversal of the adopted cells, but the evidence points the other way. |
| `2026-10-19_weekly_daily_profile.md` | Friday book trades at 1.5× | (same file) | Adopted despite Fridays 2025–26 −16.79 pts/trade on 75. The file sets its own drop condition (next quarter). |
| `2026-10-16_cascade_all_sessions.md` | cascade_asia_2h: passes gates but "NOT validated" | `2026-10-18_prop_daily_stop.md` addendum | The Asia cascade is listed among "One VALIDATED strategy per account" (75 % Vest pass) without that label being supported. `2026-10-18_combine_many.md` marks it ineligible (2010–18 −2.77). |

### 6. Other observations
- The project rule is "no Python". Two studies used Python outside the repo: `2026-10-17_fleet_consensus.md` ("scratchpad consensus.py")
  and `2026-10-18_zoo_study.md` (bot repo harness).
- Several "holdouts" are marked "not clean" in their own files, because the same 2025+ data was looked at repeatedly:
  direction_followups, path_direction_map, momentum_daily, bdh_optimize, momentum_optimize.
- `2026-10-18_follow_and_quad_close.md`: NQ.c.0 has 0 RTH bars on quarterly expiry Fridays. This affects every edge_lab result that does not exclude `rollwin`.

## Counts (218 rows from 118 files + 1 spec directory)

**By date:** 2026-10-16 55 · 2026-10-17 38 · 2026-10-18 76 · 2026-10-19 49

**By family:**
| family | rows |
|---|---|
| sizing/risk/prop-sim | 38 |
| direction/NY-open | 29 |
| exits/stops | 22 |
| cross-market (ES/VIX/macro) | 21 |
| price-action/patterns | 18 |
| EMA/indicators | 15 |
| calendar/news | 14 |
| overnight/sessions | 13 |
| order-flow | 11 |
| models/ML | 10 |
| pipeline/validation | 9 |
| mean-reversion | 8 |
| behaviour (descriptive) | 8 |
| VWAP | 1 |
| other | 1 |

**By verdict:**
| verdict | rows |
|---|---|
| FAIL | 115 |
| NOT ADOPTED | 31 |
| DESCRIPTION | 24 |
| PASS | 20 |
| ADOPTED | 15 |
| NO RESULT | 8 |
| OPEN | 5 |

The PASS rows that were never later adopted or overturned are mostly paper / forward candidates: cascade_gap_agree, gap_or_retest,
bb_on_gap, bos_ema_gap, mom_scan_3_pb, union_2r, scalp_m1_1000, rdir_m1 and the gap-retest feature refit.
The 15 ADOPTED rules:
- VWAP exit on gap-agree days
- the EMA permission
- stacking agreeing setups
- re-entry after a win
- the book ablation
- the VIX-fell short size rule
- the daily-path size rule
- path × direction (pd_gap / pd_mom)
- news-day stand-asides
- overlapping book F
- O3 overnight hold
- weakness long with a 1.5 ATR stop
- Friday 1.5×
- funded claim buffer
- the portfolio eval policy
