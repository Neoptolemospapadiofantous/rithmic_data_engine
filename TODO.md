## Best strategy so far (2026-10-11): the weakness × VIX book — `docs/strategy_weakness_vix_book.md`
- [x] **INTRADAY SEARCH CLOSED 2026-10-11 night (founder: "has to be intra day")** — `docs/intraday_search_2026-10-11.md`,
      ledger rows S1–S10. The day-session weakness long (09:30→15:55, VIX≥20) is the legal form of the edge: $53/MNQ, 33/yr,
      PF 1.60, 14/16 yrs — a sleeve (~1 payout/quarter). Crypto sessions and MBO order-flow ML are measured and empty.
      Do NOT re-run intraday price-action, crypto-session or order-flow searches on this data.
- [x] **EXIT RE-SIM (S16):** every base's real entries × 54 exit combos on rebuilt minute paths — the engine's tight stops /
      targets / BE triggers lose (median test PF 0.53 → 0.92 with hold-to-close or a wide stop), yet even the hindsight-best
      exit leaves 0 of 419 bases PF>1.3 in both halves. ⚠️ Fleet-wide: the default exit set (trail_be_trigger, tight trails,
      fixed targets) is value-destroying on NQ intraday entries — a one-line change to the fleet defaults is worth a replay.
- [x] **FLEET MATRIX (S15):** every overlay × tf × family × regime scored OOS — level overlays and VIX≥20 are the only
      additive pieces and together reach PF 0.98 (breakeven). Book overlays hurt everywhere. The 12k combo grid stays queued at
      lowest priority; do not promote it on the strength of in-sample PF.
- [x] **VALIDATED 2026-10-11 night (S14):** S1 recomputed from SQL and replayed in the engine — matches. Found + fixed: stale
      build-hist binary (rebuild build-hist after ANY trend_strategy/paper change before trusting a history month), per-strategy
      consistency cap freezing paper strategies (now off for paper). The `tradeify_day` dry-run keeps the live 30 % cap: after a
      day that out-earns prior profit it halts until `clear_halt` — expected under Tradeify rules, but read the log before
      calling a missing entry a bug.
- [x] **P1 CORRECTED (P3):** the stack export was one row per unit → payout_sim tested per-unit MAE. Correct accounting: the overnight
      stack blows ~12×/yr on Select Daily 50K; 1 MNQ flat 2.8×/yr. Venue sim (P2): FTMO 100k Swing at minimum size ≈ $700/month,
      payout every ~7 weeks; Phidias worse. ⚠️ `scripts/payout_sim.sh` / the stack export still emit per-unit rows — a stacked series
      must be exported as ONE trade with summed size before any future prop-rule run.
