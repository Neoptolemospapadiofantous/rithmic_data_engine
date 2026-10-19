# C — Non-edge_lab research history (rithmic_engine), inventoried 2026-10-19

Scope: everything EXCEPT the edge_lab pipeline itself (research/README.md ledgers, research/ideas, research/preregistered run
through `build/edge_lab`). Where a 10-16 … 10-19 study was run by a one-off program in `research/programs/` it is listed here
(those are not edge_lab runs). Sources abbreviated:
- **LEDGER** = Postgres `research_tests` (108 rows, 2026-10-11 … 10-15; queried read-only)
- **CH** = CHANGES.md (dated entry), **MEM** = auto-memory note (`memory/<name>.md`), **DOC** = docs/*.md, **RPT** = data/*/report.md,
  **PROG** = research/programs/<name>.cpp + out/<name>.txt

Verdict vocabulary: worked / weak / failed / superseded / invalid / live / paper / dry (executor dry_run) / research-only.
Ledger verdicts are mapped: `edge`→worked, `weak`→weak, `no edge`→failed, `invalid`→invalid.

## 1. Study table

| id | date | area | question (≤15 words) | verdict / status | key numbers (≤20 words) | source |
|---|---|---|---|---|---|---|
| fleet-engines | 2026-09-21 | paper fleet | Run ORB/trend/mtf strategies side by side on paper with honest fills? | infrastructure / paper | 110→197 strategies; feed-gap guard removed 36 phantom winners (+$1,081) on 09-21 | MEM project_paper_fleet_engines |
| book-overlays | 2026-09-23 | paper fleet | Do book gates (spread, imbalance, microprice) improve fleet bases? | mostly failed; `__inv` kept | 1,553 variants; only `__inv` (fade stacked side) beat bases: PF 2.19 vs 0.92 | MEM paper_fleet_engines; CLAUDE.md |
| tape-modes | 2026-09-25 | paper fleet | Do tape modes (absorption, delta divergence, volume burst) have edge? | weak → later failed | volume_burst_1m PF 8.1, delta_div PF 2–3.5 on 16 trades (5 days); died in 10-16 sweep | CLAUDE.md; CH 10-16 |
| book-imb-trigger | 2026-09-25 | order flow | Book imbalance as an entry TRIGGER (follow or fade)? | failed, removed | fade won 1 of 16; follow likewise | CLAUDE.md |
| handoff-rotation | 2026-09-25 | execution/live | ORB → hand-off engine at 10:00, weekly rotation by leaderboard? | superseded (removed 09-29) | FIB_PB_1M_DEEP live 09-28/29 at 1→5 MNQ, then removed by founder | MEM project_scalper_handoff; CH 09-29 |
| per-contract-rank | 2026-09-26 | paper fleet | Rank strategies per contract instead of raw net P&L? | adopted | mtf variants sized 8–16 lots had inflated net; leaderboard now net_per_contract | CLAUDE.md |
| tod-windows | 2026-09-26 | paper fleet | Is time of day a first-class variable for each mode? | adopted rule | `__open`/`__am` siblings; `__pm` dropped; same mode wins one hour, loses another | CLAUDE.md; CH 09-26 |
| prune-0926 | 2026-09-26 | paper fleet | Retire modes/overlays that lost on the 7-day forward test? | superseded 09-29 | 635 of 1,675 ran; reversed: "all run for ever" (2,170→6,657 ids) | CLAUDE.md; CH 09-29 |
| regime-gate | 2026-09-28 | regime | Gate entries on session shape (range/eff/move vs ATR)? | paper (8 `__rg_*` keepers) | blocks 55–80% of entries; no gate beat bases on average | MEM project_regime_gate |
| take-profit | 2026-09-28 | paper fleet | Does a fixed take-profit help trailing strategies? | failed | loses at every R; 3R on classic ORB ≈ neutral | MEM project_regime_gate; CH 09-28 |
| trail-25 | 2026-09-28 | paper fleet | Exit-side regime vs plain wider trail? | worked (paper `__tr25`) | trail_step 25 beat base on 7/8 bases by 2–4× over 6 sessions | MEM project_regime_gate |
| newmodes-0928 | 2026-09-28 | paper fleet | orb_retest, gap_fade, news_break, ib_break modes? | weak; 4 keepers paper | ib_break PF 0.35; others flat/tiny over 6 sessions | CH 09-28 |
| es-mirror | 2026-09-28 | paper fleet | Run top NQ bases on ES (MES) as a mirror fleet? | paper (label es) | 24 bases, knobs scaled by ATR ratio | CH 09-28 |
| volgrid | 2026-09-29 | paper fleet | Relative-volume / vol-target filters, atr_break, vprofile modes? | weak / failed | rvol≥2.5 +$32.8/ct but 3 trades; quiet tape −$10.3/ct; atr_break/vprofile negative | CH 09-29 |
| beta-holds | 2026-09-29 | overnight/holds | Beta exposure holds (overnight 15:55→09:30, 18:00 reopen, day)? | paper; noise | 6-session backfill: overnight long −$433, day long +$649 | CH 09-29 |
| paper-costs | 2026-09-30 | execution/live | Are paper costs honest vs live fills? | corrected | commission 1.0→1.82, slippage 1→3 ticks; stops never better than print | CH 09-30 |
| warmup-restart | 2026-09-30 | paper fleet | Do restarts corrupt forward results? | corrected | 3 of first 6 sessions were restart days; 1600-bar warm-up, restart_sessions column | CH 09-30 |
| feed-drop | 2026-09-30 | execution/live | Does paper DB load starve the collector's tick writer? | fixed | "Writer queue full" dropped ticks; queue 8192, change-only flushes | CLAUDE.md |
| live-orb-1nq | 2026-09-29 | execution/live | Live = ORB only on 1 NQ, then MNQ sizing by room? | superseded (stopped 10-11) | ORB +$337 / 30 trades since 09-21; Growth 25K room ~$90 | CH 10-11; MEM live_switch |
| fleet-expand | 2026-10-01 | paper fleet | Staged winners/newmodes/markets/combos expansions? | staged / research | 673 winners, 124 newmodes, ~1,610 per market, 12,031 combos (label nq_combo) | CLAUDE.md |
| liq-gate | 2026-10-04 | regime | Liquidity gate (vol/min, pts per 1k contracts) overnight? | weak | gate cuts London momentum median −$603 → −$173..−$231; none positive | CH 10-04 |
| night-grid | 2026-10-04 | overnight/holds | Asia/London grid (948→1,764 nt_* combos) by window×family×gate? | failed (19 sessions) | 1 of 948 consistent (nt_ldl_mtf_htf30_v5_g0 PF 1.55); bounce loses everywhere | RPT data/night/report.md; CH 10-04 |
| slot2 | 2026-10-04 | paper fleet | Best second strategy for 10:30–12:00 after ORB? | failed so far (no PASS) | 10 fwd sessions: only s2_mr_rsi2_15m +$623 PF 1.83 (12 trades); 10 of 11 negative | RPT data/slot2/report.md |
| trend-confirm-grid | 2026-10-07 | paper fleet | Founder's trend_confirm mode with stacked confirmations (2,432 variants)? | research grid (tcf label) | daily tcf-eval timer; no promoted result recorded | CH 10-07 |
| absorption-zone | 2026-10-07 | order flow | Founder's Absorption Strategy Spec as a trend mode? | paper; no edge shown | built all scenarios; absorption rarely fires; later all order-flow tests negative | CH 10-07 |
| sig-signals | 2026-10-08 | execution/live | Publish manual signals (sig_*) for the most-WR, low-DD strategies? | live-ish (alerts), no proven edge | sig_ny_trend_vwap_rgx 23 trades 4% losers +$1,328 over 8 sessions | CH 10-08 |
| live-rota | 2026-10-08 | execution/live | Regime-picked strategy per session via hand-off timers? | superseded (disabled 10-11) | evidence = one replay, 7–17 trades/cell; sig_* "no proven edge" | MEM project_live_rota; CH 10-11 |
| crypto-mirror | 2026-10-10 | crypto | Every NQ strategy mirrored to BTC/ETH perps? | paper (labels crypto, crypto_hist); failing | 9,690 ids; Binance 0.10% RT fee ≈ one BTC stop; fees decide | MEM project_crypto; CH 10-10 |
| history-mbo | 2026-10-10 | paper fleet | Replay whole fleet on Databento MBO 2023-03…2026-03 + 1m bars to 2010? | failed (ranking) | 9 months: 0 of 3,933 pass robust gate; ~85% lose before costs | CH 10-11; RPT data/history/report.md |
| history-report | 2026-10-13 | paper fleet | Fleet net per month across replayed history? | failed | every 2024 month −$0.8M…−$1.9M net/ct summed; 120 done / 581 pending | RPT data/history/report.md |
| bars-vs-ticks | 2026-10-11 | other (data) | Do 1m-bar replays match tick replays? | corrected | bar stops −20.4 vs −11.2 pts; 56/4,479 matched → bar_path fix | CH 10-11 |
| price-levels | 2026-10-11 | paper fleet | Price levels as gates/targets/stops (lv_room/lv_conf/lv_tgt/lv_stop)? | weak; paper only | 1,728 variants; lv_conf PF 1.4–2.2 over 5 yrs; S15 test PF modest | CH 10-11; LEDGER S15 |
| walk-forward-wf | 2026-10-11 | walk-forward | Walk-forward parameter pick per family × session window? | failed | 3-mo train: 10 of 82 groups positive, summed −$48,494 | RPT data/walk_forward/report.md |
| payout-sim-v1 | 2026-10-11 | payout/prop sim | Which history strategy pays fastest on Tradeify Flex/Growth? | superseded | beta_on_1800 top test EV ~$1,388 Flex 25K; superseded by rules-correct sim | RPT data/payout/report.md |
| tradeify-rules | 2026-10-11 | payout/prop sim | Real Tradeify rules (flat 16:45, Select Daily) in payout_sim? | worked (model) | biweekly payout not reachable: ≤6% within 10 days | CH 10-11 |
| lv-conf-cadence | 2026-10-11 | payout/prop sim | Cadence of lv_conf family on Select Daily 50K? | weak | win10 47–51%; staggered accounts add nothing; eval+buffer 3–4 months | CH 10-11; MEM daily_context_holds |
| D1-weak-close-hold | 2026-10-11 | overnight/holds | Weak close (IBS<0.2) → buy 18:00 → sell 15:55 next session? | worked (research-only on Tradeify) | 669 tr, PF 1.49, t 3.0, 15/17 yrs; ES PF 1.36 | LEDGER |
| D2-weak-close-rth-only | 2026-10-11 | intraday search | Same signal, RTH 09:30→15:55 only? | weak | PF 1.31, t 2.0; half the edge | LEDGER |
| D3-daily-sweep | 2026-10-11 | overnight/holds | 7 daily families × ~70 variants (IBS, RSI2, streaks, fear, FOMC, spread, Donchian)? | worked | 21 survive; all long weakness mean reversion; ibs<0.1 xs PF 3.12 | LEDGER; CH 10-11 |
| D4-gap-and-go-vix | 2026-10-11 | regime | Gap-and-go conditioned on VIX? | weak | gate on PF 1.31 8/14 yrs; off PF 0.85 | LEDGER |
| I1-ml-15m | 2026-10-11 | intraday search | Walk-forward LightGBM, 69 indicators on 15m bars? | failed | OOS IC 0.02–0.04; gross 0.05–0.10 ATR vs 0.2 cost | LEDGER |
| I2-day-type-0945 | 2026-10-11 | intraday search | Predict day type at 09:45 and trade it? | failed (signal, no strategy) | range IC 0.51 every year; ATR-ORB still loses | LEDGER |
| I3-ema-pullback-structure | 2026-10-11 | intraday search | EMA20/50 pullbacks in stacked regime, 5m/15m? | failed | 4,175 tr PF 1.01; gross 0.4–0.6 pts vs 2.4 cost | LEDGER |
| I4-passive-entries | 2026-10-11 | execution/live | Limit entries (−1 tick) for lv_conf? | failed | fills 78–85% but PF 1.43 → 0.35 | LEDGER |
| M1-multi-asset-sweep | 2026-10-11 | other (cross-asset) | Weakness-long / TSMOM on 15 futures + 18 ETFs daily? | failed beyond equity indices | edge only US equity indices; first run's bond "shorts" were a cost-sign bug | LEDGER; CH 10-11 |
| M1-daily-phases | 2026-10-11 | regime | 4 market phases × down day / vol expansion daily? | failed | 28 variants, 1 holds 2019-26 | LEDGER |
| M2-effort-result | 2026-10-11 | other (Wyckoff-style) | Effort vs result on daily bars? | weak (= weak close) | 1 holds, PF 1.89 NQ; it is a weak close | LEDGER |
| M3-cause-effect | 2026-10-11 | other (Wyckoff-style) | Tight base then breakout/breakdown/spring? | failed | 24 variants, 0 pass | LEDGER |
| M4-waves | 2026-10-11 | other | Zigzag/ABC/pullback wave rules daily? | failed | 14 variants, 0 pass | LEDGER |
| N1-execution-bound | 2026-10-11 | execution/live | Fleet at free execution (upper bound for limits)? | failed | 180 of ~4,400 positive, 18 in both years | LEDGER |
| N2-fomc / release | 2026-10-11 | intraday search | Follow/fade 08:30 and 14:00 release moves? | failed | FOMC follow PF 0.80; 08:30 follow PF 0.70 t −6.4 | LEDGER (3 rows) |
| N3-vol-buckets | 2026-10-11 | regime | ATR buckets on every N-test? | failed | no bucket once IB look-ahead removed | LEDGER |
| N4-relative-strength | 2026-10-11 | intraday search | NQ vs ES relative strength at 10:30? | failed | 4 variants PF 0.62–1.22 | LEDGER |
| N5-ib/outside | 2026-10-11 | intraday search | IB breakout; open outside prior range (holds / back inside)? | failed | IB first read PF 1.91 was look-ahead; fixed PF 1.00 | LEDGER (3 rows) |
| N7-target-bound | 2026-10-11 | paper fleet | Fixed profit targets filling first (upper bound)? | failed | ≤12 of 4,264 positive; exits are not the fix | LEDGER |
| N8-calendar | 2026-10-11 | intraday search | OPEX, quad witching, month-end, pre-holiday, last 10 min? | failed | PF 0.35–0.93 | LEDGER |
| N9-swing | 2026-10-11 | intraday search | Follow open→10:00 move ≥0.3 ATR to close? | failed | NQ PF 1.11, ES 0.82 | LEDGER |
| R7 family (10 rows) | 2026-10-11 | intraday search | Cost sensitivity, first hour, gap fade, momentum, Monday, OF, overnight trend, TOM, weak close legs? | mostly failed; weak-close cc worked | weak close c→c PF 1.41 12/17; Monday PF 1.40 t 1.4; rest ≤1.15 | LEDGER |
| R8 family | 2026-10-11 | intraday search | Big down day, down streak, late-day reversal, weekly VP edges? | weak (big down day) / failed | big down day PF 1.91 t 3.0 unstopped, 1.20 with stop; VP PF 0.84 | LEDGER (5 rows) |
| R9-weakness-system | 2026-10-11 | intraday search | Intraday buy-weakness system: 7 signals × 5 entries × 4 stops? | weak | only big-down-day holds both halves (PF 1.61/2.00) | LEDGER |
| R10-fear-daily / intraday | 2026-10-11 | intraday search | Buy fear (daily triggers; intraday drops)? | weak / failed | daily 9 of 48 hold (PF 2.46 best); intraday 1 of 288 | LEDGER |
| R11-confluence-pullback | 2026-10-11 | intraday search | VWAP+EMA20 confluence pullback, 144 variants? | failed | 0 of 144 hold both halves both markets | LEDGER |
| R12-wf-model | 2026-10-11 | intraday search | Walk-forward logistic/GBM on 14 daily features → day direction? | failed | PF 0.91–1.03; 0.60 threshold carried by 2025 | LEDGER |
| V1-vix-regime-weakness | 2026-10-11 | regime | Does VIX regime scale the weakness hold edge? | worked | VIX<15 ≈ $0; >30 +$81/MNQ; rsi2<20 & VIX≥20 PF 2.09 14/16 yrs | LEDGER; CH 10-11 |
| V2-vix-intraday | 2026-10-11 | regime | Do intraday families improve by VIX regime? | failed | fear makes breakouts less losing: ORB PF 0.36→0.81 | LEDGER |
| V3-day-only-weakness-vix | 2026-10-11 | intraday search | Day-only weakness hold with VIX gates? | worked | any weakness & VIX≥20 +$50/MNQ PF 1.54; rsi2 PF 1.77 | LEDGER |
| V4-pre-fomc-drift | 2026-10-11 | overnight/holds | Enter evening before FOMC, exit 15:55? | weak | +$41/MNQ PF 1.55 12/17; day after −$42 | LEDGER |
| X1-conditional-exits | 2026-10-11 | overnight/holds | Early take/cut/target/trail on the gated book? | failed (hold-to-close best) | avg $54 PF 1.52 with hold; every early exit worse | LEDGER |
| X2-earnings-nights | 2026-10-11 | overnight/holds | Hold P&L when top-10 NQ weight reports after close? | weak (keep trading) | +$38 vs +$48/MNQ otherwise | LEDGER |
| X3-crypto-daily | 2026-10-11 | crypto | BTC/ETH daily weakness, fear, TSMOM? | weak, fading | BTC PF 1.46 t 3.0, h2 near zero; shorts negative | LEDGER |
| S1-day-session-split | 2026-10-11 | intraday search | Split weakness edge into overnight vs day legs? | worked (the intraday survivor) | weak & VIX≥20 day-only +$52.8 PF 1.60 t 3.3, 33/yr | LEDGER; DOC intraday_search |
| S2-day-session-mechanics | 2026-10-11 | intraday search | Better entry/exit timing, dips, stops for S1? | failed (base best) | market 09:30→15:55 +$55 PF 1.62 best cell | LEDGER |
| S3-regime-conditioned-fleet | 2026-10-11 | paper fleet | 5,853 fleet strategies on weak & VIX≥20 days only? | failed | 17 of 5,853 both halves (chance); train leaders negative OOS | LEDGER |
| S4-at-open-conditions | 2026-10-11 | intraday search | Every known-at-09:30 feature vs day return? | weak | long side = weakness trade; short side tiny after cost fix | LEDGER |
| S5-last-hour | 2026-10-11 | intraday search | Short into the close on down days? | failed | +$4–8/MNQ PF 1.15–1.28, 7/17 yrs | LEDGER |
| S6-weekly-consistency | 2026-10-11 | payout/prop sim | Strategies netting ≥$250/week consistently (9,295)? | failed | every top-25 negative out of sample | LEDGER |
| S7-multi-account-cadence | 2026-10-11 | payout/prop sim | Payout cadence 1/2/3 accounts on different sleeves? | weak | 1 acct overnight book 0.4–0.6 payouts/wk | LEDGER |
| S8-intraday-cadence | 2026-10-11 | payout/prop sim | Payout cadence of intraday sleeves? | failed | 1–6% of weeks with a payout | LEDGER |
| S9-crypto-sessions | 2026-10-11 | crypto | BTC/ETH hourly/session seasonality? | failed | 1–5 bps seasonality, ~1 bp net | LEDGER |
| S10-order-flow-ml | 2026-10-11 | order flow | Walk-forward LightGBM on 45 MBO/book features? | failed | IC ≤0.02; PF 0.93, loses 8–9 of 10 quarters | LEDGER |
| S11-nq-es-pairs | 2026-10-11 | intraday search | Intraday NQ/ES pairs reversion? | failed | −$10/pair every setting, PF 0.55, 0/17 yrs | LEDGER |
| S12-fomc-day | 2026-10-11 | intraday search | FOMC decision-day intraday windows? | weak | last-hour short PF 1.48, 8/yr, halves split | LEDGER |
| S13-gap-fade | 2026-10-11 | intraday search | Gap fade ≥0.3–0.8 ATR toward prior close? | weak | ≥0.8 ATR +$17/MNQ PF 1.35, years noisy | LEDGER |
| S14-validation-s1 | 2026-10-11 | other (validation) | Independent recompute + MBO replay of S1? | worked (confirmed) | n=526 PF 1.60 t 3.30 14/16; engine matches day for day | LEDGER; CH 10-11 |
| S15-fleet-matrix | 2026-10-11 | paper fleet | Factorial scoring of 8,207 replayed variants, overlays and regimes? | failed | level overlays improve 77–85% of bases but test PF stays low | LEDGER; DOC intraday_search |
| S16-exit-resim | 2026-10-11 | paper fleet | Re-simulate exit grid on 1.15M real entries? | failed | engine exits destroyed value: test PF 0.53 → 0.92 at best | LEDGER |
| P1-conviction-book | 2026-10-11 | payout/prop sim | Conviction-stacked weakness book on Select Daily 50K? | INVALID (→P3) | claimed 53% of 2-wk windows, $1,590/mo — per-unit MAE bug | LEDGER; DOC weakness_vix_book |
| P2-venue-comparison | 2026-10-11 | payout/prop sim | Venues allowing overnight hold (FTMO Swing, Phidias)? | weak | FTMO 100k ~$688–700/month, payout every ~7 weeks | LEDGER |
| P3-stack-mae-correction | 2026-10-11 | payout/prop sim | Correct P1 with position-level MAE? | correction | stack breaches $2K trail ~12×/yr; 1 MNQ ~2.8×/yr | LEDGER |
| conviction-sizing | 2026-10-11 | execution/live | Executor sizes from strategy-requested units? | built; dry (tradeify_book) | 1–4 units, qty_max 4 | CH 10-11 |
| book-switch | 2026-10-11 | execution/live | Stop ORB, run weakness books dry? | done | tradeify_book (overnight) + tradeify_day (intraday) dry | CH 10-11; MEM live_switch_20261011 |
| C1-combo-book | 2026-10-12 | overnight/holds | Combination of daily weakness signals, long open→15:59, stacked NQ+ES? | worked; dry (tradeify_combo, _es) | 2019-26 n=451 $158/day PF 1.70 8/8 yrs, maxDD −$6.3k | LEDGER; CH 10-12 |
| B1-intraday-battery | 2026-10-12 | intraday search | Weak intraday leads re-tested at today-price scale? | weak | only G1 (low-GEX) passes; half its trades are fear days | LEDGER |
| D1-daily-target | 2026-10-12 | intraday search | One trade/day with small target, wide stop? | failed | WR 85–92% but avg ≤0 pts/day | LEDGER |
| F1-15m-daily-filters | 2026-10-12 | regime | Daily-context filters on 514 15m strategies? | failed | chosen-on-H1 sets lose on H2 for every filter | LEDGER |
| F2-daily-feature-sweep | 2026-10-12 | regime | 30 new daily features known at 09:30? | failed | 0 of 176 tests pass both halves | LEDGER |
| F3-stx-short-score | 2026-10-12 | regime | Pre-registered Supertrend-15m SHORT score filter? | failed | in-sample PF 2.8–3.3 did not hold | LEDGER |
| F4-5m-ticks / F5-1m-ticks | 2026-10-12 | paper fleet | All 5m / 1m fleet variants on 2024-03..2026-10 ticks? | failed | 5m: 1 of 1,315 both halves; 1m: 0 of 1,239 net positive | LEDGER |
| G1-gex-dix-regimes | 2026-10-12 | regime | SqueezeMetrics GEX/DIX as intraday filters? | failed (range regime only) | next-day range 1.22 ATR when GEX<0 vs 0.94 high | LEDGER |
| I5-intraday-scan | 2026-10-12 | intraday search | Every 30-min window × side × 11 regimes; shock fade; lead-lag? | failed | 0 of 2,266 chosen hold; fair re-run 0 of 17 | LEDGER |
| L1 / L1b noise-area momentum | 2026-10-12 | intraday search | Zarattini noise-area momentum (± low-GEX)? | weak | NQ 2019-26 PF 1.22–1.36, 2011-18 weak | LEDGER |
| L2-gamma-last-half-hour | 2026-10-12 | regime | Last-half-hour momentum when dealers short gamma? | weak; later dropped | NQ PF 1.28; fails 2011-18 (V6) | LEDGER |
| L3-month-end-rebalance | 2026-10-12 | other (flows) | Month-end stock-bond rebalancing on last day? | worked | NQ 2011-18 PF 1.85, 2019-26 PF 1.75; ES 1.62/1.50 | LEDGER |
| M1-fear-other-indices | 2026-10-12 | other (cross-asset) | Fear-day book on RTY/YM/SOX? | worked | 2019-26 PF NQ 1.59, ES 1.36, RTY 1.35, YM 1.46, SOX 1.80 | LEDGER |
| ML1-orb-meta | 2026-10-12 | walk-forward | ORB grid with walk-forward settings + meta-model? | failed | walk-forward NQ −$3,500 over 13 yrs | LEDGER |
| ML2-fleet-selection | 2026-10-12 | walk-forward | Pre-registered selection over 6,477 fleet strategies? | failed | 165 pass train, 0 pass validation; 3% positive test | LEDGER |
| ML3-all-strategy-pipeline | 2026-10-12 | paper fleet | 6,430 strategies × 144 exits, one position at a time? | failed | overlapping version faked p<0.005 edges; sequential = none | LEDGER; MEM feedback_exit_resim_traps |
| P4-vol-sizing-firms | 2026-10-12 | payout/prop sim | Volatility sizing of C1 across firms? | weak (adds nothing) | at today's ATR R=300/600 sizes 1 micro anyway | LEDGER |
| P5-firms-official-rules | 2026-10-12 | payout/prop sim | C1 through each firm's official rules? | weak | MFF Rapid 50K best EV $14.2k; daily-payout plans fit ~30 tr/yr | LEDGER; CH 10-12 |
| P6-daytrade-variants | 2026-10-12 | payout/prop sim | Looser fear-day thresholds on Select Daily 50K? | weak | 2 MNQ base EV $1,957, blown 12%, 2.8 pay/mo | LEDGER |
| P7-daytrade-portfolio | 2026-10-12 | payout/prop sim | Fear + noise + gamma + month-end sleeves combined? | weak | 10.1 pts/day, Sortino 2.56 (gamma30 later dropped) | LEDGER |
| P8-150k-eod | 2026-10-12 | payout/prop sim | Fear book at today's price on a 150K EOD-trailing account? | weak | 1 MNQ eval pass 36%, ~$500/month funded | LEDGER; MEM select50_combo |
| V5-combo3-engine-parity | 2026-10-12 | other (validation) | C++ engine vs research day list for combo3? | worked (parity) | 709 vs 706 trades; PF 1.55 vs 1.49 | LEDGER; CH 10-12 |
| V6-independent-validation | 2026-10-12 | other (validation) | Independent recompute + placebo + today-price normalisation? | worked, with caveat | fear NQ PF 1.57 t 3.1; but 36-pt stop kills edge at today's price | LEDGER; MEM select50_combo |
| select50-plan | 2026-10-12 | execution/live | Select Daily 50K config for combo book (2 MNQ, 36-pt stop)? | superseded / not viable | V6: stop hit ~90% of fear days; dry only | MEM project_select50_combo |
| prop-coin-flip | 2026-10-13 | payout/prop sim | Prop EV of a coin-flip strategy vs fleet? | worked (framing) | coin flip ≈ $70 per eval; fleet worse than coin flip | MEM project_prop_account_math |
| opening-momentum | 2026-10-13 | intraday search | 22 directional signals vs coin flip in prop shape? | worked (only opening momentum) | +0.09..+0.17R vs coin in all eras | MEM project_prop_account_math |
| N10-overnight-window-scan | 2026-10-13 | overnight/holds | Every 30-min overnight window × 12 conditions? | failed | all = long overnight drift (PF 1.20) | LEDGER |
| N11-big-aggressor-flow | 2026-10-13 | order flow | Follow big MBO aggressor prints? | failed | 180 settings, best test t 1.15 | LEDGER |
| N12–N16, N19 flows | 2026-10-13 | other (flows) | Vol-target deleverage, quarter-end, LETF, CTA triggers, pre-FOMC, intraday momentum? | failed | all train/test t ≤ 2; quarter-end reversed after 2018 | LEDGER (6 rows) |
| N17-buyback-window | 2026-10-13 | other (flows) | Fear-day trade inside buyback open window? | weak | NQ $91 vs $48 blackout 2019-26, placebo p 0.017 | LEDGER |
| N18-morning-inst-flow | 2026-10-13 | order flow | Morning net aggressor volume → follow to close? | weak | $47/trade t 1.73 (2023-26) | LEDGER |
| ofo-orb | 2026-10-14 | order flow | ORB on NY 5m / Asia 15m with order-flow agreement? | paper (fragile / artefact) | NY PF 1.27 CI [0.99,1.62]; Asia honest PF 1.17 | CH 10-14; MEM project_of_orb |
| vix-fall-doubling | 2026-10-14 | regime | Prior VIX close fell ≥3% → double the OF ORB? | paper (hypothesis) | NY PF 1.79/2.11; adds nothing before 2019 | CH 10-14 |
| research-rounds-4-8 | 2026-10-14 | other (flows) | Late-day flows, calendar, big prints, profile, IB, crypto day trades, x-asset? | failed (~6,000 configs) | all negative; E1 strong open confirmed PF 1.33 (~+0.05 ATR) | MEM project_of_orb |
| late-so | 2026-10-15 | intraday search | Late strong-open re-checks + looser weak day? | paper (5 ids) | +92 tr/yr, +0.10R all eras; picked from ~18 cells | CH 10-15; MEM late_so_fleet |
| cascade | 2026-10-15 | intraday search | Every-day momentum cascade (09:50 → 12:00)? | paper; dry (tradeify_cascade) | PF 1.16 at today's cost; PF 1.07 real cost, negative 2010-18 | CH 10-15 |
| direction-signals | 2026-10-15 | order flow | VWAP side / bands / delta / book imbalance → day direction? | failed | 42 tests, best t 1.9 | CH 10-15 |
| vwap-band-fade | 2026-10-15 | intraday search | VWAP SD-band fade 1m/5m 2010-26? | failed | 2/72 and 0/120 positive all eras | CH 10-15 |
| rl-sizing | 2026-10-15 | payout/prop sim | Tabular Q-learning daily size on a 50K? | failed | +$2.5k/yr vs fixed 4 MNQ +$7.3k | CH 10-15 |
| BK1-daily-book-1to3 | 2026-10-15 | payout/prop sim | 1–3 trades/day book (E2 + fade + E1 + L1)? | weak | 1.45 trades/day; PF 1.44, all eras >1.2 | LEDGER |
| flex-pipeline | 2026-10-15 | payout/prop sim | Flex vs Daily for the cascade; eval big / funded small? | worked, then corrected | first payout 35–41 days → corrected ~17 weeks (83 trading days) | MEM project_flex_pipeline |
| cross-firm-hedge | 2026-10-15 | payout/prop sim | Hedge long at Tradeify vs short at another firm? | failed (banned) | Tradeify FTA §6.7 bans opposite positions vs external accounts | MEM project_crossfirm_hedging |
| family-sweep | 2026-10-16 | walk-forward | All 24 fleet families walk-forward (3,933 + 1,840 variants)? | failed except 3 | survivors weak_day, SO-agree PF 1.44, supertrend60 = one vol bet | CH 10-16; MEM family_sweep |
| box-theory | 2026-10-16 | intraday search | Fade the opening range edges (founder's box)? | failed | best PF 0.87; ORB breakout PF 1.00 (600 variants) | CH 10-16 |
| trend-day-pullback | 2026-10-16 | intraday search | Trend day then pullback, 3,876 variants vs random-direction control? | failed | best real +0.35R vs best random +0.27R | CH 10-16 |
| regime-scan-0950 | 2026-10-16 | regime | 24 pre-09:50 features vs 09:50 momentum R? | weak (2 candidates) | prior-day efficiency LOW p 0.017; TOM p 0.047 | CH 10-16 |
| flow-hypotheses | 2026-10-16 | other (flows) | TOM, payroll, LETF, post-expiry week, VIX-expiry Wednesday? | failed | none beats random-date control both markets | CH 10-16 |
| fomc-skip | 2026-10-16 | regime | Cascade on FOMC decision days? | worked (skip adopted) | PF 0.54 t −3.8 on decision days | CH 10-16 |
| prop-reality | 2026-10-16 | payout/prop sim | Prop plan vs Monte Carlo and measured live costs? | correction | Flex 8→2 MNQ: median ~17 weeks to first payout | MEM flex_pipeline; research/2026-10-16_prop_plan_reality.md |
| speed-levers | 2026-10-17 | payout/prop sim | Which levers shorten time to first payout? | failed (floor) | baseline 16.4 wks; +10 pt synthetic edge only 10.4 wks | PROG speed, speed_mon; MEM flex_pipeline |
| combo-plan | 2026-10-17 | payout/prop sim | Cascade + gap retest (+weak day, Monday) as one plan? | worked (adopted) | first Flex payout 16.4 → 11.2 wks; +4,125 pts/yr at 1 MNQ | PROG combo; MEM flex_pipeline |
| combo-es | 2026-10-17 | payout/prop sim | NQ + ES on one account? | weak; 25K = NQ only | Flex 10.8 vs 11.2 wks; on 25K blown 61–65% | PROG combo_es |
| fast25 | 2026-10-17 | payout/prop sim | $400–500/day on 25K with 1–3 trades? | failed | +$16/day per MNQ; needs 25–30 MNQ = ruin | PROG fast25 |
| daily-lock | 2026-10-17 | payout/prop sim | Daily profit lock? | failed | caps big winners; paid/yr falls | PROG lock |
| both-trades | 2026-10-17 | payout/prop sim | Take both cascade and gap retest when they agree? | worked (adopted) | Flex 25K paid/yr $2,781 → $3.5–4.0k | PROG both |
| reentry | 2026-10-17 | intraday search | Re-entries in the day's direction? | weak (rule adopted) | only after a winner +5.7 vs twins −0.8 | PROG reentry; MEM daily_plan |
| regime-measure | 2026-10-17 | regime | Can 1m regime be measured and does it persist? | failed (no persistence) | regime label 60 min later ≈ base share | PROG regime, regime2, regime_synth, regcal |
| regime-optimise | 2026-10-17 | regime | 9,408 regime rules walk-forward + shuffles? | failed | only "trending by 10:30 in gap direction" = cascade | PROG regopt |
| fleet-entries-exits | 2026-10-17 | paper fleet | Are fleet entries or exits the problem? | failed (exits lose; entries ≈ day direction) | hold to 15:55 +4.7 median vs day-direction +7.2 | PROG fleetq, fleetq2 |
| fleet-tape-regime | 2026-10-17 | paper fleet | Fleet with tape regime gate + profit lock on 2026 data? | failed | no family turned positive | PROG fleetreg, fleetfam, fleetlock(2) |
| ema-hand-trade | 2026-10-17 | intraday search | Founder's EMA-14 entries, stops (125), trails (96), sweep (2,880)? | failed | ≈ breakeven OOS; all stop×target combos negative in R | PROG emasweep, emastop, emaexit, emamom, emamtf, perm, openreg |
| live-orb-rules | 2026-10-17 | intraday search | Live ORB rules over all eras? | failed | −1.2 pts/trade all eras; no filter fixes | MEM daily_plan |
| sprint | 2026-10-17 | payout/prop sim | One-week payout sprint (Lightning)? | failed / founder's choice | payout ≤7/20 days ≈ 6.7%/25%; 1 min late kills it | PROG sprint*, sprint4; DOC manual_playbook |
| pa1m | 2026-10-17 | intraday search | Follow 1m price action given the regime? | failed | no tradable effect | PROG pa1m |
| vol-size / calm | 2026-10-17 | payout/prop sim | Volatility / calm-day sizing of cascade, weak day, Monday? | failed (no size rule) | per-risk returns flat across vol | PROG volsize, calm |
| news10 | 2026-10-17 | intraday search | 10:00 ET releases (JOLTS, ISM) and the cascade? | failed | no effect | PROG news10, newscheck |
| mom-dir | 2026-10-17 | intraday search | 1-min momentum direction + 5-min entry family? | paper (6 ids); not adopted as replacement | 12,000-setting WF fails plateau check | CH 10-17; git dd44a92, c8f583d |
| behaviour-map | 2026-10-17 | other (descriptive) | How does NQ behave (structure, vol, cross-market, news)? | descriptive | basis for 10-19 behaviour studies | PROG behavior, behavior2 |
| plan-checks | 2026-10-18 | execution/live | Stacking setups, news days, quiet-market stand-asides on engine trades? | weak (news stand-asides adopted) | setups correlate +0.6–0.7; Lightning stacking parked | PROG planchk; CH 10-18 |
| path-direction | 2026-10-18 | intraday search | Gap/momentum side at 10:00 on a straight path? | paper (pd_gap_1000, pd_mom_1000; engine not restarted) | gap +28.3 pts/tr t 4.26; holdout 50 tr +919 pts | MEM project_path_direction; CH 10-18 |
| daily-book-1nq | 2026-10-18 | payout/prop sim | All confirmed setups as one 1 NQ book (20–50 pts/day)? | failed for goal | +10.6 pts/session, median day 0, DD 1,156 pts | CH 10-18 |
| high-winrate | 2026-10-18 | intraday search | High-win-rate exits (0.5R, 90% WR)? | failed | 90% WR exit −0.93/trade; ≥60% WR loses 2010-18 | CH 10-18 |
| vest-plan | 2026-10-19 | payout/prop sim | Vest Markets $5K eval/funded plan with the book? | research plan | eval 44% pass ~9 days; ~+$10–14k/yr up to 10 accounts | MEM project_vest_battle_plan; PROG awk_20261019 |
| O3-overnight-weakness-vest | 2026-10-19 | overnight/holds | Overnight weakness hold (IBS≤0.3, VIX≥20) for Vest (allows holds)? | adopted for Vest plan | 15:59 → next close, 1.5 ATR stop | git 1864c67; MEM vest_battle_plan |
| session-model | 2026-10-19 | intraday search | Does known-at-open info predict Asia/London/NY direction? | failed (direction); size forecastable | direction coin flip; straight ≥0.55 at 10:30 → trend day 57% | PROG session_model, intraday_models |
| behaviour-3 / sessions / vwap | 2026-10-19 | other (descriptive) | Pullbacks, gap fills, sweeps, round numbers, RVOL, delta, VWAP per session? | descriptive; no new edge | overnight rules test as coin flips | PROG behavior3, behavior_sessions; git 549c0b6, ecca87b |
| target-by-range | 2026-10-19 | intraday search | Targets by predicted range vs 2R everywhere? | failed (2R stays) | — | PROG target_by_range |
| vest-rules | 2026-10-19 | payout/prop sim | Vest Markets rules (hedging, overnight)? | fact check | $40 fee, $500 goal, $300 static DD; cross-account hedging banned; holds allowed | MEM project_vest_markets_rules |
| audit-2026-04-30 | 2026-04-30 | execution/live | Execution layer + re-audit of stated fixes? | historical audit | T2 findings in src/execution | DOC execution_layer_audit, AUDIT_REAUDIT_2026-04-30 |
| orphan-stop | 2026-09-23 | execution/live | Why did a hidden long get liquidated? | fixed | cancel 'failed' → guards survive FLAT, NetReconciler | MEM project_orphan_stop_incident |
| cancel-ack | 2026-09-21 | execution/live | Why did cancels never complete? | fixed | rp_code 1045; missing manual_or_auto=2 field | MEM project_cancel_ack_fix |

## 2. Superseded or corrected findings

- **P1 → P3 (2026-10-11 night):** the conviction-stack payout result (53 % of 2-week windows paid, 1.8 payouts/month,
  $1,590/month, 28 %/yr blown) is INVALID — the export carried one row per unit, so MAE was tested per unit. With the
  position MAE the 4-unit stack breaches a $2K trail ~12×/yr (1 MNQ ~2.8×/yr). Banner at top of docs/strategy_weakness_vix_book.md.
- **Tradeify flat-by-16:45 rule (10-11):** every multi-session hold (`*_h2`, `_h3`, `_xs`, `ds*_xup`, `fear18_*`, `dc20_h5`)
  became research-only; the overnight weakness book became research-only on Tradeify (later usable on Vest, 10-19).
- **payout-sim v1 → official rules (10-11/10-12):** first sim used assumed Flex/Growth rules and labelled MNQ results as NQ;
  rules re-read from firms' pages (P5); Apex trail $2K not $2.5K; Topstep payout share 1.0 → 0.45.
- **History-research corrections (10-11):** bar replays filled stops a bar's range away (−20.4 vs −11.2 pts) → bar_path fix;
  half-replayed months counted as complete (beta_day +$6,316 → +$1,761); regime look-ahead in `hist_trades_regime`;
  "per contract" = per MNQ; stale `build-hist/paper_engine` ignored hold filters (2,088 rows deleted, re-replayed).
- **N5-ib-break:** first read PF 1.91 / 17 of 17 years was look-ahead; fixed → PF 1.00.
- **M1 multi-asset:** first run's t 7–9 bond/grain "short edges" were a cost-sign bug + roll gaps.
- **S4/S5:** short-side P&L was credited instead of charged the round trip (−$9.64/trade correction).
- **R7-orderflow:** overlapping-signal average +4.9 pts → real trades break even.
- **ML3 / exit re-sims (10-12):** overlapping positions faked p<0.005 edges (trend_rs_es_15m, keltner_ride); sequential = none.
- **V6 (10-12) vs Select 50K plan:** research $ were historical-price; at today's NQ (~31,000, ATR ~320) the 36-pt stop is hit
  on ~90 % of fear days and unstopped 1 MNQ DD ~$5–7k > $2K trail — select50 config not viable as configured; gamma30 dropped.
- **Select Daily → Select Flex (10-15):** for the cascade Flex pays ~2× as often, reversing the P5/select50 choice.
- **Flex first-payout median (10-16):** 35–41 trading days → ~83 trading days (~17 weeks) with walk-forward trades and measured cost.
- **Prop coin-flip sim (10-13):** Tradeify eval has NO time limit (pass ~33–39 %, was 20–27 %); fixed 40-pt stop drifts with price → stop in ATR.
- **ofo ORB (10-14):** pre-validation PF figures fragile (NY CI [0.99, 1.62], top-5 days 66 %); Asia = artefact (32 losing trades dropped).
- **session_stats ATR14 (10-14):** ran 8 % low (stray/partial sessions counted) — recomputed; VIX regime was stale after day 1 (fixed).
- **Cascade 12:00 layer (10-17):** removed from every `so_cascade_*` id; rows before 10-17 include the 12:00 trades.
- **Tape-mode early winners (09-25):** volume_burst / delta_div looked PF 2–8 on 5 days; negative in every window in the 10-16 sweep.
- **Fleet pruning (09-26) superseded (09-29):** "all strategies run for ever".
- **Hand-off FIB_PB_1M_DEEP and live rota:** removed from live (09-29 / 10-11); "no proven edge".
- **Stop-after-first-loss / meta take-skip (10-17):** earlier gain was look-ahead; meta model fails at shuffled labels.

## 3. Things live or on paper right now (2026-10-19, from CHANGES / memory / `systemctl --user` read-only)

**Live money:** nothing. `nq-executor-local@tradeify` (live ORB, config `dry_run: false`) STOPPED and DISABLED (10-11 / 10-17);
live-rota timers, MTF-Asia, strategy-handoff and rotation timers disabled.

**Dry-run executor instances (all running, `dry_run: true`, MNQ/MES, no orders):**
| unit | strategy | size |
|---|---|---|
| tradeify_book | weakness × VIX conviction stack, 18:00 → 15:55 (research-only on Tradeify) | 1–4 MNQ |
| tradeify_day | WEAK_DAY: any weakness & VIX ≥ 20 → 09:30 → 15:55 | 1 (+1 at VIX ≥ 25) MNQ |
| tradeify_combo / tradeify_combo_es | C1 combo3 (weak close, big down, wide down) | 1–3 MNQ / MES |
| tradeify_select50 | combo3 for Select Daily 50K (2 MNQ, 36-pt stop) — known not viable (V6) | 2 MNQ |
| tradeify_cascade | every-day momentum cascade with FOMC skip | 4 MNQ |
| tradeify_gor | gap retest (`skip_fomc_day`) | 2 MNQ |

**Paper engines running:** `paper-engine-local` (NQ, 6,657 ids in config/paper_fleet.json, label tradeify), `-es`, `-gc`, `-cl`,
`-crypto-btc`, `-crypto-eth`. Notable paper ids: `so_k25_2r`, `so_k25_2r_late`, `so_late_k30_2r`, `wkday_ibs30_vix20`,
`wkday_ibs50_vix20`, `so_cascade_2r(_nofomc)`, `so_cascade_3r_s30(_nofomc)`, `gap_or_retest(__nofomc)`, `ofo_ny5`, `ofo_asia15`,
`ofo_*_vfall`, `ofo_ny5_wide`, `mom_dir_*` (6), `anyweak_day__vix20`, `rsi2_20_day__vix20`, `wc_ibs30_day__vix20`, hold variants
(`wc_ibs*`, `ds*_xup`, `fear18_*`, `rsi2_20_*`, `dc20_h5`, `anyweak_h1*`, `fomc_drift_h1`), `beta_on*`, `__rg_*`, `__tr25`,
`__lv_*`, `s2_*` (slot 2), `sig_*` (signals). `pd_gap_1000` / `pd_mom_1000` are in the config and backfilled but the forward
engine was NOT rebuilt/restarted for them (founder's call). Replay-only research: night grid (`night-eval.timer`, label night),
slot2 eval (`slot2-eval.timer`), tcf grid (`tcf-eval.timer`), history queue (labels nq_hist / nq_bars / crypto_hist; 120 done /
581 pending at 10-13).

**Evening checks:** manual-review (16:15 ET) and working-fleet-report (16:30 ET) timers; daily-plan.timer drives data/plan
(14 plan files present). data/manual_review/ and data/crypto/ are empty (no crypto report written yet).

**StarNet desk:** resumed 2026-10-18 on edge_lab work (separate catalogue).

## 4. Counts by area (rows in section 1; grouped rows count once)

| area | rows |
|---|---|
| intraday search | 43 |
| payout/prop sim | 31 |
| paper fleet | 26 |
| regime | 17 |
| other (flows, cross-asset, validation, descriptive, Wyckoff-style) | 16 |
| execution/live | 15 |
| overnight/holds | 10 |
| order flow | 7 |
| walk-forward | 4 |
| crypto | 3 |
| **total** | **172** table rows (the 108 ledger rows are folded in; several ledger families share one row) |

Verdict tally (approx.): worked / adopted ~22, weak ~33, failed ~95, paper/dry ~20, invalid/superseded/corrected ~20.
The durable survivors across all of it: buy weakness at the daily horizon (VIX-gated; day-only form S1/V3, overnight D1/V1,
combo C1, month-end L3), the opening-momentum / strong-open / cascade family (recent-era, cost-sensitive), and the gap/path
direction rule (10-18, paper). Everything pattern/indicator/ML/order-flow on the fleet failed out of sample.
