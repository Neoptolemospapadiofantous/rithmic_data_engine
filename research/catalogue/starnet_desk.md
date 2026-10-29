# D — StarNet strategy desk: research inventory (read-only, 2026-10-19 ~15:20 Cyprus)

Sources read: `~/starnet-work/desk/*.md` (13 files, 2026-10-16 16:37 and 2026-10-19 00:27–12:55), `holdout/desk_holdout.csv`,
`memory/*.md` (16 files), `rithmic_engine/research_data/edge_lab/{README,registry}.md` + `reports/desk_*.md`,
and the StarNet sidecar log (counts only). `~/starnet-work/findings/` is **empty**. No `*-EDGE-HOLDOUT.md` exists.
Files that do NOT exist on the new edge desk: QUANT, PROPDESK, FILLS (those jobs are OFF since 2026-10-16), any
`-RESEARCHER-RESEARCHER.md` or `-RESEARCHER-FLOWLAB.md`.

Stage key (edge_lab): 0 why · 1 screen · 2 walk-forward · 3 controls · 4 robustness · 5 prop (5 = PASS).
EDGELAB writes "FAIL at X, stage N" where N = last stage passed.

## 1. edge_lab ideas (the current desk, in-sample 2010-07 → 2024-12)

| desk id | date | proposing agent/lane | idea (≤ 15 words) | edge_lab verdict | holdout verdict | SKEPTIC verdict | BRIEF recommendation |
|---|---|---|---|---|---|---|---|
| desk_vwap_fade_quiet | 10-19 00:56 | RESEARCHER-REGIMES | VWAP band fade only on quiet days (prng < 0.6 ATR, low vixpct) | FAIL at SCREEN (st 0); OOS −4.66 pts, PF 0.64, n 103 | not reached | none (below st 3) | not recommended |
| desk_pibs_cplc_short | 10-19 00:56 | RESEARCHER-REGIMES | Short open→noon after strong prior close in complacent VIX regime | FAIL at SCREEN (st 0); OOS −1.74, PF 0.89, n 170 | not reached | none; weak-day effect looks one-sided | not recommended |
| desk_rsi2_baldayfade | 10-19 00:56 | RESEARCHER-REGIMES | RSI-2 extreme fade only after a prior balance day | FAIL at WALK-FORWARD (st 1); OOS +0.11, PF 1.01, n 197 | not reached | none; random time beats it (+875 vs +22) | not recommended |
| desk_tom_vwap_fade | 10-19 00:56 | RESEARCHER-REGIMES | VWAP fade inside turn-of-month window (intraday trigger on calendar effect) | FAIL at WALK-FORWARD (st 1); OOS +0.49, PF 1.03, n 529 | not reached | none; 2023-26 era −3.65 pts | not recommended |
| desk_vwapfollow_vixrise | 10-19 00:56 | RESEARCHER-REGIMES | VWAP band follow only when VIX rising | FAIL at CONTROLS (st 2); OOS +1.90, PF 1.10, n 563 | not reached | none; "volatility bet again", 2024 carries it (−1,199 without) | closest of cycle 1, "far from passing" — no |
| desk_mom1000_trendgate | 10-19 08:55 | RESEARCHER-REGIMES | 10:00 momentum only when prior 10 sessions straight + r10 agrees | FAIL at WALK-FORWARD (st 1); OOS −0.96, PF 0.96, n 313 | not reached | none; answers lead (3) negatively | (no BRIEF yet for cycle 2) |
| desk_cascade_norollwin | 10-19 08:55 | RESEARCHER-REGIMES | so_cascade (book F) excluding roll-window days | FAIL at CONTROLS (st 2); OOS +4.42, PF 1.21, n 1697, screen t 3.94/2.50 | not reached | none; random TIME beats it (1%); data-quality finding, not edge | (no BRIEF yet) — STEER: propose founder re-measure F with `!rollwin` |
| desk_cascade_rollwin_only | 10-19 08:55 | RESEARCHER-REGIMES | so_cascade on roll-window days only (matched pair) | FAIL at WALK-FORWARD (st 1); OOS −4.28, PF 0.81, n 204 | not reached | none; window drags F ~0.93 pts/trade | (no BRIEF yet) |
| desk_cascade_r5skip | 10-19 08:55 | RESEARCHER-REGIMES | so_cascade only after a choppy prior week (low e5) | FAIL at CONTROLS (st 2); OOS +4.03, PF 1.23, n 935 | not reached | none; random time 10%, ES −23 | (no BRIEF yet) |
| desk_cascade_r5skip_compl | 10-19 08:55 | RESEARCHER-REGIMES | complement: so_cascade after a straight prior week (high e5) | FAIL at CONTROLS (st 2); OOS +2.28, PF 1.12, n 893 | not reached | none; random dir 94% | (no BRIEF yet) |
| weakgapdown_stop | 10-19 12:55 | RESEARCHER-FLOWS | Gap-down weakness long with fixed 25/30/35-pt stop (Vest $150 limit) | **not judged yet** (next EDGELAB 16:00) | — | — | — |
| levg_etf_close_pret | 10-19 12:55 | RESEARCHER-FLOWS | Follow yesterday's big-return sign 15:30→15:55 (leveraged-ETF close rebalance) | FAIL at WALK-FORWARD (desk 10-19: +0.09 on 296); founder 2026-10-23 re-spec on TODAY's move (x23_letf_close_1530/1545) FAIL at SCREEN | — | — | — |
| macro_shock_follow | 10-19 12:55 | RESEARCHER-FLOWS | Follow 10y-yield / dollar shock (y10c, usdc) re-hedge sign, 10:30→15:55 | **not judged yet** | — | — | — |
| desk_of5_delta_follow (smoke test) | 10-16 16:37 | desk_edge.sh smoke test | Follow last 5m bar's delta 10:00–14:30 (2023-24) | FAIL at WALK-FORWARD; OOS −0.91, PF 0.96, n 364 (not in registry) | — | — | — |