- [x] **Conviction sizing in the executor — BUILT 2026-10-11 evening (CHANGES.md).** `stack_*` TrendConfig keys → `TrendStrategy::last_units()`
      → `OrderManager::set_entry_units()` capped by `OrbConfig::qty_max`; fill-size warnings follow the position's qty. The book dry-run
      (`tradeify_book`, tag `WEAK_STACK`) runs it at `qty 1 / qty_max 4`. Still ONE instance per account (a second instance would read the
      other's fills as unowned). Open: watch the first stacked dry fills for the `[OM] entry size N unit(s)` line; Tradeify 18:00 answer;
      new 50K Select account; ≥20 forward sessions before any `dry_run: false`.
- [x] 2026-10-11 evening: ORB stopped, MTF-Asia + rota timers disabled, the book runs dry as `nq-executor-local@tradeify_book`.
Any-weakness close + VIX fear gate → buy 18:00, sell 15:55, conviction stack 1–4 MNQ on Select Daily 50K: sim eval pass 92 %,
1.8 payouts/month, 53 % of 2-week windows, $1,590/month, blown 28 %/yr. Forward test running (ids in the doc). Go-live
checklist in the doc; the Tradeify 18:00-entry question is parked by the founder. Ledger rows: research_tests D*/V*/I*/P*.

## Research pipeline 2026-10-11 (founder: "make sure all strategies are validated and correct with all the data we have … add as many strategies as we can … which ones are the best and when")
Status legend: [ ] todo · [~] in progress · [x] done. Order matters: speed → correctness → scoring → more strategies.
No change to `src/execution/orb_strategy.hpp`, `config/live_config.json` or the live executor. Every engine change is
built in `build-hist/`, must keep the golden replay byte-identical, and is swapped in outside 09:25–16:05 ET.

### Already running
- [~] History queue (`scripts/history_queue.sh run --parallel 2 --parallel-closed 6`): `nq` main fleet × every MBO month
      (label nq_hist), `nqx` night grid + staged winners/new modes (1,745 ids, nq_hist), `nqbars` NQ 2010-07…2023-02
      from 1m bars (`--dbn-bars`, label nq_bars), crypto btc/eth 2020-01… (crypto_hist). 6 slots Fri 17:00 → Sun 18:00 ET.
- [x] Bar feed `src/history/bar_feed.hpp` (4 synthetic prints/bar) + `--dbn-bars` + test_dbn (101 checks).
- [x] Queue slot race fixed (a just-launched job counts before its engine spawns).

### R. What has been tested (ledger, 2026-10-11) — NQ, net of costs, finished months only, regime known at entry
- [x] R1. Fleet entry families (41 types, 464 bases): orb (61 + 144 overnight), mtf_scalper (32 + 132 overnight), trend
      engine modes — fib_pullback, ema_pullback, rsi2_pullback, donchian, atr_break, pdhl_breakout, ib_break, range_break,
      squeeze, nr7, vwap_fade, band_fade, gap_fade, thrust_fade, failed_breakout, level_fade, supertrend, keltner_ride,
      ema_ribbon, ichimoku, vwap_trend, trend_confirm, trend_day, roc_momentum, rs_continuation, tod_momentum,
      opening_drive, news_break, gap_go, orb_retest, vwap_reclaim, book_imbalance, delta_divergence, delta_trend,
      absorption_reversal, volume_burst, vprofile, hold (beta_*). Night grid 948, winners 673, new modes 124.
      Result on 2024–26 ticks: 25 of ~4,400 (≥ 100 trades) net positive; only beta_* hold-long positive every year.
- [x] R2. Variant families (~6,100): regime presets rg_trend/range/fade/expand/nochase; rvol15/25/q, vt1000/2000; book
      gates inv/imb/all/micro/sg/sz/wait; book exits bx/bbe/btp; exits tr25, tp1/15/2/3, rx grids; windows open/am/pm;
      htf; wide. Per trade none fixes a losing entry (regime presets look better only by trading less); rx/tp ≈ +$0.5.
- [~] R3. Level variants lv_room/conf/tgt/stop (1,728): forward fleet live; history waits for nqx.
- [x] R4. Regime filters at entry: gap small/mid/big, previous day trend_up/down/range/short, trend_up50/dn50.
      Survivors are beta_* only (tcf5_ldn · gap_big was look-ahead).
- [x] R5. Walk-forward per family × session (asia, london, ny_open, ny_mid, ny_pm; train 3/6/12 months): 8 of 80 groups
      positive, mostly beta; in-sample ~$650/month → out-of-sample ~$0–200.
- [x] R6. Payout simulator: 64 daily rules, portfolios ≤ 4, Select Flex / Growth 25K/50K, 1–5 MNQ and 1 NQ. Nothing
      passes at 1 NQ; best = beta_on_1800 · trend_up50 at 2–3 MNQ (a 2024–26 bull-market effect, see R7).
- [x] R7. Direct 2010–2026 tests on 1m bars / session_stats (seconds each):
      overnight long ± trend filter — fails (2022 −$2.3k/MNQ); intraday momentum — fails; order-count / depth / book
      imbalance + delta (2023–26) — break-even as real trades (PF 1.00); cost sensitivity — 4 % positive at 0 slippage;
      **buy a weak close (IBS < 0.2) → next close: PF 1.41, 670 trades, 12/17 years (the one edge)**, overnight-only PF
      1.28, daytime-only 1.20, any stop kills it; gap fade ×4 — fails; first-hour fade/follow ×3 — fails/weak; turn of
      month — weak; day of week — Monday t = 1.35 (not significant), Thursday long loses (t = −2.1).
- [ ] R8. Not tested yet: 2010–2023 bars for the fleet (bar path fixed 2026-10-11, check month re-running), level
      variants on history, ES ticks + bars, YM/RTY/GC/CL mirrors (~8,000 staged), combos (12,031 staged), crypto beyond
      2026-09, passive (limit-order) execution.

### N. Next ideas (to rank — intraday first, founder 2026-10-11)
- [x] N1 bound (2026-10-11): free execution (0 slippage) leaves 180 of ~4,400 positive, 18 in both 2024 and 2025 — near
      chance; passive fills add adverse selection, so a queue simulator is not built until an entry shows gross edge.
      Passive execution: simulate limit-order entries with queue position on MBO 2023–26; re-test the 180 strategies
      positive at zero slippage.
- [x] N2 (2026-10-11, NQ+ES 2010–26, release days found by 08:30 / 14:00 volume spikes): follow/fade the release
      move, release-day direction — PF 0.62–1.20, no t-stat ≥ 1.2. Event days: CPI / FOMC / NFP (calendar table) — post-release drift vs fade, by minutes after the release.
- [x] N3 (2026-10-11): ATR low/mid/high buckets on every N-test — no significant bucket once look-ahead removed.
      Volatility regimes: ATR / realised-vol percentile buckets per strategy and per direct test.
- [x] N4 (2026-10-11): laggard catch-up / leader keeps leading at 10:30 — PF 0.62–1.22, t ≤ −0.35. ES vs NQ intraday relative strength / spread reversion (both owned 2010–2026).
- [x] N5 (2026-10-11): back-inside-prior-range, open-outside-and-holds — PF 0.75–1.14; IB breakout looked PF 1.91
      until a look-ahead in the query was removed → NQ PF 1.00, ES 0.79 (minute-by-minute, stop on the far IB side).
      Market-profile rules: open outside prior value area (80 % rule), initial-balance extension, overnight
      inventory vs RTH open.
- [ ] N6. Opening-auction signals: first-5-min volume / delta extremes → day type → strategy choice.
- [x] N7 (2026-10-11): fixed target 2–20 pts with the target ALWAYS filling first (upper bound) turns at most 12 of
      4,264 strategies positive; exits are not the fix. Exit research on families with large MFE: time stops, partial at +1R, MFE-capture ratio per family.
- [x] N8 (2026-10-11): OPEX, quad witching, month-end 14:00→close, pre-holiday, last 10 min follow/fade — PF 0.35–0.93.
      Calendar flows: OPEX / quad witching, month-end 14:00–16:00, pre-holiday, roll week, MOC last 10 minutes.
- [x] N9 (2026-10-11): follow open→10:00 ≥ 0.3 ATR to the close — NQ PF 1.11 (t −0.2), ES 0.82. Higher-timeframe intraday swings (15/30-min bars, ATR-scaled targets) so costs are a smaller share.
- [x] N10 (2026-10-11): every N-test also run on ES — nothing better than NQ. MES / ES as the prop instrument (lower volatility per contract, same drawdown).
- [x] N11 (2026-10-11, founder: multi-day holds are allowed; "try all and add to fleet"): sweep of ~70 daily variants
      on NQ + ES 2010–2026 — 21 survive (t ≥ 2.5, PF ≥ 1.2, both halves > 0, ES confirms), ALL long mean reversion after
      weakness + donchian20 hold5: weak close IBS<0.1/0.2/0.3 × hold 1–3 / exit-on-strength, down streaks ≥2/3 → first
      up close, fear day hold 2–3, RSI(2)<20 hold 1. Shorts, FOMC/NFP drift+fade, NQ/ES spread, SMA cross: nothing. 36
      fleet ids (`wc_*`, `ds*_xup`, `fear18_*`, `rsi2_20_*`, `dc20_h5` + `es_`), engine support in CHANGES 2026-10-11.
      Next: payout-sim the portfolio (N13) under Flex vs Growth at 1–3 MNQ; the 2022 drawdown is the sizing constraint.
- [x] Intraday closed on evidence (2026-10-11): 69-indicator walk-forward ML on 15m bars IC 0.02–0.04 (3–4× below cost),
      passive fills recover nothing, day-type at 09:45 is predictable (range IC 0.51, ORB follow IC 0.17 every year) but no
      breakout/trend family converts it (ATR-ORB loses on predicted big days; mid-range days a +1–8 pt tilt only).
- [~] Phase 1 context data (2026-10-11): VIX family + ETF closes + FOMC history pulled; VIX regime is the conditioner that
      scales the weakness edge 3–8× (CHANGES). Still untested: CPI dates, earnings nights, VX futures term structure beyond
      VIX/VIX3M; SPY-drawdown and HYG/TLT gates (same regime, could replace VIX live if CBOE's file breaks).
- [ ] N12. Meta-filter (logistic regression in C++) over context features, walk-forward only, on the top candidates.
- [~] N13 (2026-10-11): payout_sim now models Tradeify's official Select eval + Flex + Daily rules (CHANGES). Portfolios
      scored: lv_conf family (safe, slow), legal one-session holds (faster, 10–40 % blown). Open: replay `nqx 2024-05…2025-12`
      (priority.txt) and re-score lv_conf on its full history; confirm the 18:00-entry hold with Tradeify support; forward
      ≥ 20 sessions before any funded account. Steady state on Select Daily: ~half of all 2-week windows pay (win10
      47–51 %), 1–1.5 payouts/month; staggered accounts do not help; passive entries kill lv_conf (PF 0.35). Still open:
      night + ES lv_conf results (night_eval / label es), the 2024 nqx months, contract-aware portfolios ≥ 6 at 5 MNQ.

### A. Speed (target 5–10× per replay)
- [x] A1. Profiled (gprof; perf needs `sudo sysctl kernel.perf_event_paranoid=1`): CPU-bound, 96 % per-tick on MBO;
      ~half the CPU in libc — per-strategy `gmtime_r` (broker + trend, ~185 M/bar-week) and per-tick mode/session
      string compares. Next: one per-minute ET cache shared by all, enum modes (A2), flat fast path (A3).
- [ ] A2. Shared indicators per base strategy (overlay variants reuse the base's indicator state).
- [ ] A3. Flat-strategy fast path: no per-tick work for a strategy with no position / pending order (bar close only).
- [ ] A4. One process per month for all NQ sets (main + nqx) — decode each MBO file once.
- [ ] A5. History mode writes trades only (no paper_signals), bulk COPY.
- [ ] A6. 12 slots overnight + weekends (memory/DB checked), 2 in RTH-adjacent hours as today.
- [ ] A7. Golden replay byte-identical after each of A2–A5; record speed-up per step in CHANGES.md.

### L. Levels (founder 2026-10-11: "implement levels into our strategies", all families, all uses)
- [x] L1. LevelState (paper_levels.hpp) + broker lv_room / lv_conf / lv_tgt / lv_stop, calibrated; 1,728 variants live
      in the forward fleet; history via nqx. Golden re-frozen, hermes 20/20.
- [ ] L2. MTF scalper bases (bracket broker has no level hook yet).
- [ ] L3. Crypto mirrors of the lv variants (crypto_fleet.sh build — +35 % crypto fleet + history time; founder's call).
- [ ] L4. Per-family split once results exist (which level family carries the edge) + a live implementation only
      for a proven winner (founder's call).
- [ ] L5. A restart loses an open position's level target (not persisted) — persist it if lv_tgt wins.

### B. Correctness — prove the numbers
- [~] B1. Bars vs ticks (scripts/bars_vs_ticks.sh, table history_bar_reliability; 4-print feed failed — bar path now walked tick by tick, 2026-01 re-running): replay 2023-03…2026-03 with `--dbn-bars` too; per-strategy deviation → only strategies within
      tolerance count their 2010–2023 `nq_bars` results.
- [ ] B2. Per-trade audit SQL after every month: fill inside the minute's high/low, P&L = (exit − entry) × pv − costs,
      no overlapping positions per strategy, entry inside the strategy window, daily trade cap, no trade across a data gap.
      A failed month is flagged and excluded from rankings.
- [~] B3. Look-ahead check (hist_trades_regime fixed gap/prev/trend at entry 2026-10-11; regime gate + ES bars still to audit): regime gates / ATR / ES reference bars use only data complete at entry time.
- [ ] B4. Contract rolls: flag trades across an NQ.c.0 roll; check stop behaviour at the roll jump.
- [ ] B5. Source parity: same days from Postgres vs files (crypto 2026-09 has both) → identical trades.
- [ ] B6. Partial MBO file `2026-03-partial-update`: verify book quotes after its mid-session start.

### C. Scoring — best, not luckiest
- [~] C1. Walk-forward (scripts/walk_forward.sh per family × session built; extend to 2010–2026 once bars pass): select 2010–2022, validate 2023–24, one final test 2025–26; rolling windows.
- [ ] C2. Parameter plateaus: a strategy counts only if its parameter neighbours are positive too.
- [~] C3. Cost sensitivity (0 slippage → 180 of ~4,400 positive): rank at 1 / 3 / 5 ticks slippage (live ORB measured 4.6).
- [ ] C4. Multiple-testing correction (deflated Sharpe / reality check) for ~15k strategies.
- [x] C5. Prop overlay (scripts/payout_sim.sh — daily rules, train/test, portfolios): worst day, trailing drawdown, trades/day under Tradeify rules.
- [ ] C6. "When": tag every history trade (15-min slot, weekday, day_type, ATR regime, gap size, FOMC/NFP/roll week) →
      best strategy per slot × regime, out-of-sample → feed `live_rota.sh` picks.
- [ ] C7. Summary table `strategy_period_stats` (strategy × month × slot × regime), refreshed per finished month;
      dashboard page reads it (today's 2M-row ad-hoc queries take minutes).

### D. More strategies (after A–C)
- [ ] D1. Combo grid (12,031) over all NQ data, filtered to combos whose parts hold up.
- [ ] D2. Dense grids around the best family plateaus (e.g. 15m supertrend / keltner / roc `__inv`, gap_go).
- [ ] D3. Trail sweep 25/35/50 on the top bases (carried from item 10 below).
- [ ] D4. Crypto: 24-hour variants (today crypto mirrors only trade 07–16 ET, NQ's windows); fix the families not
      rescaled for crypto (absorption_rev_*, news_break_*, fixed-mode MTF, gap_* without own minimum).
- [ ] D5. Bitstamp MBO replay reader once ≥2 weeks are recorded.
- [ ] D6. ES/other markets on 1m bars (ES 2010–2026 bars owned) for the ES mirror fleet.

### E. Platform plan (design doc "Trading platform: design and plan") + open founder decisions
- [ ] E1. Phase 0: websocket auth, DB connection pool, freezing report off the request path, disk alert, execution-audit alert.
- [ ] E2. Dashboard backend memory leak (`dashboard-api-local` hit its 3 GB cap 2026-10-10; restarted).
- [ ] E3. Founder: crypto history scope (2024+ / reduced set / everything — default newest-first).
- [ ] E4. Founder: cold-data location + second drive (85 GB free; ~30M history trades ≈ 6–8 GB).
- [ ] E5. Founder: 60-day hot retention, pages to remove/merge, retire 15 stale labels, trim the crypto forward fleet.
- [ ] E6. NQ fan-out (:8198) live check at the Sunday 18:00 ET open.
- [x] E7. Commit the history/crypto work (both repos) — engine 16da892, bot ca9ed35 (2026-10-11).

### F. Engine ↔ dashboard boundary (founder 2026-10-11: keep two repos, fix the weak spots)
- [ ] F1. Contract file (`docs/CONTRACT.md` here, mirrored in bot): every table/column, file path, config key, log
      line, port (8197/8198 JSON lines) and systemd unit the dashboard reads or drives. Plus a check in BOTH gates
      (`make hermes` here, bot pre-commit) that fails when a listed item is missing or renamed.
- [ ] F2. Remove the old Python trading bot from `bot` (`python/`, `cpp/`, Optuna/walk-forward pipeline, stale
      tests, its CLAUDE.md sections) — first prove nothing `ui.app` (:8080) imports it.
- [ ] F3. Rename `bot` → e.g. `trading-dashboard` (repo, folder, unit paths) when convenient.

## Build queue 2026-09-28 (founder: "start getting data from more instruments" + "do all you said")
Status legend: [ ] todo · [~] in progress · [x] done. Each item: tests + gate green, backfill replay where a strategy is involved.
- [x] 16. Volume / volatility filters: rvol gate + vol-targeted size (generic, both sides), atr_break + vprofile modes, 90-variant matrix in the fleet (label volgrid). Verdicts on six sessions: rvol ≥1.5 helps per contract, quiet-tape gate hurts, sizing is size only, new modes flat/negative.
- [x] 15. `beta_on_1800` (+__htf, __wide, es_) — the Tradeify-legal overnight hold; found and fixed the 18:00 rollover-vs-gap-reset ordering bug in paper_main (every 18:00-reopen strategy was mis-counting its day).
- [x] 14. Fleet audit triage (founder pasted 4 FAILs): resume catch-up bug fixed in both paper brokers (+test); audit checks re-scoped (exit-side overlays exempt from the subset check, coverage/KPIs on the fleet label, new exit-after-entry check); insights KPIs fleet-label only. The 14 impossible rows now sit under label `invalid_resume_20260923` (reversible); audit 0 fail / 1 warn / 57 ok.
- [x] 13. Beta exposure (founder: "for now add the rest in leaderboard rotation"): trend mode `hold` (clock-time entry, window-end exit, survives the 18:00 reset; day filters hold_tom / hold_pre_event / hold_up_day; paper-broker windows wrap midnight). Eight NQ variants (beta_on, __htf, _tom, _prefomc, _short, beta_close, beta_day, __htf) + three ES. Paper only: Tradeify forbids overnight holds and the live EOD logic does not wrap. Six-session replay is noise (overnight long −$433, short +$224, day long +$649). Known: a Friday entry exits at the first tick after the weekend gap.
- [x] 12. Founder 2026-09-29: ALL 2,170 registered strategies run for ever (09-26 retirements re-enabled, research grids added to the fleet; 4.5 % CPU); Leaderboard gains this month / this year / 90d / 365d; new `Week · month · year` tab (`/api/cpp/strategies/periodic`: per-strategy net per calendar period, +periods count, search/sort/paging).
- [x] 11. Strategies page: registered ids that are not in `config/paper_fleet.json` show as status `research` (hidden unless the status filter says research) and leave the fleet count — 664/1704 fleet + 466 research, not 664/2170 (2026-09-29).
- [ ] 10. Follow-ups from the queue: trail-width sweep (25/35/50) on the top 24 bases; decide retention before the 8-instrument tick volume fills the disk (~2 months); judge the 29 forward-test keepers (`__rg_*`, `__tp*`, `__tr25`, gap_fade, news_break, orb_retest, `es_*`) after ≥2 weeks on the Leaderboard.
- [x] 1. Collector records 8 instruments since 2026-09-28 22:47 EEST: NQ (depth) + ES, RTY, MNQ, MES, YM:CBOT, CL:NYMEX, GC:COMEX (`RITHMIC_EXTRA_SYMBOLS`, `SYMBOL:EXCHANGE` form new in config.hpp); contracts rows for MES/RTY/YM (quarterly); CL/GC roll monthly — not modelled. Disk 93 % (66 GB free): budget ~2.5x today's ~110 MB/day of ticks — revisit retention within ~2 months.
- [x] 2. Fixed take-profit (`tp_points` / `tp_r`): built both sides, parity-tested (same tick, same reason). Grid verdict: a fixed target LOSES on average at every R (it caps the trailing winners); 3R ≈ neutral on classic ORB only. Five ORB `__tp*` keepers forward-test; do not put a target on fib/trend bases.
- [x] 3. `orb_retest` mode built + tested; grid: flat-to-negative on six sessions (retests mostly scratch at BE). One variant forward-tests.
- [x] 4. `gap_fade` mode built + tested (strategy-owned `gap_filled` exit); grid: 2 gap days, 2 winners (+$124) — too few to judge; forward-tests.
- [x] 5. `news_break` mode built + tested, `set_event_day` fed from `calendar` (fomc/nfp) in paper_main + executor; grid: 14:00 5/5 small winners, 08:30 flat (needs `session_open_hour` 8 in params); event-only variant forward-tests (first NFP 2026-10-02).
- [x] 6. `ib_break` mode built + tested (strategy-owned `ib_target` exit); grid: negative (PF 0.35) — not kept.
- [x] 7. ES mirror LIVE 2026-09-28 23:06 EEST: `config/paper_fleet_es.json` (24 top NQ bases as MES $5/pt, point knobs × 0.144 = ES/NQ ATR ratio, ids `es_*`, label `es`), unit `paper-engine-local-es.service` (deploy/ + installed). Leaderboard data: label `es`; NOT on the Fleet tab (one fleet config).
- [x] 8. Exit-side regime built both sides (`regime_exit_min_eff`, `trail_step_trend/_range`, `tp_r_range`), parity-tested. Grid 8/8 better with trend trail 25 — but the plain-trail-25 control beat it on 7/8: the win is the WIDER TRAIL. Eight `__tr25` keepers forward-test. Follow-up: trail sweep 25/35/50; propose trail 25 for the live hand-off (founder's call).
- [x] 9. Combined book-fade + regime variants: backfilled (label combo) — 1–2 trades each, all losers; the gates block each other. Not kept (rows disabled).

# TODO — database additions (filed 2026-09-26; founder: "do all the rest" the same evening)

Ranked by what it protects or unlocks. None touch strategy logic or `config/live_config.json`.
Context: CHANGES.md 2026-09-26 entries, `scripts/sql/`, the "Rithmic Database" page.

## 1. Protect the data
- [x] **Nightly `pg_dump`** — `pg-backup.timer` 02:40 → `scripts/pg_backup.sh --verify` (custom format,
      ≈65 MB, keep 14, `data/backups/pg/`), Sunday off-box copy to Oracle `~/backups/rithmic` (best-effort;
      Oracle was unreachable on 09-26 — the first copy will report via grid-notify).
- [ ] **`pg_stat_statements`** — needs sudo (`shared_preload_libraries`) + a Postgres restart; then a
      "top 10 queries" panel on the ops page.

## 2. Facts the executor knows but did not store
- [x] **`live_order_events`** — every order message sent/received (new_order_sent, cancel_sent,
      gateway_ack/reject, rithmic_notify tid=351, exchange_notify tid=352), `OrbDB::write_order_event`.
      First rows arrive Monday 09-28.
- [ ] **`live_signals`** — the live engines' taken/declined signals (same shape as `paper_signals`).
- [ ] **`broker_snapshots`** — the PNL-plant position/P&L snapshot per minute.

## 3. Market context
- [x] **`session_stats`** — one row per NY session (`refresh_session_stats()`, every minute via
      `refresh_bars.sh`; day_type is a label, ATR-14/vol-20d provisional until 14/20 sessions exist).
- [x] **`book_1m`** — quotes per minute (`refresh_book_1m(symbol)`, NQ + ES).
- [x] **`bars_5m` / `bars_15m` / `bars_1h`** — views over `bars_1m`, clock-aligned.
- [x] **`contracts`** — third-Friday expiries, roll = Thursday 8 days before; `front_month(symbol)`.
      **Roll to Z6 on 2026-12-10.** Rule-generated: verify each quarter against the CME product calendar.
- [x] **`calendar`** — CME holidays/early closes Q4-2026..Q3-2027 (marked "verify"), FOMC 2026 (Oct 28,
      Dec 9), NFP first-Friday rule; `upcoming_events(days)`. CPI/PPI deliberately not guessed — add from
      the BLS schedule. Nothing READS these yet: wire the executor's news blackout and a "next roll"
      warning on the dashboard.

## 4. Strategy bookkeeping
- [ ] **`rotation_runs`** + **`strategy_versions`** (today `data/rotation_last.json` + `config/archived/handoff_history/`).
- [ ] **`strategy_daily`** for live tags.
- [ ] Point the hand-off config's `paper_source` at the same-window twin once it has a week of data:
      hand-off is `fib_pb_1m_deep` (all-day twin) since 09-26 → `fib_pb_1m_deep__am`.

## 5. Trim
- [x] `audit_log`: `ticks.written` retired in the collector and the 1.8 M rows deleted (252 MB → 160 kB).
- [x] `idx_ticks_ts`: KEEP — the collector's per-minute `MIN/MAX(ts_event)` summary needs a symbol-less
      time index; dropping it would make that a full scan every 60 s.
- [x] **ES**: KEEP collecting — `mtf_smt_v5` uses ES as its reference symbol and `rs_continuation` reads it;
      storage is the only cost and retention is "keep everything".

## Open defect — golden replay still drifts by ~10–24 rows (2026-09-26 23:00)
- Fixed tonight: ORB cooldown on the engine clock (was steady_clock), and a TOTAL tick order
  (`ORDER BY ts_event, seq, price, size` — `seq` is only 0..4, so 20 % of ticks tied and their order
  fell to the heap; drift appeared right after a VACUUM) plus a page-boundary tie guard, in both
  `PaperDb::poll_ticks` and the executor's pg feed. `--twice` pairs went from 90 → 0 differing rows.
- Still open: separate runs minutes apart differ by 10–24 rows, ALWAYS ORB re-entries
  (`orb_t1000_2nd_*`, `orb_15_*`, `orb_30_*`): the same cross fires in one run and is cooldown-blocked
  in the other, with identical logged history before it. Verified NOT the inputs (tick + bbo streams
  md5-identical across runs), not randomness, not threads, not pointer-keyed containers, not the risk
  manager. Evidence: scratchpad `glog1/2.log` first divergence = `[ORB] LONG breakout signal
  price=30930.75 orb_high=30927.50` for `orb_15_buf3_nochase5` present in one run only.
  Next step: log the tick timestamp in `[ORB]` signal/close lines and `cooldown_until_us_` at
  notify time, run twice, diff.
- 2026-09-28 evidence: two binaries (before/after the regime gate) replayed the golden window
  byte-identically, twice each (504 trades) — the drift is NOT code-path timing inside one run.
  The file frozen at 20:11 had 408; `bbo` autovacuumed at 21:03 in between (ticks autoanalyze
  20:34). First divergence = the FILL price of `orb_t1030_2nd_5`'s 14:35:28.500174 entry
  (30953.75 frozen vs 30954.00 fresh): that microsecond holds ticks (seq 0, 30954), (seq 0,
  30954.25), (seq 1, 30954), (seq 1, 30954.25)… — `ORDER BY ts_event, seq, price, size` is total
  over them, so the order should not change; suspect the page-boundary guard / a plan change
  after (auto)vacuum instead. Reproduce: freeze, `VACUUM ANALYZE ticks, bbo`, replay, diff. Until then `make hermes` can report a golden FAIL of that size —
  read the diff: only ORB re-entry rows shifting by minutes = this defect, anything else = real.

## Decided 2026-09-26 (not to do, for now)
- Retention: keep everything. Partitioning / TimescaleDB: leave as is.