Totals: 10 judged (66 variants), 0 in-sample passes, 0 holdout runs, 0 SKEPTIC ROBUST/FRAGILE/NOISE verdicts (nothing reached stage 3).
All 10 judged ideas came from ONE lane (REGIMES). BRIEF 07:30: "There is no new edge yet." 8 of 10 were before-the-day gates on
a dead family or on F itself; SKEPTIC 12:27 says one-filter cascade re-cuts structurally cannot pass controls (profit = the day's direction, not the 09:50 time).

### By-product the desk flagged for the founder
- Roll-window days drag the cascade: outside +4.42 × 1697 = +7,501 pts; inside −4.28 × 204 = −873; costs ~0.93 pts/trade blended
  (STEER 1240). Founder's `so_cascade.json` has no `!rollwin` (many other founder specs do). Proposal: re-measure F with `!rollwin`.
- Housekeeping (BRIEF): `holdout/desk_holdout.csv` is OLD paper-fleet data with a name that looks like edge-desk holdout output.

## 2. Old paper-desk variants in `holdout/desk_holdout.csv` (pre-2026-10-16 desk; 54 rows)

None counts: the rule needs ≥ 30 holdout trades and the largest here is 12. Verdicts 14 PASS / 21 FAIL / 19 THIN (as written by the old desk).
No SKEPTIC/BRIEF verdict beyond the BRIEF's "samples this small are anecdotes"; best by holdout net/ct: desk_fibpb1m_golden +137 (5 tr),
desk_ex_vbnt_sl12_chand +107.25 (8 tr, PF 1.83), desk_tl_vbch_pm +90.44 (9 tr, PF 4.14).

| strategy_id | IS trades | IS net/ct | holdout trades | holdout net/ct | holdout PF | holdout verdict |
|---|---|---|---|---|---|---|
| desk_ex_fib_sl12_be12o2_tr15 | 10 | 208.00 | 6 | -216.50 | 0.02 | FAIL |
| desk_ex_fib_sl12_be12o2_tr15_ch2 | 10 | 208.00 | 6 | -216.50 | 0.02 | FAIL |
| desk_ex_fib_sl12_be12o2_tr25 | 13 | 357.15 | 8 | -227.13 | 0.01 | FAIL |
| desk_ex_fib_sl12_be12o2_tr25_1t | 5 | 760.00 | 3 | -169.00 | 0.04 | THIN |
| desk_ex_fib_sl12_be8o2_tr10 | 10 | 184.50 | 6 | -169.83 | 0.04 | FAIL |
| desk_ex_fib_sl12_chand | 15 | 133.00 | 8 | -227.13 | 0.01 | FAIL |
| desk_ex_fib_sl12_ts45 | 10 | 184.50 | 6 | -169.83 | 0.04 | FAIL |
| desk_ex_golden_sl12_be8o2 | 8 | -43.38 | 4 | -189.00 | 0.03 | THIN |
| desk_ex_ichi_sl12_be12o2_tr25 | 15 | -91.67 | 9 | -97.33 | 0.11 | FAIL |
| desk_ex_ichi_sl12_chand | 15 | -33.00 | 9 | -97.33 | 0.11 | FAIL |
| desk_ex_orb5105_be6o2 | 11 | 17.36 | 9 | -59.00 | 0.19 | FAIL |
| desk_ex_orb5105_t2 | 8 | 93.50 | 6 | -35.67 | 0.02 | FAIL |
| desk_ex_vbnt_be8o2_tr10_t3 | 15 | 64.33 | 9 | 18.78 | 1.22 | PASS |
| desk_ex_vbnt_sl12_be12o2_tr25 | 15 | 442.33 | 8 | 6.63 | 1.05 | PASS |
| desk_ex_vbnt_sl12_chand | 15 | 329.67 | 8 | 107.25 | 1.83 | PASS |
| desk_ex_vm22_be10o2_tr15_t3 | 15 | 106.67 | 8 | 29.13 | 1.30 | PASS |
| desk_ex_vm22_be8o2_tr10_t3 | 15 | 40.33 | 9 | -57.89 | 0.50 | FAIL |
| desk_fibpb1m_golden | 14 | 6.71 | 5 | 137.00 | — | PASS |
| desk_fibpb1m_htf15 | 3 | 62.67 | 2 | 71.00 | — | THIN |
| desk_fibpb1m_micro_htf | 2 | -89.00 | 0 | — | — | THIN |
| desk_fibpb1m_sw45 | 20 | -53.00 | 8 | 1.00 | — | FAIL |
| desk_fibpb5m_deep | 9 | -56.78 | 4 | -64.00 | 0.01 | THIN |
| desk_fl_ddw_micro | 8 | -14.00 | 2 | 1.00 | — | THIN |
| desk_fl_failbo_inv3 | 2 | -89.00 | 1 | 1.00 | — | THIN |
| desk_fl_failbo_invctl | 2 | -89.00 | 1 | 1.00 | — | THIN |
| desk_fl_failbo_sginv | 2 | -89.00 | 1 | 1.00 | — | THIN |
| desk_fl_fibpb_sginv | 2 | -89.00 | 0 | — | — | THIN |
| desk_fl_vbnt_ctrl3 | 15 | 53.67 | 9 | 16.00 | 1.27 | PASS |
| desk_fl_vbnt_ctrl4 | 20 | 31.50 | 12 | 7.25 | 1.12 | PASS |
| desk_fl_vbnt_inv | 1 | -179.00 | 1 | 1.00 | — | THIN |
| desk_fl_vbnt_micro | 5 | -35.00 | 3 | -119.00 | 0.00 | THIN |
| desk_fl_vbnt_rv12 | 15 | 53.67 | 6 | 54.33 | 1.91 | PASS |
| desk_fl_vbnt_sg | 12 | 81.83 | 6 | 54.33 | 1.91 | PASS |
| desk_ichi1m_t4k11s22 | 15 | 43.00 | 9 | 29.89 | 1.61 | PASS |
| desk_ichi1m_t6k15s30 | 15 | -69.00 | 9 | 54.33 | 3.23 | FAIL |
| desk_tl_fb_fade_move10 | 5 | -35.00 | 3 | 1.00 | — | THIN |
| desk_tl_fb_r18_t3 | 9 | -59.00 | 9 | 41.00 | 1.69 | FAIL |
| desk_tl_fb_r18_t3_am | 9 | -59.00 | 8 | 68.50 | 2.53 | FAIL |
| desk_tl_fb_range_eff40 | 3 | -59.00 | 0 | — | — | THIN |
| desk_tl_fib_eff40_wm | 8 | -44.00 | 3 | 1.00 | — | THIN |
| desk_tl_fib_eff40_wm_sl12 | 6 | -0.67 | 2 | -119.00 | 0.08 | THIN |
| desk_tl_fib_move10_wm | 2 | 1.00 | 3 | 1.00 | — | THIN |
| desk_tl_fibtr25_1012 | 8 | 102.25 | 5 | -208.00 | 0.02 | FAIL |
| desk_tl_ichi_pm | 15 | -78.33 | 9 | -121.22 | 0.00 | FAIL |
| desk_tl_vbch_am | 10 | 309.50 | 6 | 35.17 | 1.20 | PASS |
| desk_tl_vbch_pm | 15 | 92.00 | 9 | 90.44 | 4.14 | PASS |
| desk_tl_vbnt_t3_pm | 15 | -39.00 | 9 | 29.33 | 1.49 | FAIL |
| desk_tl_vb_range_eff40 | 3 | 504.33 | 0 | — | — | THIN |
| desk_tl_vm22_r18_t3 | 9 | -39.00 | 8 | 10.38 | 1.12 | FAIL |
| desk_vb1m_notrend_d60 | 14 | -14.36 | 2 | 341.00 | — | THIN |
| desk_vbnt_vm16_cl05_t3 | 15 | -47.00 | 9 | -3.44 | 0.96 | FAIL |
| desk_vbnt_vm20_cl05_t3 | 15 | 53.67 | 7 | 63.86 | 2.25 | PASS |
| desk_vbnt_vm20_cl07_t3 | 15 | 41.67 | 7 | 89.57 | 4.50 | PASS |
| desk_vbnt_vm22_t3 | 15 | -35.00 | 5 | 89.00 | 2.24 | FAIL |

## 3. Desk ideas the founder's sessions did NOT test (name/grep check vs `research/README.md` + `research/ideas/*.json`, 442 specs)

| desk idea | founder coverage found | verdict |
|---|---|---|
| levg_etf_close_pret (yesterday's return → last-25-min follow) | `pret` only used in monday_fade / tuesday_turnaround / vol_target_delever / rgm_*_pdt; no LETF / close-rebalance spec | **judged**: desk FAIL at walk-forward; correct mechanism (today's move) FAIL 2026-10-23 (preregistered/2026-10-23_starnet_open_items.md X1) |
| macro_shock_follow (y10c / usdc as standalone trigger) | y10c/usdc only as FILTERS: cascade_rates_move(_compl), weak_day_flight(_compl), weak_day_usd(_compl) | **untested as own trigger; not yet judged** |
| desk_cascade_norollwin / rollwin_only (F split by roll window) | founder uses `!rollwin` in many specs, but `so_cascade.json` has none; DESK.md open lead (4) | **desk did it first** (desk result: window costs ~0.93 pts/trade) | — 2026-10-23 fixed book-F setting: roll trades +212 / +38 NQ (2010-18 / 2019-24) → skip NOT adopted (X4)
| desk_cascade_r5skip / _compl (prior-week straightness e5 on cascade) | so_cascade_eff/_not gate on SAME-DAY straightness; `r5` only in dpa_5; no e5 spec | **untested by founder** (failed controls here) |
| desk_pibs_cplc_short (strong prior close short, complacent VIX) | `pibs >=` only in cascade_pibs_agree(_compl) | **untested by founder** (failed screen) |
| desk_rsi2_baldayfade (RSI-2 after balance day) | no RSI-2 + balance-day spec found | **untested by founder** (failed walk-forward) |
| desk_tom_vwap_fade (VWAP fade in turn-of-month window) | turn_of_month and vwap_fade exist separately, not combined | **untested combination** (failed walk-forward) |
| desk_vwapfollow_vixrise (VWAP follow, VIX rising) | vwap_follow (plain), highvix_vwap_fade (VIX LEVEL, fade) | **untested combination** (failed controls) |
| desk_vwap_fade_quiet | quiet_1135_vwap, range_day_fade (quiet/range regime) — close cousins | partly covered |
| desk_mom1000_trendgate | open_momentum_all, dpa_10 / dpf10_* path specs; DESK.md lists multi-day path gates on the 10:00 rule as closed | covered |
| weakgapdown_stop (fixed-pt stop) | README row 216: gap-down weakness long with 0.5–1.5 ATR stops — **1.5 ATR ADOPTED for Vest** 2026-10-19 | covered (ATR not fixed-pt; desk idea is a near-duplicate) |

Net: two genuinely new, still-pending ideas (levg_etf_close_pret, macro_shock_follow); the rest either failed or duplicate founder work.

## 4. Code findings still open
`~/starnet-work/findings/` is **empty** — no open code findings. (The code crew REGRESSION / DOCSYNC / SCANNER / TESTGAP /
RISKGUARD / HERMES is OFF since 2026-10-16; their memory files' newest lessons are 2026-09-30 / 10-01, or empty.)

## 5. Desk health since the resume (2026-10-18 21:00 Cyprus → 2026-10-19 ~15:20)

Output files written since the resume: **11** — SKEPTIC ×3 (00:27, 04:27, 12:27), RESEARCHER-REGIMES ×2 (00:56, 08:55),
EDGELAB ×2 (04:00, 12:00), STEER ×2 (04:40, 12:40), BRIEF ×1 (07:30), RESEARCHER-FLOWS ×1 (12:55).

Missing vs the schedule (4-hourly cycle; researchers at :48/:54):
- EDGELAB 00:00 and 08:00: no file (nothing new to judge at 00:00; 08:00 ran before the 08:55 researcher file — consistent with "skip on no input").
- STEER 00:40 and SKEPTIC 08:27: no file (likely "no new input" skips).
- **Lanes silent**: RESEARCHER (00:48, 08:48) 0 files; FLOWLAB (04:48, 12:48) 0 files; FLOWS 04:54 no file (12:55 wrote). So order flow inside the 5m candle has **0 desk ideas**, and the main RESEARCHER lane has never written on the edge desk. STEER 0440 and 1240 and BRIEF 0730 all flag this.
- StarNet sidecar log since the re-arm: **17 cron runs, all `outcome: ok / done`, 0 failed**, 5 individual shell tool calls returned isError (normal command failures). Two short runs (researcher-3 1 turn, researcher 2 turns) are consistent with skip-on-no-input; one researcher-4 run of 15 turns left no lane file (empty output). Agent→lane mapping is inferred from turn counts, not confirmed.
- **No "No such tool available", "tools are not connected", "untrusted-content-lockout" text** in any desk/memory file or in the sidecar log.
- Pre-resume gap: no EDGELAB between 10-16 16:37 and 10-19 04:00 (~59 h, ~14 cycles) — flagged by SKEPTIC 0027 and BRIEF.
- Memory: lessons written since resume only in DESK.md (founder's 10-19 STATE), SABLE.md, SKEPTIC.md, TIMELAB.md (newest 2026-10-19); RESEARCHER.md, FILLS.md, HERMES.md, RISKGUARD.md, TESTGAP.md are empty.

## 2026-10-23 — desk stopped, leftovers closed
StarNet stopped on the founder's word (scheduled runs lost their tools mid-run; no output after 2026-10-22 00:41). Every open item was
tested or found already closed: research/preregistered/2026-10-23_starnet_open_items.md. Nothing passed; the roll-window skip is not
adopted. The desk ends with 15 desk ideas, 0 PASS.
