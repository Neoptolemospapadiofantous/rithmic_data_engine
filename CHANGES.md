# Changelog

All notable changes to rithmic_engine are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Dates are in ISO-8601 order (newest first).

---

## [Unreleased]

### Fixed — stale orders after a trade (2026-09-24 re-audit, 23 trade-end scenarios)
- **Breakeven before the stop's server id mapped left NO exchange stop**: the client-id cancel
  failed, `on_cancel_failed` re-adopted the old stop and cancelled the replacement, then the
  LATE-MAP cancel by server id killed the old stop too. A failure on the client-id attempt is
  now ignored once a server-id cancel for that stop is in flight.
- **A failed cancel from the PREVIOUS trade's stop was applied to the current trade** (swapped
  out its real stop — wrong direction after a reversal). Cancelled stops carry the trade they
  belonged to; only the same trade may re-adopt.
- **Entry/exit cancels went by client id** (EOD cancel of a pending entry, pending-entry timeout,
  stuck-exit retry) — Rithmic cannot route those, the orders kept resting. They now use the
  server id, or are re-sent by server id when it maps; a retried stuck exit is guarded like a
  cancelled stop.
- Daily-trade-limit no longer exits the process (outside cycle_mode): exiting in the same tick
  as the close skipped the post-close recancel window.
- Ghost-fill halt clears on any tid=451 update showing exchange flat + consistent (it could
  block entries for the rest of the session).
- New alarm: a cancelled stop unconfirmed for 60 s → CRITICAL log + grid-notify (2026-09-21:
  nine refused cancels left stops working 41–52 min).
- `scripts/strategy_handoff.sh`: ORB → trend handoff during RTH (one executor at a time),
  waits for flat + no pending stop cancel, hands back to ORB after the session.

### Fixed — remaining audit items (2026-09-24, afternoon)
- **Shutdown drain**: SIGTERM sets `g_draining` before `g_running=false`; the ORDER/PNL plant
  readers, heartbeat and the 1 s housekeeping loop keep running (tick feed stopped, strategy
  halted) so exit fills, cancel ACKs, "Cancellation Failed", late server ids and reconciler
  unwinds are still processed. Phase-2 runs ≥ 8 s and up to 25 s while a cancel is
  unconfirmed or the exchange disagrees with us.
- **Reconnect mid-trade**: when the startup snapshot unwinds a position this process does not
  own, the previous cycle's working orders are CANCELLED by server id (were kept as
  "protective stops" protecting nothing). The live stop's server id is persisted to
  `pending_stop_cancels` as soon as it maps.
- **Unwinds are retried**: NetReconciler re-acts every 15 s while a mismatch survives an
  action, cancelling the previous unwind (by server id) first; the executor re-checks the last
  exchange net every second (tid=451 updates only arrive on account changes).
- **tid=352 fills use the cumulative `total_fill_size`** (per-event sizes made the second equal
  partial look like a duplicate).
- **Partial entry/exit is consistent while pending**: any net between 0 and the full position on
  the position's side (was: only 0 or full → a partial entry was unwound after 5 s).
- **A replaced stop that fires before its cancel lands is the exit** (was routed as unowned:
  entries halted while in the trade, then a second exit flipped the net).
  Tests: test_trade_end_invariant 26/26 (#24–26 fail on 5eba64a).

### Added
- **Live executor runs any trend-engine variant (`"engine": "trend"`).** `run_executor` /
  `flush_position` are templated on the strategy type; `main` builds `OrbStrategy` or
  `TrendStrategy` (params read by `TrendConfig::from_json_string` from the same file) and runs
  the unchanged session loop — same OrderManager, orphan guards, NetReconciler, risk and broker
  day-P&L halt. ORB-only paths (chase filter, `seed_orb_range`, ORB minute log) are compiled out
  for trend. A trend signal that leaves the order manager FLAT (entry rejected, exit while
  already flat) releases the engine after `emit()` returns, so it cannot sit "in position" all
  day. Trend adds an executor backstop flatten at `eod_flatten_hour:min`. `rs_continuation` and
  `book_imbalance` are refused (the executor feeds no ES bars / BBO). Config validation: engine
  must be orb|trend and a trend engine needs its own `strategy` tag. First config:
  `config/tradeify_trend_config.json` (trend_supertrend_7_3_1m). Tests in test_trend_strategy.
- `scripts/stack_status.sh` / `make stack-status`: one index of every trading process — units
  with pid/RSS/OOM score, STRAY executors/collectors not owned by their unit, feed age, last
  broker line, memory and Chrome inside/outside the browser cap. Exit 1 = not ready.
- `scripts/overnight_live_test.sh`: unattended drill → live ORB (18:05 ET, 2 MNQ) → live trend
  (19:00–20:00 ET, 2 MNQ) → production hand-back, each start gated on PNL plant + `[BROKER]` +
  flat exchange; any failure stops everything, sets NO_DEPLOY and alerts.
- `scripts/pre_rth_check.sh` + `deploy/pre-rth-check.{service,timer}`: weekday 09:00 ET
  readiness report to Telegram.
- `deploy/dashboard-api-local.service`: the :8080 dashboard backend as a managed unit
  (MemoryMax 4G) instead of a terminal process. `deploy/browsers.slice` + `scripts/chrome-capped`
  (installed as the google-chrome desktop entry): Chrome capped at 10 GB.

### Changed
- **Trading date rolls at 18:00 ET** (`trading_date_str`, orb_config.hpp): the executor's
  "today" — trade_date, daily P&L seed, trades_today seed, rollover reset — now matches CME and
  Tradeify's daily reset (17:00 CT). An evening session no longer inherits the closed day's P&L
  and trade count (it previously did until midnight ET). RTH rows are unchanged.
- **Instance lock is per account** (orb_db.hpp `instance_lock_key`): one executor per account
  whatever its engine/strategy tag, so an ORB and a trend executor can never trade one account
  at once.
- Local units: executor + collector `OOMScoreAdjust=100` (user services defaulted to 200, which
  made a 7 MB executor a likelier OOM victim than browser tabs), `MemoryMin`, `CPUWeight/IOWeight
  1000`; paper engine `MemoryMin=64M MemoryMax=2G`.

### Fixed
- `collector_watchdog.sh`: `08xx`/`09xx` ET times were parsed as octal (`value too great for
  base`), so the market-hours check misfired 08:00–09:59 ET. Also adds a once-per-episode
  memory-pressure alert (< 2 GB available, or swap ≥ 95% with < 4 GB available).
- **Orphaned stop → hidden position → broker liquidation (2026-09-23, tradeify, ~$600).**
  A stop cancelled by CLIENT id (server basket not yet mapped) came back
  `Cancellation Failed` (tid=351 notify_type=17), which the executor did not handle; at
  FLAT the guard was purged as "confirmed"; 12s later the still-working stop filled
  2 MNQ and the fill was logged as `unknown user_tag … ignoring (not our order)`; the
  account stayed long 2 through five restarts (the tid=451 position snapshot was only
  requested when the DB said non-flat) until Tradeify auto-liquidated it 32 minutes later.
  `src/execution/order_manager.hpp`: client-id cancels are tracked (`client_only_cancels_`)
  and never purged as confirmed; `on_stop_server_mapped()` re-sends such a cancel by server
  id the moment the id arrives; `on_cancel_failed()` re-adopts the still-live stop (at its
  real level) while in a trade and cancels the replacement, or keeps the guard when flat;
  `net_qty_consistent()` + `NetReconciler` express "does the exchange agree with us".
  `src/execution/executor_main.cpp`: fills on our account for orders we do not track are
  routed into the order manager's stale-stop guards (FLAT → unwind/ghost-halt; in a trade →
  halt entries) instead of ignored; `Cancellation Failed` is handled; the tid=451
  subscription is unconditional on every start and reconnect; every update is reconciled
  against our state and a mismatch older than `net_mismatch_grace_ms` (new config,
  default 5000) is unwound once and halts entries; the broker's own `day_pnl` is checked
  against `daily_loss_limit` and halts + flattens (`RiskManager::halt_external`).
  Six regression tests replay the sequence (`tests/execution/test_order_manager.cpp`).
  **And the position feed itself never worked**: the whole log held zero tid=451 updates,
  because `RequestPnLPositionUpdates` was sent on the ORDER plant while Rithmic serves
  position/P&L on the PNL plant. The executor now opens a third session (`PNL_PLANT`
  login → tid=400 subscribe → tid=402 forced snapshot, `proto/rithmic.proto` gains 402/403),
  reads it in `pnl_loop`, heartbeats it, and halts entries (`pnl_plant_unavailable` /
  `pnl_subscribe_rejected`) when it is missing — no position truth, no trading. Verified
  live on tradeify: snapshot net 0 with 12 buys/12 sells, balance 24,584.52, day P&L
  -605.34 → broker guard halted at once.
  **Testable and drillable.** `src/execution/notification_router.hpp` holds the routing
  policy for unowned fills, cancel outcomes, the unwind plan (extra contracts → trade them
  away; position closed externally → `OrderManager::adopt_external_close`, never re-enter)
  and the broker-P&L check; `tests/execution/test_incident_replay.cpp` (new gate binary)
  replays the incident's own prices, ids and message order through it and asserts the
  position is closed within seconds in every variant. `nq_executor --drill orphan` +
  `scripts/drill_orphan.sh <account> recover|crash` run the same shape on the REAL account
  with one contract (strategy halted): an untracked BUY 1 after the exchange confirms flat,
  then the guards and the PNL-plant reconciliation must close it (PASS/FAIL, exit code,
  grid-notify); `crash` SIGKILLs the process before the unwind and proves the restart path.


### Added
- **Eleven more signal families in the trend engine, fleet 110 → 150** (founder: "add as
  many uncorrelated strategies with different variations"; `src/execution/trend_strategy.hpp`,
  `tests/execution/test_trend_strategy.cpp` 32 checks, `config/paper_fleet.json` +40).
  Trend: `trend_day` (recognises a trend day after N minutes — net move ≥ k×ATR and ≥ 80%
  of minute closes on one side of VWAP — then buys every VWAP / fast-EMA pullback),
  `failed_breakout` (turtle soup: a Donchian break that closes back inside within N bars
  is faded — the negative of the Donchian family), `keltner_ride`, `ichimoku`
  (Tenkan/Kijun/Senkou, cloud filter), `roc_momentum` (ROC in ATR units at a fresh
  extreme), `delta_trend` (price high confirmed by a session cumulative-delta high from
  the tick aggressor flag; divergence blocks), `fib_pullback` (38–62% retrace of the last
  impulse, resumption close). Mean reversion, the least correlated to everything above:
  `vwap_fade` (≥ k×ATR from VWAP then a turn back, target VWAP), `band_fade` (Bollinger
  close outside then back inside, target the mid), `rsi2_pullback` (Connors RSI-2 on the
  trend side of the slow EMA). Two generic overlays usable by ANY mode: `htf_tf_min` /
  `htf_ema` (higher-timeframe alignment gate in `can_enter`) and `chandelier_mult`
  (flatten k×ATR off the best price since entry). Bars now carry aggressor-buy volume.
  Replay 2026-09-21 (afternoon data only): 22 of the 40 new variants fired — losers
  `mr_vwap_fade_1m` −69.5, `fail_bo_10_1m` −52.0, `delta_trend_1m` −52.0; winners
  `trend_day_ema_1m` +100.0, `trend_tod_1530_close_drive` +46.5. Dashboard: analysis text
  for every mode (mean-reversion modes labelled as such), gate/exit rules, and
  coverage-aware silence reasons.

### Added
- **Four new models in the zoo (10 → 14)** (founder: "go for it"; dashboard
  `ui/services/models/`): **#11 exit_optimizer** — re-simulates every closed trade on its
  own recorded 1-minute path under a grid of break-even trigger / trail step / delay rules
  with the brokers' exact mechanics (incl. the live working-stop rule), chooses per engine ×
  session on earlier days and judges on later days against the live rule 3/10/300;
  **#12 signal_meta** — LightGBM classifier over the signal log (every base entry taken,
  with the book at the signal) predicting P&L > 0, with base rate and a gate backtest;
  **#13 overlay_uplift** — per overlay, the contexts (engine × session × spread × bid share)
  where the paired value vs the base is positive, bootstrap CI per context, regressor when
  ≥300 rows; **#14 fill_cost** — LightGBM regressor of the recorded book cost ($) from spread,
  book state, session, engine and hold time, vs the mean-cost baseline. All refuse below
  their minimum data and report a time-ordered hold-out; none is wired into trading.

### Added
- **Collector runs 24/7 with an external watchdog** (founder: "make sure that the collector
  runs 24/7 uninterrupted"; `scripts/collector_watchdog.sh`,
  `deploy/rithmic-collector-watchdog.{service,timer}`, installed as a user timer every
  2 min). Independent of the collector's in-process link watchdog: restarts the unit if it
  is not active or if the newest NQ tick is older than 120 s while CME Globex is open
  (Sun 18:00 → Fri 17:00 ET, daily halt 17:00–18:03 excluded), alerts once via grid-notify
  and once on recovery, rotates the append-only logs daily above 50 MB (copy + truncate,
  gzip), and writes `data/validation/collector_watchdog.json`. Drilled live: a forced
  stale threshold restarted the collector and alerted; the next run reported recovery and
  ticks resumed within seconds. Fleet audit: tick-feed and feed-hole checks are
  market-hours aware (closed market → warn, not fail) and show the watchdog's last run.
  State verified: units enabled + lingering, sleep-on-AC "nothing", no unplanned reconnect
  in 36 h; disk 93% used but the DB grows ~250 MB/day against 67 GB free.

### Changed
- **Live ORB allowed 5 trades a day** (`config/tradeify_config.json` `max_daily_trades` 3 → 5;
  founder: "add more trades a day to 5 live orb"). Worst case five full stops at 2 MNQ
  = −$340 plus $40 commission, inside the −$500 daily halt. Restarted flat before the open.
- **Live executor sized to 2 MNQ** (`config/tradeify_config.json` `qty` 1 → 2; founder:
  "make it 2 sure"). Per full 15-pt stop −$68, three stops −$204 (daily halt −$500),
  Monday's +96-pt trade would be +$376. Restarted flat at 07:05 ET before the open.

### Fixed
- **Paper trailing exits were tighter than live** (`src/paper/paper_broker.hpp`,
  `src/execution/order_manager.hpp` `exchange_stop()`, parity + broker tests): the live
  executor re-places its resting exchange stop only when the in-memory level is ≥
  `trail_step` beyond the last placed stop (cancel/resubmit suppression), and its software
  backstop watches the EXCHANGE stop — so the working live stop steps up in 10-pt jumps.
  The paper broker ratcheted every tick and exited at the in-memory level. It now keeps a
  `placed_stop_` with the same rule and exits against it; `stop_price_` stays the display
  value. The parity test now compares the two WORKING stops (0 ticks apart on all paths),
  and the golden file is re-frozen for this intended change (trail exits later, larger
  give-back — that is what live does). Board wording updated.

### Added
- **Validation system — every strategy, number and comparison has an independent check**
  (founder: "create a comprehensive plan … and create it"). Four layers:
  1. *Rules*: `tests/paper/test_parity_paper_vs_live.cpp` drives the paper broker and the
     LIVE `OrderManager` with the same config and price path and asserts the stop never
     differs by more than the tick snap, break-even fires on the same tick, and the exit
     reason matches (5 paths: BE-only long/short, trail long/short, plain stop). This is
     the test that would have caught the 2026-09-23 break-even mismatch. In hermes.
  2. *Numbers*: `scripts/golden_replay.sh` + `tests/golden/2026-09-22_rth_open.csv` — a
     frozen 09:25–11:00 window (830 trades across the 1,553-strategy fleet, bid/ask shadow
     included) replayed on every full `make hermes` and diffed trade-by-trade; `--twice`
     requires byte-identical output (determinism, verified). `make golden`,
     `make golden-freeze`. Fleet audit recomputes every strategy's n, wins, total,
     expectancy, profit factor and max drawdown in plain SQL and compares with the
     insights API (`Performance KPIs` section).
  3. *Live vs replay*: dashboard `ui/routers/validate.py` — `POST /api/cpp/validate/parity`
     replays the Globex day and matches each live trade to a replay trade (same strategy,
     direction, entry within 3 s), listing live-only, replay-only, P&L and exit-reason
     differences per engine; `GET /api/cpp/validate/status`. Scheduled in `ui/app.py`:
     fleet audit every hour, parity + audit at 18:30 ET, Telegram alerts via grid-notify on
     the first failure and on recovery (`data/validation/`).
  4. *Statistics*: the Compare leaderboard adds a bootstrap 90% CI and P(mean > 0) over
     per-base paired deltas and a verdict that stays "insufficient evidence" until ≥30
     paired signals across ≥2 sessions — direction is shown, decisions are withheld.
  Strategies page → Audit tab gains a Validation panel (golden, parity, hourly audit, hermes).

### Fixed
- **Paper break-even now matches the live executor** (`src/paper/paper_broker.hpp`,
  `tests/paper/test_paper_broker.cpp`): the paper broker moved the stop to break-even only
  after the 300 s trail delay, while `order_manager.hpp` moves it the first time MFE reaches
  `trail_be_trigger`, with no delay (the trail alone waits for the delay). Paper now does
  the same; the test that encoded the old rule was replaced by two that encode the live one.
  `PaperDb` seeding queries stay unscoped when no account label is set (tests, tools).
  Board wording for break-even / trailing corrected to the code.
- **Break-even on book flip could put the stop on the wrong side of the market**: a flip
  while the trade was under water moved the stop to entry+1 above price, which the paper
  broker then "filled" at a price better than reality. The overlay now moves the stop only
  when price is already beyond the break-even level (new broker test); its measured value
  is re-derived below.

### Fixed
- **Three honesty fixes found by auditing the book layer** (`paper_quote.hpp`, both brokers,
  dashboard `compare.py` / `paper.py`): (1) a book fill could look better than reality when
  the quote was stale during a spike — 246 of 1,668 replay trades beat the reported fill by
  more than $2; the honest fill is now never better than the print that triggered it
  (0 of 1,667 after), and the SQL back-fill applies the same floor. (2) "Take profit into
  pressure" fired 65 times at ~zero profit because slippage ate a one-tick gain; it now needs
  `book_tp_min_pts` (1.0) first. (3) The replay endpoint cleared audit trades but not audit
  signals, so gate values and blocked counts for the replay label accumulated across runs
  (they doubled between two runs); the signal log is cleared per replay too. Round-2 numbers
  were re-derived after the fixes.

### Added
- **Book overlays, round 2 — six more ways to use the bid/ask, as paired siblings** (founder:
  "lets try them"; `src/paper/paper_quote.hpp`, both brokers, `OrbConfig` paper knobs). Entry:
  `imbalance_max` (INVERTED gate — only when the book leans against the move; the round-1
  numbers showed the book at a breakout is contrarian). Management/exit: `book_exit_flip`
  (flatten when the book flips against the position), `book_be_on_flip` (stop to break-even
  on the flip, no trail delay), `book_tp_imbalance` (in profit and the book stacks in favour →
  take profit). Sizing: `book_size_agree` (+N contracts when the book agrees at fill).
  Execution: `fill_wait_secs` (wait up to N s for a 1-tick spread or a microprice lean).
  Siblings `__inv __bx __bbe __btp __sz __wait` for the 105 bases with ≥2 live trades →
  fleet 923 → 1553 (1.3% CPU, 30 MB). Compare page: entry gates are scored by "what they
  removed", exit/sizing/execution overlays by paired Δ P&L, with the book-exit count per
  sibling; a "Live executor vs the book" line gives the real account's slippage beyond the
  touch at signal time. Model features `bbo_imb_open15` / `bbo_imb_persist_open15`
  (opening-book pressure and its persistence). Unit test: 55 checks.

### Added
- **The paper fleet reads the book: honest fills, entry gates, a signal log, an
  order-book family, and ~300 sibling variants to compare against** (founder: "Honest
  paper fills, Entry gates on the signal engines, New signal families, Data-quality and
  execution analytics"). `src/paper/paper_quote.hpp` (new): `Quote`/`QuoteState` — spread
  in ticks, bid-share imbalance, microprice, freshness (5 s), the gates and the honest
  market fill. Both paper brokers now take the collector's BBO stream (`PaperDb::poll_bbo`,
  one-sided rows forward-filled; the runner applies quotes in tick-time order, live and in
  replay). **Every paper trade carries its bid/ask SHADOW fill** (`entry_bbo`, `exit_bbo`,
  `pnl_bbo_usd`, `spread_entry/exit_ticks`, `fill_model`; migration 013) whatever fill
  model it ran with — buy at ask, sell at bid, stops no better than the touch — so the cost
  of the book is measured on the same trade. `OrbConfig` gains paper-only knobs (no strategy
  logic touched): `fill_model` (`last_slip` default | `bbo`), `spread_gate_ticks`,
  `spread_gate_rel` (× rolling spread), `imbalance_min`, `microprice_lead`, `base_id`,
  `overlay`. A gated signal is logged, taken or `blocked:<reason>`, with the book at that
  instant, in the new `paper_signals` table — join a sibling to its base by time to see
  exactly what a gate removed. Trend engine: `on_quote` + mode `book_imbalance` (sustained
  bid share ≥ x for N s with a tight spread → with the pressure; flat when it normalises).
  Fleet 197 → 923: 179 bases (every strategy that has traded + every non-09:30 session variant) × 4
  overlays (`__sg` spread ≤ 1.5× rolling, `__imb` bid share 0.55, `__micro`, `__all`) +
  10 `book_imb_*` variants across sessions (one on `fill_model=bbo`); 3.7% CPU, 23 MB. Bases are untouched;
  siblings carry `base_id`/`overlay` in their params. Unit test: 49 checks incl. gates,
  fills and the imbalance mode. Replay of 2026-09-22 RTH across 923: 619 trades, all with
  shadow fills — reported avg $1.60/trade vs $0.57 at the book (≈ $1.03 spread cost at a
  2.2-tick entry spread); gates logged 619 taken / 172 blocked on imbalance / 52 on
  microprice. Dashboard: **Compare page** (`/compare`, `ui/routers/compare.py`) — honest-fill
  panel (same trades, reported vs at-the-book, by session / engine / strategy, winners that
  flip to losers), overlay leaderboard (better/worse/same bases, Δ P&L, paired Δ expectancy,
  signals blocked), base-vs-siblings table, and a signal-by-signal view per base (book at
  the signal, each sibling's decision and outcome). Replay reasons are sibling-aware ("gate
  removed every signal — imbalance ×3"); board analyses list the overlay rules; the fleet
  audit gains a "Book layer" section (shadow fill on every trade, sibling ⊆ base signals,
  shadow never better than reported). Model feature `bbo_spread_rel_1h` (spread regime).

### Changed
- **Paper fleet account envelope switched off** (`config/paper_fleet.json`
  `daily_loss_limit` / `trailing_drawdown_cap` = 0, both disable their check). 197 strategies
  × 1 MNQ on one $25k envelope is not an account: on 2026-09-22 the fleet ran +3.1k by 10:13
  and gave back 2.6k, the $1k trailing cap latched at the 18:00 rollover, every strategy was
  halted and the evening session gathered nothing. The risk unit is the per-strategy
  `strategy_daily_loss_limit` (−250/day) and the per-strategy max-drawdown KPI; the account
  envelope stays available for a curated live-sized subset later. Halt cleared with the
  fleet stopped (peak reset to equity).

### Fixed
- **Live executor took no trade on 2026-09-22: its session open had been rewritten to
  17:31** (`config/tradeify_config.json`, dashboard `cpp_engine.py go_live`). The go-live
  endpoint defaulted `open_in_minutes=20` and PATCHED `session_open_hour/min` into the
  account config file; the 17:30 ET go-live on 09-21 (single-feed switch) therefore left
  `session_open=17:31` on disk, the 24/7 unit reloaded it at 17:30, the ORB window
  17:31–17:36 sat inside the Globex halt, no range ever formed, and the executor logged
  "ORB building … (no range yet)" for the whole 09-22 session while the feed was complete.
  Config restored to 09:30 (`git checkout`), executor restarted flat at 18:04 ET. go-live
  now defaults to the RTH open; `open_in_minutes` / `session_open_*` are explicit-only test
  overrides and documented as persisting in the file. The fleet audit gains a "Live
  executor" section (session open must be 09:30, dry_run, trade_route, contract, today's
  ORB/trades) so a drifted config is red on the strategies page before the next open.

### Fixed
- **End-of-window flatten was batch-granular in replay** (`src/paper/paper_main.cpp`): the
  runner asked strategies to flatten once per poll batch on the wall clock (5,000 ticks ≈
  minutes of replay time), so a strategy whose window ends at 14:00 could be stopped out at
  14:04 in a replay while live (100 ms polls) would have flattened at 14:00:00 — audits did
  not match live. The check now also runs on every minute boundary of TICK time in both
  modes; verified: `mr_vwap_fade_5m_midday` exits `signal_flatten` at 14:00:00 (−3.0) instead
  of `stop` at 14:04:58 (−25.5). Found by the fleet audit (`scratchpad/audit_fleet.py`:
  every closed trade checked against its own strategy's window, flatten time, daily cap,
  P&L arithmetic, direction sign and MAE ≤ P&L ≤ MFE — 0 failures after the fix).
- Per-strategy KPIs (dashboard `insights.py`): expectancy, max drawdown of the cumulative
  curve, average hold, trades/day and a per-session split, shown on the strategies page.
- **Strategies page as a control centre** (dashboard `strategies/page.tsx`): four tabs —
  Fleet, Replay, Audit, Insights & KPIs — with live badges (audit fail/warn, replay traded
  count, trade count); the Fleet tab gains a search box (name, engine, any param), family
  filters (ORB / MTF / Trend / Mean reversion), session filters (RTH, pre-market, London,
  Asia, evening, Globex day, derived from each strategy's own window) and status filters
  (traded today / never traded / halted); each row's drawer shows its KPIs (expectancy,
  profit factor, max drawdown, hold, trades/day, win rate, MFE capture, sessions) and a
  "view trades on chart" link (`/chart?paper=<id>` deep link); the Replay tab gains presets
  (yesterday RTH, today so far, last overnight, last 24 h) and a name filter.
- **Chart page shows the fleet** (dashboard `chart/page.tsx`, `paper.py chart_trades`,
  `chart.py /api/data/range`): defaults to the Paper Fleet source over the collector's own
  tick window (`pg_start`/`pg_end`), overlays every strategy's entries and exits with one
  stable colour per strategy and the strategy name on the marker, unions the executor's
  real fills as `LIVE <account>`, groups the selector by engine, and adds a "Strategies in
  view" legend (n, W/L, P&L, open positions) that isolates a strategy on click.
- **Fleet audit on the strategies page** (dashboard `ui/routers/fleet_audit.py`,
  `GET/POST /api/cpp/fleet-audit[/run]`): the five audit sections (processes & feed, fleet
  state, trade integrity, model input data, KPIs) as a collapsible panel with a run button —
  seconds to run, no replay, no writes; the last result is cached and shown on load.

### Fixed
- **Top-of-book (BBO) had been silently dropped since the collector was written**
  (`proto/rithmic.proto`, `src/client.cpp`, `src/collector.cpp`). The collector subscribed
  `LAST_TRADE|BBO` on every connection, Rithmic acknowledged (`rp_code=0`) and streamed
  ~150 BestBidOffer frames a minute, but the proto carried invented field numbers
  (1542xx/1543xx) so every frame parsed with no bid and no ask and was dropped before
  the counter — `bbo=0` in every status line, 0 rows in `bbo`, and nothing said why.
  Verified on the wire with a raw-frame dump + `protoc --decode_raw`: bid 100022/100030,
  ask 100025/100031, orders 159000/159002, lean (micro) price 154909, is_snapshot 110121;
  presence_bits 1 bid / 2 ask / 4 lean. The "depth" request sent update_bits 64, which
  is HIGH_BID_LOW_ASK (template 153) — not depth; it is off by default now
  (`RITHMIC_MD_DEPTH_BITS`). ORDER_BOOK (bit 4, template 156) answers with an EMPTY book
  (presence_bits 0) on the Tradeify login, i.e. aggregated L2 is not entitled; depth-by-order
  (160) never arrives. Diagnostics kept: per-template frame counts in the status line,
  `ResponseMarketDataUpdate` rp_code logging, `RITHMIC_MD_DUMP_DIR` raw dumps,
  `RITHMIC_MD_EXTRA_BITS`. Dashboard: four top-of-book features
  (`bbo_spread_ticks_1m`, `bbo_imbalance_1m`, `bbo_microprice_dev_ticks`, `bbo_updates_5m`)
  in `features.py`, NaN for trades before the fix.

### Added
- **All-sessions fleet, 150 → 197** (founder: "want strategies to trade on all markets to
  gather more data per session"). The trend engine gains a session anchor
  (`session` = `rth` | `globex` | `window`: what VWAP, cumulative delta and the "session
  open" used by opening_drive / gap / trend_day are measured from) and entry windows that
  cross midnight (`win_start` > `win_end`, e.g. 2000–0230, with the flatten at the wrapped
  end). 47 new variants: **Asia** 20:00–02:30 (Tokyo-open ORBs, 21:30 HK-open momentum,
  Donchian, VWAP/band fades, Keltner, supertrend, NR7, RSI-2, delta, two MTF windows),
  **London** 02:30–07:30 (03:00 drive, Asia-range break, 03:30 EU-cash momentum, trend day,
  the full mode set, MTF), **US pre-market** 07:00–09:25 (08:30 data-release momentum,
  drive, fades, MTF), **US evening** 18:00–23:00 (Globex reopen drive, Donchian, fades,
  Keltner, RSI-2, MTF) and four **whole-Globex-day** trend followers on 15/60-minute bars.
  Every session now feeds the dataset; insights gain a per-session bucket
  (`by_session`, ET: Asia / Asia late / London / US pre-market / RTH open / midday / close /
  US evening) on the strategies page. Caveat recorded up front: overnight paper fills at one
  tick of slippage are optimistic in a thin book — trust the fill-quality model before
  sizing anything there.

### Fixed
- **Whole fleet halted `account_trailing_dd` on a phantom peak; seed queries read audit rows**
  (`src/paper/paper_db.{hpp,cpp}`, `src/paper/paper_main.cpp`). Two residues of the first,
  un-isolated replay on 2026-09-21: (1) it had written the phantom post-gap P&L into the LIVE
  `paper_account` row (peak 26,532 vs a real day peak of 25,472), so the persistent
  trailing-drawdown halt fired at the next restart and every strategy sat idle; the row is
  rebuilt from the label's own closed trades (equity = peak = 25,457.5). (2) `sum_pnl`,
  `sum_pnl_since` and `count_trades_since` matched on `strategy_id` alone — `paper_trades`
  is keyed by strategy across labels, so the 101 audit rows were seeding live daily trade
  counts, P&L and halts at every restart. All three are now scoped by
  `PaperDb::set_account_label(fleet.account_label)`. Operational note: the fleet persists
  its in-memory account state back to `paper_account` while running — repair the row with
  the unit STOPPED, or the repair is overwritten before the restart reads it.

### Added (earlier today)
- **Trend engine — 11 configurable trend modes, 28 fleet variants (fleet 82 → 110)**
  (`src/execution/trend_strategy.hpp`, `tests/execution/test_trend_strategy.cpp`,
  `src/paper/paper_main.cpp` engine `"trend"`, `config/paper_fleet.json`). One
  header-only `TrendStrategy` driven by `TrendConfig{mode, tf_min, win_start/win_end,
  allow_longs/shorts, exit_on_flip, time_stop_min, …}` builds 1-minute → `tf_min` bars
  in ET (a bar closes when its bucket's last minute completes) and emits the same
  `OrbSignal`s the ORB does, so it shares the plain `PaperBroker` (stop / BE / trail
  from the strategy's `OrbConfig` overrides). Modes: `donchian` (N-bar channel close),
  `ema_pullback` (fast/slow EMA, touch-and-resume), `vwap_trend` (retest of a sloping
  session VWAP), `opening_drive` (first N minutes' direction ≥ k×ATR, pullback entry),
  `gap_go` (open gap vs prior RTH close, unfilled after a wait), `pdhl_breakout`
  (prior-day or overnight high/low with volume), `squeeze` (Bollinger inside Keltner
  then a break), `supertrend` (band flip; opposite flip flattens and re-enters once
  flat), `nr7` (narrowest-of-N break), `rs_continuation` (NQ leads the ES reference
  feed by ≥ bp), `tod_momentum` (continuation at a clock time). 14-check unit test in
  the hermes gate. Replay audit 2026-09-21 (afternoon data only): 12 of 28 variants
  fired, with losers recorded (`trend_donchian_10_1m` −64.5, `trend_supertrend_7_3_1m`
  −51.0, `trend_rs_es_15m` −36.5) alongside winners.
- **Feed-gap guard in the paper engine** (`src/paper/paper_main.cpp`,
  `paper_config.hpp` `feed_gap_reset_secs` = 300). A hole in the tick stream longer
  than the threshold (collector down, forced logout, box asleep) now restarts the
  session state of every orb/trend strategy that is flat and not halted (daily trade
  counters re-seeded); open positions keep their stops, halts stay latched, MTF keeps
  its bar history. Why: on 2026-09-21 the feed had no ticks 09:33–11:01 ET, and the
  first tick back fired 36 "entries" across the fleet (an ORB range built on one
  minute of ticks, Donchian channels with a 90-minute hole) — all winners by
  construction of the jump, +$1,081 of phantom P&L. Replay of the same day with the
  guard: 62 trades / +1,059.5 → 26 trades / −21.5. Logged as
  `[PAPER] WARN feed gap …`. Applies identically in replay so audits match live.

### Fixed
- **Trend runners crashed the fleet at start-up (SIGSEGV, 20 systemd restarts)**
  (`src/paper/paper_main.cpp`): the trade-close hook and the position-resume path
  branched on `strategy` (ORB) vs "else bracket broker", so a `trend` runner
  dereferenced a null `PaperBracketBroker`. Both now branch on which broker exists;
  a resumed trend position also seeds the strategy's in-position flag
  (`TrendStrategy::seed_open_position`) so the flip-exit and time-stop see the leg.
- **Stop cancels were being refused by Rithmic (rp_code=1045) — every trailing-stop
  update left the superseded stop WORKING** (`proto/rithmic.proto`,
  `src/execution/executor_main.cpp`, `tools/cancel_stops_main.cpp`). `RequestCancelOrder`
  omitted `manual_or_auto` (tag 154710, the field new orders send as
  `manual_or_auto_select`); Rithmic answered each cancel with `ResponseCancelOrder`
  `rp_code=1045` and no notification, and the executor never parsed tid 317, so the
  refusal was invisible — 2026-09-21 on Tradeify, 9 stale SELL stops sat "trigger
  pending" behind 725 silently refused re-cancels. All cancel builders now send
  `manual_or_auto=2` (AUTO); verified live: all 9 cancelled within 200 ms.
  `cancel_stops` also cancels by Rithmic's `server_basket_id` (the client `MNQ-…` id
  is refused too), logs tid 317 responses, and bounds its close. This supersedes the
  "cancel ACK reverse map" fix, which could not have helped.
- **ORDER_PLANT login refusal no longer hammers Rithmic** (`src/execution/executor_main.cpp`):
  a rejected login (`rp_code=13` — returned both for "too many rapid logins / duplicate
  session" and for bad credentials) used to `co_return` into the 10s cycle loop, i.e. a
  fresh login every ~13s that kept 13 coming back on its own. The rejection is now logged
  with the server's text plus an audit row, and the cycle loop backs off 300s
  (`g_reconnect_delay_s`, reset to 10s on the next successful login).
- **SIGTERM works outside a live session.** `main()` set SIGINT/SIGTERM to `SIG_IGN` and
  relied on the session's `asio::signal_set`, which is only armed after ORDER_PLANT login —
  so `pkill -SIGTERM` was a no-op for an executor stuck in the connect phase or the
  reconnect sleep. Both now route to `handle_signal` (flag only), the sleep is sliced 1s,
  and the handler is re-armed before each sleep.
- **Subscription rejections log the server's reason** (order updates tid=309, market data
  tid=101) instead of just the numeric `rp_code`; a rejected order-update subscription
  also writes an `audit_log` error.

### Added
- **`pg` market-data source: the executor trades on the collector's Postgres ticks**
  (`RITHMIC_MD_PROVIDER=pg`; `orb_config.hpp` `md_provider`/`md_feed_symbol`/`md_poll_ms`,
  `executor_main.cpp` `pg_feed_loop`). A prop-firm login allows ONE TICKER_PLANT session,
  so the executor's own MD login and the 24/7 collector could not coexist (each kicked the
  other every ~35s on 2026-09-21). In pg mode the executor opens no MD session at all:
  `pg_feed_loop` polls `ticks` (symbol `md_feed_symbol`, default NQ, every `md_poll_ms`)
  from "now" (never replays history; the ORB is restored from `live_sessions`) and feeds
  the same `process_tick` handler the WebSocket loop uses. A stale-feed watchdog
  (`tick_timeout_s`, active window or in-position) halts NEW entries with reason
  `pg_feed_stale`, writes an `audit_log` error and fires `grid-notify`; the first tick
  after that unhalts. "MD connected" (`md_up()`) reports feed freshness in pg mode.
  Enabled for `tradeify` via `.env.tradeify` (`RITHMIC_MD_PROVIDER=pg`); verified live
  mid-session: ORB/trade count restored, order plant OK, ticks ~300 ms behind the wire.
- **24/7 collector as a systemd user unit** (`deploy/rithmic-collector-local.service`,
  installed to `~/.config/systemd/user/`, `Restart=always`): the local box's only
  Rithmic MD session. Feeds the paper fleet, the chart and pg-mode executors.
- **Whole local stack under systemd user units, enabled at boot, linger on**
  (founder: "lets have it running 24/7"): `deploy/paper-engine-local.service` and the
  template `deploy/nq-executor-local@.service` (EnvironmentFile `.env` + `.env.%I`,
  `Restart=always`, NO_DEPLOY guard, SIGTERM with a 45s stop window for the exit fill).
  The executor's own midnight-ET rollover plus the live_sessions/live_trades restore on
  restart make a wrapper unnecessary. The dashboard's Go Live / Stop / Paper ▶■ now drive
  the units when installed (`systemctl --user restart|stop`), so a stop is never undone
  by `Restart=always`; without the units the nohup path is unchanged.
- **Trade context dataset** (`migrations/011_trade_context.sql`, computed by the
  dashboard's new `ui/routers/insights.py`): for every closed live/paper trade, the
  opening-range size, entry minutes after 09:30, distance beyond the level at fill,
  pre-5/15-minute range and volume, overnight (Globex) range, open gap, day range at
  entry, hold time, MAE/MFE and MFE capture — all from the collector's `ticks`. Served as
  win-rate/expectancy buckets on the dashboard's /strategies page ("What good trades
  look like"). Buckets under 20 trades are flagged thin.
- **Trade-quality model** (dashboard `ui/services/trade_model.py`, LightGBM): a classifier
  for p(win) and a regressor for expected P&L over the trade-context features (+ the
  strategy's stop/trail and hour of day). Time-ordered hold-out (latest 30%), reported
  against the win-rate baseline, plus a "trade only what the model liked" replay on the
  hold-out and gain-based importances. Refuses to train below 60 closed trades or 12 of
  either class (today: 22 trades, 21W/1L → refused, by design). Endpoints
  `GET /api/cpp/insights/model`, `POST /api/cpp/insights/train`, `POST /api/cpp/insights/score`;
  auto-retrains daily 16:30 ET; model scores annotate best/worst trades on /strategies.
  Verified on a synthetic set with a planted signal (AUC 0.70 out-of-sample, planted
  drivers ranked top). **The executor does not consult it** — strategy logic untouched;
  wiring a gate is a separate, explicit decision.
- **Model zoo — ten analysis models** (dashboard `ui/services/models/`, page `/models`,
  API `/api/cpp/models[/train-all|/{name}/train|/{name}/predict]`, daily train-all
  16:30 ET), ranked by expected payoff: 1 day-type classifier (pre-open, from overnight
  bars), 2 trade filter, 3 adaptive stop & trail (quantile regression on MAE / MFE),
  4 breakout-failure (events mined from ticks at every opening-range cross), 5 variant
  selection (Thompson bandit over the paper fleet), 6 exit/stall model (trade paths
  rebuilt from ticks, 30 s samples, stall-rule replay), 7 regime clustering (k-means on
  hourly bars, P&L per regime per strategy), 8 fill-quality (live trades only),
  9 trade-management policy search (offline-RL baseline replaying real tick paths under a
  trail/BE/time-stop grid), 10 GRU sequence model on 1-minute bars. Shared rules in
  `base.py`: refuse below the model's minimum data, time-ordered hold-out only, baseline
  next to every score. All ten refuse on 2026-09-21 data (3 days of ticks, 22 trades) and
  all ten train correctly on synthetic sets. None is consulted by the executor.
- **Paper fleet 24 → 82 strategies** (`config/paper_fleet.json`, founder: "as many
  different, different times, different timeframes"): ORB at other session times (07:00
  and 08:30 pre-market, 10:00/10:30 second range, noon, 13:30, 15:00 power hour, 18:00 and
  21:00 Globex, 03:00 London, 04:00 EU — each with an eod/last-entry that keeps the session
  on one ET date), range lengths 1/2/30/60 min, entry/management rules (confirmation buffer,
  no-chase, trail-now/late, break-even variants, one-shot/five-shot, morning-only, cooldown,
  wide-stop/tight-trail, tight), and MTF scalper variants (HTF 5/30/60 min; flag-retest,
  sweep, onset, FVG, stochastic, all triggers; open/afternoon/Globex/London/all-day
  sessions; long-only/short-only; fixed 15/35 bracket; VWAP; long hold; no time stop;
  SMT on 30-min HTF). Every key is one the engines already parse; verified 82/82 active.
- **Feature builder + validation** (dashboard `ui/services/features.py`,
  `ui/services/data_quality.py`, `migrations/012_trade_context_features.sql`): per trade, a
  50-feature multi-timeframe vector (1/5/15/60-min bars: returns, realized vol, ATR,
  range position, EMA9−21, EMA slope, volume z, VWAP distance, plus session context)
  built ONLY from bars that closed before the entry — the builder asserts the invariant —
  stored in `trade_context.features` with a validation verdict (NaN-heavy / out-of-range
  rows flagged, not used blindly). Data-quality report per trading day (RTH coverage,
  gaps, out-of-order, duplicates, bad prints, spikes, ES parity, feed staleness); days
  under 80% coverage are excluded from the day-level models. Feature selection
  (`FeatureSelector` in models/base.py): NaN/constant drop, |corr|>0.95 pruning,
  permutation importance on a time-ordered validation tail — fitted on the training
  slice only, applied consistently at predict time — wired into the trade filter,
  adaptive risk, breakout, exit, fill-quality and day-type models.
- **Account-list verification after ORDER_PLANT login** (tid=302 `RequestAccountList` →
  303, new messages in `proto/rithmic.proto`; `ResponseLogin` gains `fcm_id`/`ib_id`).
  The executor adopts the fcm/ib Rithmic reports for the configured `account_id` and
  refuses to proceed (300s backoff, audit row) when that account is not in the login's
  list — which is exactly what `1088 user has no permission to this account` meant on
  2026-09-21: the Tradeify configs carried the Rithmic *username* (`RTU…`) as
  `account_id`; the tradeable account is the `RTG…` id the list returns.

### Changed
- `config/tradeify1_config.json`: `trade_contract` MNQM6 → MNQZ6 (June contract had
  expired; MD subscription failed `rp_code=7`, no ticks).
- `config/tradeify_config.json` + `config/tradeify1_config.json` (+ the `.env`
  `RITHMIC_ENV_TRADEIFY_ORDER_ACCOUNT` / `RITHMIC_TRADEIFY_ACCOUNT` aliases):
  `account_id` RTU989361488 → **RTG25785042011**, the account Rithmic's account list
  returns for this login (RTU… is the username). Verified live: order-update
  subscription now `rp_code=0`. Both labels are the SAME prop account — run only one;
  `tradeify` carries the 25K Growth limits (-500 / 1000), `tradeify1` the template's.

### Removed
- **`tradeify1` account label retired** (founder: "remove tradeify1"). It was a
  dashboard-created duplicate of the same Rithmic account as `tradeify` with the template's
  loose limits ($50k / -$1000 / $2000 vs the real 25K Growth -$500 / $1000). Config moved to
  `config/archived/tradeify1_config.json` (the dashboard ignores `archived/`), its env file
  and backup moved alongside (mode 600), its `live_position`/`live_sessions` rows deleted;
  it never closed a trade.

- **Paper-engine replay / audit mode** (`src/paper/paper_main.cpp`, `paper_db.*`; founder:
  "audit, validate and test to see all strategies are live taking trades, bad ones too"):
  `paper_engine --replay-from "YYYY-MM-DD HH:MM" --replay-to … [--account-label audit]`
  runs the whole fleet over RECORDED ticks with the engine clock driven by tick timestamps
  (EOD, day rollover), no sleeps, no control polling, then exits. `PaperDb::set_trades_only`
  isolates it: only `paper_trades` (label `audit`) is written, no strategy/position/daily/
  account rows, and every `load_*` returns empty so a replay never inherits the live
  fleet's day counts or positions. Dashboard: `POST /api/cpp/paper/replay {from,to}`,
  `GET /api/cpp/paper/audit`, "Replay audit" section on /strategies with per-strategy
  results and a stated reason for every silent strategy. Learning queries exclude the
  `audit` label. **Incident worth the line**: the first replay (before isolation) overwrote
  the live fleet's `paper_strategies.account_label` and doubled today's `paper_daily`
  counts, because those tables are keyed by `strategy_id` alone — a SECOND fleet label on
  this schema collides with the first. Repaired from `paper_trades`; the isolation flag is
  the fix, the shared key is the standing caveat.

### Operational notes
- **Collector + executor cannot share the Tradeify login.** With the `tradeify` executor
  live (MD + ORDER_PLANT), starting `build/rithmic_engine` (reads `RITHMIC_AMP_*`, aliased
  to the same Tradeify credentials) made Rithmic force-logout the executor's MD (tid=77)
  every ~35s. Collector stopped; it needs its own MD login before the chart's intraday
  ticks and the paper fleet can run alongside a live Tradeify executor.
- Local launches (`~/Desktop/bot` backend, `CPP_LOCAL=1`) used to `.`-source `.env` /
  `.env.<account>`; an unquoted password containing `<` was truncated silently and the
  executor looped on `rp_code=13` for hours. Values in both files are now quoted and the
  backend loads them literally (systemd `EnvironmentFile=` semantics).

---

## 2026-09-20 — Paper per-trade MAE/MFE (excursion tracking)

### Added
- **MAE/MFE per paper trade** (`src/paper/paper_broker.hpp`, `paper_bracket_broker.hpp`):
  both brokers track max adverse/favorable excursion (points) per leg on every tick
  (exit tick folded in) and persist `mae_pts`/`mfe_pts` on the `paper_trades` row.
  Columns added via idempotent ALTERs in `PaperDb::ensure_schema()` +
  `migrations/010_paper_mae_mfe.sql`.

---

## 2026-09-20 — Paper fleet manual control channel (paper_control)

### Added
- **DB-backed manual control channel** for the paper fleet: new `paper_control`
  table (`migrations/009_paper_control.sql`, also ensured by `PaperDb::ensure_schema()`)
  with `(id, strategy_id, action, created_at, consumed_at)` and a pending-row index.
  The dashboard INSERTs a row; the engine polls unconsumed rows once per second
  (in-order by id), applies the action, and stamps `consumed_at`. Actions: `disable`
  → `halt_trading("manual_disable")`; `enable` → `unhalt_trading("manual_enable")`,
  refused with a WARN when the strategy's RiskManager is halted (halt left in place);
  `flatten` → broker `flatten("manual", ...)` at the last known price (safe no-op when
  flat). Unknown strategy ids and unknown actions are WARN-logged and consumed so a
  bad row is never re-applied. Poll errors rate-limit to one WARN per error-state entry,
  same as `poll_ticks`. Works with the market closed — the main loop spins on zero ticks.

### Changed
- **Startup enabled-merge**: `paper_strategies.enabled` is now operator-owned — the
  config seeds it on first insert but `upsert_strategy` no longer overwrites it on
  conflict. A config-enabled strategy whose DB row says `enabled=false` is still built
  at startup but immediately halted (`manual_disabled`) so a dashboard disable survives
  engine restarts. Config-disabled strategies keep the existing skip behavior.

---

## 2026-09-20 — ES reference feed (SMT live) + fleet feed-symbol starvation fix

### Fixed
- **Paper fleet consumed zero ticks**: `paper_fleet.json` polled `symbol='MNQ'` while the
  collector writes `symbol='NQ'` (front-month feed). Every strategy had been silently
  starved since the fleet launched — hidden by the weekend close. New explicit
  `feed_symbol` fleet field (default = `symbol`): trade the micro label (MNQ P&L maths)
  on the NQ feed, same price series. Verified live: 24/24 strategies now consume ticks.
- **`poll_ticks` WARN spam**: a missing `ticks` relation logged one WARN per 100ms poll
  (thousands of lines during the test_db window). Now logs once per error-state entry.

### Added
- **ES reference feed for the MTF SMT/intermarket module** (Pine v5 §1.8, previously
  inert): `RITHMIC_EXTRA_SYMBOLS` (comma-separated) makes the collector subscribe extra
  symbols LAST_TRADE|BBO alongside the primary (no depth); `.env` sets `ES`. The paper
  engine polls `reference_symbol`, aggregates ticks into 1m bars (minute-rollover, same
  convention as the strategy's own bars) and fans them out via
  `MtfScalperStrategy::on_reference_bar` to strategies that wired a reference feed
  (`wants_reference_feed()`). New fleet variant `mtf_smt_v5` (trigger_mode `smt`,
  `use_smt_entry`, reference ES, positive expected correlation) — fleet now 24.
- 4 new MTF tests: SMT bull divergence fires long, SMT inert without feed, correlation
  gate blocks uncorrelated reference, gate passes correlated/up-trending reference
  (37/37 green). End-to-end smoke over the weekend: synthetic NQ+ES ticks injected into
  `ticks` (source='synthetic', deleted after) → collector subscribed ES, engine logged
  `Reference feed wired (ES) — SMT machinery live`.

---

## 2026-09-19 — Paper fleet engine + MTF scalper port + collector heartbeat fix

### Added
- **Paper fleet** (`src/paper/`): local paper-trading engine running 20+ strategies
  (ORB variants + MTF scalpers) on a single account, fed by the PG tick stream — no
  per-strategy Rithmic sessions. Tables `paper_strategies/paper_positions/paper_trades/
  paper_daily/paper_account` (`migrations/007_paper_fleet.sql`), NOTIFY channel
  `paper_update`, config `config/paper_fleet.json`. Two broker modes: ORB-style exits
  (`paper_broker.hpp`) and strategy-driven brackets (`paper_bracket_broker.hpp`).
- **MTF Scalper** (`src/execution/mtf_scalper_strategy.hpp` + config): C++ port of the
  Pine v6 strategy, 33 unit tests (`tests/execution/test_mtf_scalper.cpp`). SMT filter
  disabled (no DXY feed locally). `auto_mode_flag_fix` defaults to intended semantics.

### Fixed
- **Collector weekend death spiral** (`src/client.cpp`): `receive_loop` no longer cancels
  the Beast read on heartbeat timeout; a sibling `link_watchdog` coroutine owns all
  heartbeat sends and closes the socket only after 2.5× heartbeat-interval silence.
  `LoginError` carries `rp_code` — 13 retries every 300s, other codes terminal.
- **Paper engine correctness**: ORB strategies now receive `notify_trade_filled` (was
  capped at 1 trade/day); bracket broker clears stale phantom exits; reversal flips
  supported; MTF halt resets at day rollover; restart-resume flattens at entry instead
  of feeding stale state; `MtfScalperStrategy::seed_state` added; daily-loss double
  count and `init_bracket` ratchet fixed.
- **`test_db` destroyed live data**: the suite created `ticks_test` then renamed it over
  the production `ticks` table and dropped `ticks`/`audit_log` on teardown. All tests now
  run inside an isolated `rithmic_test` schema via `options='-c search_path=rithmic_test'`
  on the connstr, recreated fresh at start and dropped at exit; catalog queries pinned to
  `schemaname/table_schema = 'rithmic_test'`. Verified: production row counts identical
  before/after a full run with the live collector writing.
- **Account mismatch**: `RITHMIC_TRADEIFY_ACCOUNT` and `RITHMIC_ENV_TRADEIFY_ORDER_ACCOUNT`
  in `.env` pointed at `RTSL25815692164`; aligned to the real Tradeify 25K account
  `RTU989361488` (matches `config/tradeify_config.json`).

---

## 2026-09-18 — Config/schema/doc drift fixes + local mode docs

### Changed
- **live_sessions primary key**: promoted from `(session_date, instrument, strategy)` to
  `(session_date, account_label, instrument, strategy)` locally, reusing the existing
  `live_sessions_acct_inst_strat_idx` unique index (`ADD PRIMARY KEY USING INDEX` — no table
  rewrite, brief lock). Two accounts trading the same instrument on the same day no longer
  collide. Migration `migrations/006_live_sessions_account_pk.sql` (idempotent `DO $$` guard)
  added for Oracle — **not run anywhere but local**.
- **Trade routes**: `trade_route` set to `"simulator"` in `config/MNQ_config.json`,
  `config/MES_config.json`, `config/MYM_config.json` (was `"Rithmic Order Routing"`, the route
  that silently cancels orders). `config/live_config.json` untouched (already `"simulator"`, frozen).
- **`.env.tradeify1`**: added `RITHMIC_TRADEIFY1_SYSTEM="Tradeify"` and
  `RITHMIC_TRADEIFY1_URL="wss://rprotocol-mobile.rithmic.com:443"` — executor was falling back
  to system `LegendsTrading` + wrong URL.
- **`audit_daemon`** (`src/audit_daemon_main.cpp`):
  - `write_metrics` INSERT fixed to match the real schema: `quality_metrics(metric, value,
    labels_json, ts)` (was nonexistent `labels`/`recorded_at` columns).
  - Config checks now honor `AUDIT_CONFIG_PATH` (default `config/live_config.json` preserved).
  - Data-freshness check queries `AUDIT_TICK_SYMBOL` (default `NQ`, matching the collector's
    `RITHMIC_SYMBOL`) instead of hardcoded `MNQ`.
- **Tradeify 25K config** (`config/tradeify_config.json`): `starting_balance` 25000,
  `trailing_drawdown_cap` 1000, `daily_loss_limit` -500, `trade_contract` `MNQZ6`,
  `trade_route` `"simulator"`, `order_env_prefix` `RITHMIC_TRADEIFY`.

### Removed
- **Legacy tables dropped locally**: `nq_trades`, `nq_session`, `nq_position` — all 0 rows,
  superseded by `live_trades` / `live_sessions` / `live_position`; nothing wrote to them.

### Docs
- **CLAUDE.md**: new "Local mode" section — dashboard `:3000`, backend `:8080` with
  `CPP_LOCAL=1` spawning collector + executor as `nohup` subprocesses; `deploy/*.service`
  are Oracle-only; warning to kill local executors before Oracle failback (no cross-host
  single-writer guard yet).
- **RUNBOOK.md**: per-account configs throughout — `nq_executor@tradeify` /
  `nq_executor-24x7@tradeify` (was nonexistent `nq_executor@RTH` / `nq_executor-24x7@default`);
  dry_run edits now target `config/tradeify_config.json`, with a note that frozen
  `config/live_config.json`'s `dry_run` is not read by any binary.
- **DATA.md**: `ticks.source` is no longer "Always `amp_rithmic`" — provider is Tradeify now.
- **scripts** (`hermes_lifecycle.sh`, `agents/deploy_manager.sh`, `provision_oracle.sh`):
  default Oracle service `nq_executor@RTH` → `nq_executor@tradeify`; provisioning checklist
  points at `config/tradeify_config.json`.

### Known issues
- `config/tradeify_config.json` and `config/tradeify1_config.json` share
  `account_id` `RTU989361488` — two configs point at one Rithmic account. Left unchanged
  pending a second account; do not run both executors simultaneously.

---

## 2026-05-09 — Hermes full lifecycle + multi-agent fleet

### Added
- **Hermes lifecycle** (`scripts/hermes_lifecycle.sh`): time-aware CI loop with 4 phases keyed to
  Eastern market hours — PRE_MARKET (fleet + deploy), PRE_SESSION (fast + Oracle health),
  SESSION (fast every 30 min, no deploy), POST_SESSION (fleet + session post-mortem + push),
  OVERNIGHT (fast every 90 min). Makefile targets: `hermes-lifecycle`, `hermes-lifecycle-once`,
  `hermes-lifecycle-local`.
- **`doc-syncer` fleet agent**: 5th specialist agent that audits all docs (CLAUDE.md, CHANGES.md,
  DATA.md, RUNBOOK.md) and Obsidian decisions against current code, fixes stale content, and
  creates missing decision notes automatically on every fleet run.
- **Obsidian vault** (`~/obsidian/rithmic/`): full vault setup with `.obsidian/` config (core
  plugins, daily notes, graph colors), templates (daily, session, decision), pre-seeded decision
  notes (simulator route, trail_be_offset, cancel+resubmit), and daily digest auto-generation.
- **`scripts/obsidian_daily.sh`**: generates/updates daily digest note aggregating all hermes runs
  and open audit warnings for the day.
- **`scripts/obsidian_session.sh`**: pulls trade data from PostgreSQL and writes a session
  post-mortem note. Accepts optional `DATE=YYYY-MM-DD` argument.
- **Makefile targets**: `hermes-note`, `hermes-fleet`, `hermes-fleet-fast`, `hermes-lifecycle`,
  `hermes-lifecycle-once`, `hermes-lifecycle-local`, `obsidian-daily`, `obsidian-session`.
- **Weekly digest**: lifecycle generates `daily/week-YYYY-WW.md` automatically on Fridays
  post-session with hermes health stats and session P&L summary.

### Removed
- ncurses dashboard (`src/dashboard.cpp`, `find_package(Curses REQUIRED)`) — UI lives in
  `bot/frontend` (Next.js). Removing also drops ncurses as a hard build dependency on Oracle.

---

## 2026-05-09 — Hermes codebase cleanup + doc refresh

### Removed
- **Dead callback machinery**: `OrderModifyCallback`, `modify_cb_`, `set_modify_callback()`,
  and `send_modify_order()` — the modify path was registered but never invoked; the executor
  uses cancel+resubmit instead. Removed from `order_manager.hpp`, `executor_main.cpp`, and both test fixtures.
- **Unused variable**: `constexpr int OFFSET = 20` in `executor_main.cpp` (shadowed by `UNWIND_OFFSET_TICKS`).
- **15 stale files**: old audit fragments (`docs/audit_gap_T{1,2,3}.md`, `AUDIT_GAP_REPORT.md`,
  `audit_gap_coordinator.md`), superseded deploy files (`deploy/nq_executor.service`, `deploy/update.sh`),
  archived configs (`config/legends_config.json`, `config/archived/tradeify_config.json`),
  orphaned scripts (`backup_pg.sh`, `cpp_build_check.sh`, `migrate_account_label.sql`, `validate-remote.sh`),
  stale docs (`docs/architecture.html`), untracked artefacts (`ticks.wal`, `rithmic.duckdb`, two `.docx` playbooks).

### Docs
- **RUNBOOK.md**: Full rewrite — Python-era workflow replaced with C++ operator procedures.
- **DATA.md**: Fixed unique index definition (5-column, not 3); removed Python psycopg2 example; updated file layout.
- **CLAUDE.md**: Fixed key files table (removed deleted `nq_executor.service`, added `nq_executor_24x7@.service`); removed "never push mid-session" rule.
- **engine_architecture.html**: Corrected audit check count (20 → 17 everywhere).

---

## 2026-05-04 — Hermes iteration 36 — dry-run full ORB cycle verified + FORCED_LOGOUT root cause fixed

### Fixed
- **Oracle session conflict**: Production `nq_executor` on Oracle VM was holding an
  active TICKER_PLANT session for `PAPA121797704102` (LegendsTrading), causing
  immediate FORCED_LOGOUT on every local dry-run connection. Stopping Oracle service
  releases the Rithmic session; local executor connects cleanly.
- **`src/execution/executor_main.cpp`**: Added 3-second ASIO-awaitable cooldown after
  FORCED_LOGOUT (tid=77) before reconnecting, preventing rapid reconnect storms from
  hammering the Rithmic server.
- **`src/execution/executor_main.cpp`**: Removed system-info probe from the MD reconnect
  loop — probe is only needed at initial startup. Probe→close→reconnect during each
  reconnect was adding unnecessary session churn.
- **`bot/frontend/src/app/live/page.tsx`**: Fixed `ReferenceError: cfg is not defined`
  — `cfg` state lives inside `EnvironmentCard`, not `LivePage`. Removed the
  `cfg?.dry_run` references from the Order status span; `op_connected=true` in dry-run
  (set by the C++ fix) is sufficient to show green "Connected".

### Added
- **`src/execution/executor_main.cpp`**: Executor now shuts down cleanly when the daily
  trade limit is reached and position is flat. After the last trade closes, logs
  `"Daily trade limit reached (N/N) — shutting down"`, flushes audit, sets
  `g_running=false` and stops the io_context. Previously the process stayed alive until
  EOD flatten time even when no further trades could be taken.

### Verified
- Full ORB dry-run cycle end-to-end: MD connected → 2-min range built (27861–27877)
  → SHORT breakout at 27860.75 → BE+offset trail → stop exit → DB write → 3 trades
  completed, +$2.00 daily P&L, daily limit respected, executor self-terminated.

---

## 2026-05-04 — Hermes iteration 35 — MD connection architecture restore + live page fix

### Added
- **`scripts/use-env.sh`**: Bash replacement for deleted `use_env.py`. Reads
  `RITHMIC_ENV_{NAME}_{ORDER,MD}_*` blocks and writes active aliases used by the
  C++ executor: `RITHMIC_LEGENDS_*` → ORDER_PLANT, `RITHMIC_AMP_*` → TICKER_PLANT.
  Applies per-env JSON config overrides via `jq`. No Python — pure bash.

### Fixed
- **`src/execution/orb_config.hpp`**: Reverted MD credential reading back to
  `RITHMIC_AMP_*` (original design). The `RITHMIC_MD_PROVIDER` dynamic lookup
  added in commit 7314556 broke compatibility with the env switcher pattern.
  Legends/Tradeify use WebSocket; AMP uses C++ R|API+ SDK.
- **`src/execution/executor_main.cpp`**: Executor now creates the `audit_log` table
  on startup via its own `audit_conn` (previously the table was only created by the
  `rithmic_engine` tick collector schema, not by `nq_executor`). Eliminates
  `relation "audit_log" does not exist` error flood during audit flush.
- **`src/execution/executor_main.cpp`**: MD plant system info probe now logs all
  available Rithmic system names and warns if the configured system is not in the
  list — aids diagnosis of login failures.
- **`src/execution/executor_main.cpp`**: `PQexec()` calls for audit schema creation
  now check result status and log errors instead of silently discarding failures.

### Infrastructure
- **`bot/.env`**: Set `CPP_PG_HOST=127.0.0.1 CPP_PG_PORT=5432` so the live page
  WebSocket (`/ws/cpp`) reads from local PostgreSQL during dry-run testing. The
  executor dry-run writes to local PG; the frontend now sees live position + price.
- **`bot/.env`**: Set `PG_PORT=5432` so `live.py` REST endpoints also read from
  local PG instead of Oracle SSH tunnel during local development.
- **AMP out of credits workaround**: Configured Legends credentials in `RITHMIC_AMP_*`
  with `USE_RAPI_SDK=OFF` (WebSocket mode). In dry-run, ORDER_PLANT is not opened
  so no session conflict; Legends TICKER_PLANT connects successfully. For live trading
  with real orders, AMP credits must be restored (or Tradeify MD configured).

### Results
- Hermes: **7/7 PASS** (build + 4 unit tests + db test + audit_daemon)
- Dry-run verified: live page at localhost:3000/live shows `md_connected=true`,
  live MNQ price (27810+), session equity, ORB state — all updating in real-time.

---

## 2026-05-04 — Hermes iteration 34 — OrderManager tests + silent failure fixes

### Added
- **`tests/execution/test_order_manager.cpp`** (19 tests): First test coverage for
  `OrderManager` — previously the most critical untested component. Covers the
  complete position state machine: FLAT → ENTRY_PENDING → IN_TRADE → exit lifecycle,
  rejection retry logic (3-strike rule → entry halt), trailing stop activation,
  software SL fallback, PnL calculation (long/short), dry-run auto-fill, MFE/MAE
  tracking.
- **CMakeLists.txt**: Added `test_order_manager` build target and CTest entry.
- **scripts/hermes.sh**: Added `test_order_manager` to unit test loop and build targets.

### Fixed
- **`src/execution/executor_main.cpp`**: 16 `ParseFromString()` calls were ignoring
  the bool return value — corrupted/truncated protobuf messages would silently
  produce zero-value fields. All calls now check the return and `continue` on failure.
- **`src/execution/orb_config.hpp`**: `load_dotenv` opened a `std::ifstream` without
  checking if the file opened successfully. Added `if (!f) return;` guard.
- **`src/db.cpp`**: `ensure_schema()` now closes stale open sessions (>2 h, no
  `ended_at`) on startup, eliminating the `session_health WARN` in `audit_daemon`
  that appeared after process crashes.

### Results
- Hermes: **7/7 PASS** (build + 4 unit tests + db test + audit_daemon)
- Audit daemon: **13 PASS / 0 FAIL / 0 WARN** (session_health WARN resolved)

---

## 2026-05-04 — Python purge + C++ Hermes runner

### Removed
- All Python files (`.py`), Python test suites, `requirements*.txt`, `pytest.ini`,
  `strategy/`, `ui/`, and all Python scripts from `scripts/`.
- `deploy/audit_daemon.service` and `deploy/live_trader.service` (dead / non-production).
- `scripts/quality_gate.sh` and `scripts/run_fast_tests.sh` (Python-dependent wrappers).

### Added
- **`scripts/hermes.sh`**: C++-only Hermes CI runner (build → unit tests →
  audit_daemon) replacing `scripts/hermes_session.py`. Writes
  `data/hermes_findings.json`. Supports `--fast` flag to skip DB test.

### Changed
- **`Makefile`**: fully rewritten with C++-only targets (`build`, `configure`,
  `test-unit`, `test`, `hermes`, `hermes-fast`, `push-eod`, `deploy`, `deploy-dry`,
  `clean`). No Python dependency.
- **`CLAUDE.md`**: rewritten for C++-only stack. Audit daemon explicitly documented
  as local/testing only — not deployed to Oracle.

### Architecture note
- Production (Oracle) runs `rithmic_engine` + `nq_executor` only.
- `audit_daemon` is a local-CI tool — never deployed to Oracle.

---

## 2026-05-04 — Hermes iteration 33 — cmd_status and cmd_verify test coverage

### Added
- **`tests/test_use_env.py`**: 9 new tests covering 2 previously-untested public
  functions in `scripts/use_env.py` (file grows from 21 → 30 tests):
  - `cmd_status` (5 tests): active env appears in output; password is masked as `***`;
    unset values show `(not set)`; available envs listed; warning when no envs found.
  - `cmd_verify` (4 tests): unknown env prints ERROR; missing credentials → SKIP;
    subprocess mocked — exit 0 → PASS; exit non-zero → "CHECK OUTPUT" message.

### Metrics
- Tests: 747 (+9); fast gate collects all 747
- All gates green (mypy 18 files, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 32 — hermes_session test coverage

### Added
- **`tests/test_hermes_session.py`** (22 tests): first dedicated coverage for
  `scripts/hermes_session.py`. Covers all 6 public functions via `_run` mocking:
  - `_run` (3 tests): `TimeoutExpired` → `(-1, "TIMEOUT…")`; `FileNotFoundError` →
    `(-2, "…not found")`; stdout+stderr combined.
  - `check_tests` (7 tests): PASS/FAIL by `passed`/`failed`/`error` counts; skipped
    parsed; `check` field reflects `fast` flag; failure lines extracted from output.
  - `check_mypy` (3 tests): PASS when clean; FAIL with `error_count`; check name.
  - `check_ruff` (3 tests): PASS by exit code 0; FAIL with issue extraction;
    indented context lines not counted.
  - `check_audit` (3 tests): PASS/FAIL by `failed` count; zero when no regex match.
  - `git_status` (3 tests): uncommitted file count; recent commits parsed;
    empty output → zeros.

### Metrics
- Tests: 738 (+22); fast gate collects all 738
- All gates green (mypy 18 files, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 31 — no_deploy test coverage; final annotation sweep

### Added
- **`tests/test_no_deploy.py`** (20 tests): first dedicated coverage for
  `scripts/no_deploy.py`. Covers all 5 public functions:
  - `is_locked` (2 tests): absent → False; present → True.
  - `set_lock` (4 tests): creates file; payload contains reason and timestamp; overwrites.
  - `clear_lock` (3 tests): removes file; no-op when absent; leaves nothing.
  - `get_lock_reason` (5 tests): None when absent; returns reason; includes timestamp;
    non-JSON plain-text fallback; empty JSON object handled.
  - `lock_required` (6 tests): allows when unlocked; sys.exit(1) when locked; both
    `@lock_required` and `@lock_required(path=...)` call forms; `functools.wraps`
    preserves `__name__`; positional args passed through.

### Improved
- **`live_trader.py`**: annotated the `load_dotenv` fallback stub (`-> None`).
- **`scripts/flatten.py`**: annotated the inner `on_notify` closure
  (`n: object, -> None`).
- **`scripts/no_deploy.py`**: annotated the inner `wrapper` closure (`-> object`).
  All three were the last remaining unannotated public-name functions across the
  18 mypy-covered files.

### Metrics
- Tests: 716 (+20); fast gate collects all 716
- All gates green (mypy 18 files, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 30 — test coverage for audit_daemon helpers

### Added
- **`tests/test_audit_checks.py`**: 12 new tests covering 4 previously-untested public
  functions in `scripts/audit_daemon.py`:
  - `check_log_file_sizes` (4 tests): missing LOG_DIR → INFO; empty dir → INFO;
    files under 200 MB → PASS; any file over 200 MB → WARN.
  - `log_failure` (2 tests): FAILURE line appended to fail file; `LOG_DIR` created
    when it doesn't exist.
  - `write_metric` (2 tests): INSERT INTO `quality_metrics` called with correct args;
    DB errors do not propagate.
  - `write_event` (2 tests): INSERT INTO `audit_log` called with correct args;
    DB errors do not propagate.

  All new tests use `monkeypatch.setattr(audit_daemon, "LOG_DIR", ...)` and
  `monkeypatch.setattr(audit_daemon, "FAIL_FILE", ...)` for file isolation, and
  `MagicMock()` connections for the DB-writing helpers.

### Metrics
- Tests: 696 (+10); fast gate collects all 696
- All gates green (mypy 18 files, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 29 — complete parameter annotation sweep; module-level pytest marks

### Improved
- **`live_trader.py`**: annotated two remaining unannotated parameters:
  `_handle_shutdown(frame: object)` (signal handler frame — widened to `object`
  per stdlib convention) and `_on_exit(exit_ts: datetime.datetime)`.
- **`scripts/audit_daemon.py`**: annotated `conn: Any` on 9 functions that previously
  had bare untyped `conn` parameters: `process()`, `check_data_freshness()`,
  `check_rejection_rate()`, `check_gap_count()`, `check_session_health()`,
  `check_pnl_sanity()`, `check_slippage_sanity()`, `check_trade_table_consistency()`,
  and `run_all_checks()`.
- **`migrate_parquet.py`**: annotated 5 remaining gaps — `_load_env() -> None`,
  `_connect() -> Any`, `_save_progress(p: dict) -> None`, `_load_file_fast(conn: Any)`,
  `_load_file(conn: Any)`.
- **`tests/test_features.py`**, **`tests/test_audit_data.py`**,
  **`tests/test_audit_system.py`**, **`tests/test_use_env.py`**: added module-level
  `pytestmark = pytest.mark.fast` to all four files that previously relied on
  per-function `@pytest.mark.fast` decorators. Module-level marks are idiomatic,
  prevent accidental unmarked tests when new functions are added, and are consistent
  with all other test files in the project.

### Metrics
- Tests: 686 (unchanged); fast gate still collects 686/695 correctly
- All gates green (mypy 18 files, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 28 — Optional[] modernization across all mypy-covered files

### Improved
- **`models.py`**: replaced 19 `Optional[X]` usages with modern `X | None` union
  syntax (PEP 604); removed `from typing import Optional` import entirely.
- **`live_trader.py`**: replaced 10 `Optional[X]` usages (function signatures and
  class attribute annotations) with `X | None`; removed `Optional` import.
- **`go_live.py`**: replaced 2 `Optional[X]` usages with `X | None`; removed
  `Optional` import.
- **`scripts/no_deploy.py`**: replaced 5 `Optional[X]` usages with `X | None`;
  removed `Optional` from the `from typing import ...` line.
- **`scripts/eod_summary.py`**: replaced 1 `Optional[float]` → `float | None`;
  removed `from typing import Optional`.

  All changed files already had `from __future__ import annotations`, so the
  `X | None` syntax is valid at runtime for all supported Python versions.

### Metrics
- Tests: 686 (unchanged)
- All gates green (mypy 18 files, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 27 — mypy 18 files, full return annotation sweep

### Improved
- **`scripts/hermes_session.py`**: added to mypy target list (17 → 18 files); the script
  already passes mypy cleanly — coverage was simply missing.
- **`scripts/contamination_audit.py`**: annotated all 15 previously-unannotated public
  functions — `check_no_negative_shift()`, `check_dedup_index_in_source()`,
  `check_validator_price_bounds()`, `check_sentinel_exists()`, `check_wal_crash_recovery()`,
  and all 9 DB-dependent `check_*` functions — as `-> list[dict]`; annotated `conn`
  parameters as `conn: Any`; annotated `main() -> None`.
- **`scripts/audit_daemon.py`**: annotated `write_metric() -> None` and
  `write_event() -> None` — the only two unannotated public functions in the file.
- **`scripts/no_deploy.py`**: added `# type: ignore[misc]` to the inner `wrapper()`
  closure (generic `*args/**kwargs` wrapper without concrete signature) so mypy
  reports it cleanly rather than requiring a concrete overload.

### Metrics
- Tests: 686 (unchanged)
- Mypy targets: 17 → 18 files
- All gates green (ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 26 — standards check tests, pipeline_run tests, future imports

### Added
- **`tests/test_pipeline_run.py`**: 31 new fast tests for `scripts/pipeline_run.py`
  — covering `_fmt_time()` (6 edge cases), `PipelineReport.total_s`, `run_stage()`
  (success / error / cache-hit / no-cache paths), `SessionMetrics.win_rate` and
  `avg_pnl` (zero-trade edge cases), `MLComparisonReport._agg()` (empty / single /
  multi-session), `run_ml_comparison()` mock mode, `_load_comparison_store()` (missing
  file, corrupt JSON, valid file), and the save/load round-trip.
  `pipeline_run.py` had zero prior test coverage.
- **`tests/test_standards_check.py`**: 39 new fast tests for
  `scripts/cpp_standards_check.py` and `scripts/python_standards_check.py`
  — covering `_resolve_scope()` (glob expansion, dedup), `_is_excluded()` (name
  patterns, build prefix, wildcards), `_scan_regex_absent()` (no violation, violation
  found, comment skipping, known_violations downgrade to WARN, invalid regex, excluded
  files), `_scan_regex_present()` (compliant, missing, invalid regex), and
  `run_rules()` dispatch for all check types plus unknown-check SKIP.
  Both scripts had zero prior test coverage.

### Improved
- **`scripts/cpp_standards_check.py`**: added `from __future__ import annotations`.
- **`scripts/python_standards_check.py`**: added `from __future__ import annotations`.
- **`scripts/formula_audit.py`**: added `from __future__ import annotations`.
- **`scripts/cross_system_audit.py`**: added `from __future__ import annotations`.
  All four scripts were the last production scripts in `scripts/` lacking PEP 563
  lazy-annotation support.

### Metrics
- Tests: 632 → 686 (+54)
- All gates green (mypy 17 files, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 25 — config_schema_audit tests, future import

### Added
- **`tests/test_config_schema_audit.py`**: 7 new fast tests for `run_audit()` and
  `_result()` in `scripts/config_schema_audit.py` — covering import failure (FAIL
  CRITICAL), missing config file (FAIL CRITICAL), valid config (PASS), schema
  violations (FAIL with parsed field errors), and the unparseable-error fallback.
  `config_schema_audit.py::run_audit()` had zero prior test coverage.

### Improved
- **`scripts/config_schema_audit.py`**: added `from __future__ import annotations`
  for consistency with all other scripts in the repo.

### Metrics
- Tests: 625 → 632
- All gates green (mypy 17 files, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 24 — mypy 17 files, migrate_parquet and flatten annotations

### Fixed
- **`migrate_parquet.py`**: fixed 3 mypy errors caused by the ternary assignment
  `progress = {...} if args.reset else _load_progress()` producing a union type
  that confused dict-access inference.  Added `from typing import Any`, annotated
  `_load_progress()` as `-> dict[str, Any]`, and explicitly typed the `progress`
  variable as `dict[str, Any]`.  Also annotated `main()` as `-> None`.

### Improved
- **`scripts/flatten.py`**: added `from __future__ import annotations`; annotated
  `parse_args(argv: list[str] | None = None) -> argparse.Namespace` and
  `async def run(stop_basket: str) -> None`.
- **`scripts/hermes_session.py`**: mypy target list expanded 16 → 17 files
  (added `migrate_parquet.py`).

### Metrics
- Tests: 625 (unchanged)
- Mypy targets: 16 → 17 files
- All gates green (ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 23 — mypy full-sweep 16 files, CLAUDE.md fix

### Improved
- **`scripts/hermes_session.py`** mypy target list expanded from 11 → 16 files:
  added `scripts/use_env.py`, `scripts/no_deploy.py`, `scripts/flatten.py`,
  `scripts/config_schema_audit.py`, `scripts/pipeline_run.py`.  All 16 pass
  `--ignore-missing-imports --disable-error-code=import-untyped` cleanly —
  mypy now covers every non-test Python script in the repo.

### Fixed
- **`CLAUDE.md`**: corrected audit daemon description from "22-check" to "23-check".

### Metrics
- Tests: 625 (unchanged)
- Mypy targets: 11 → 16 files (full script coverage)
- All gates green (ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 22 — mypy 11-file coverage, file-handle shadow fixes

### Fixed
- **`scripts/cpp_standards_check.py`** and **`scripts/python_standards_check.py`**:
  renamed `with open(RULES_PATH) as f:` to `as _fp:` (and updated the
  `yaml.safe_load` call accordingly).  The variable `f` was later reused as a
  loop variable over findings dicts; mypy was (correctly) flagging 26 errors
  from the type mismatch.  The file handle is now `_fp` in both scripts.

### Improved
- **`scripts/hermes_session.py`** mypy target list expanded from 9 → 11 files:
  added `scripts/python_standards_check.py` and `scripts/cpp_standards_check.py`.
- **`scripts/python_standards_check.py`** and **`scripts/cpp_standards_check.py`**:
  annotated `main()` with `-> None` return type.

### Metrics
- Tests: 625 (unchanged)
- Mypy targets: 9 → 11 files
- All gates green (ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 21 — mypy 9-file coverage, emergency_flatten tests, dead YAML removed

### Fixed
- **`scripts/cross_system_audit.py`**: removed dead YAML loading block (`_rules` was
  assigned but never referenced) and the unused `import yaml` / `RULES_PATH` that
  accompanied it.  This also fixed 12 mypy errors where the `f` loop variable was
  inferred as `TextIOWrapper` due to the earlier `with open(…) as f:` in the same
  scope.

### Improved
- **Mypy coverage**: expanded target list in `scripts/hermes_session.py` from 5 to
  9 files — added `scripts/eod_summary.py`, `scripts/formula_audit.py`,
  `scripts/cross_system_audit.py`, `scripts/contamination_audit.py`.  All 9 now
  pass `--ignore-missing-imports --disable-error-code=import-untyped`.
- **`scripts/formula_audit.py`** and **`scripts/cross_system_audit.py`**: annotated
  `main()` with `-> None` return type.
- **`tests/test_live_trader.py`**: 3 new fast tests for `_emergency_flatten()` failure
  paths — `conn=None` skips gracefully, DB error writes `AUDIT_HALT` sentinel,
  `WAITING` state skips trade-close without touching the cursor.

### Metrics
- Tests: 622 → 625
- Mypy targets: 5 → 9 files
- All gates green (ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 20 — ORB bar window constants, audit threshold constants

### Improved
- **`live_trader.py`**: extracted `_ORB_BARS_MAX = 120` and `_ORB_BARS_DISPLAY = 60`
  as named module-level constants.  The two bar-capping sites and the state-JSON
  display slice now reference these constants (no more bare `120`/`60` literals).
- **`scripts/audit_daemon.py`**: extracted three quality-threshold constants —
  `_REJECTION_RATE_WARN_PCT = 5.0`, `_GAP_COUNT_WARN = 50`,
  `_SLIPPAGE_WARN_TICKS = 6.0`.  The three check functions that compare against
  these values now reference the constants.

### Metrics
- Tests: 622 (unchanged — non-behavioral refactor)
- All gates green (mypy, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 19 — RTH constants, silent swallow fix, type hints

### Fixed
- **`migrate_parquet.py` silent swallow** (`_load_progress`, line 67): corrupt
  progress JSON now prints a WARN to stderr instead of silently resetting state.

### Improved
- **`scripts/audit_daemon.py`** — extracted `_RTH_START_UTC`, `_RTH_END_UTC`,
  `_WEEKEND_GRACE_S`, `_OFFHOURS_FRESHNESS_S` as named module-level constants.
  The two RTH comparison sites (`check_data_freshness`, `check_process_liveness`)
  now reference these constants instead of bare magic numbers.
- **`scripts/contamination_audit.py`** — added `from __future__ import annotations`
  and `from typing import Any`; annotated `_load_env() -> None`,
  `_pg_connect() -> Any`, `_pass(…) -> dict`, `_fail(…) -> dict`.

### Metrics
- Tests: 622 (unchanged — no new tests needed; changes are non-behavioral)
- All gates green (mypy, ruff, 23 audit checks)

---

## 2026-05-03 — Hermes iteration 18 — get_conn, build_from_trades, compute_live_features tests

### Added
- **`test_models.py`**: 5 new tests covering previously untested public functions:
  - `get_conn()` passes PG_* env vars to `psycopg2.connect`; falls back to
    localhost/5432/rithmic defaults when PG_* vars absent (patches `_load_env`).
  - `build_from_trades` edge cases: all-loss trades (`win_count=0`), positive
    `start_equity` produces correct `end_equity`, drawdown from non-zero baseline.
- **`test_live_trader.py`**: 4 new fast tests for `compute_live_features()`:
  - Returns a non-empty dict; `config=None` uses `orb_period=5`; config override
    applied; missing `orb` key falls back to default.

### Metrics
- Tests: 613 → 622 (fast gate: 597 → 606)
- All gates green (mypy, ruff, 23 audit checks)

---

## 2026-05-02 — Hermes iteration 17 — audit_log noise, fast test marks, log size check (f77380c)

### Fixed
- **`write_event()` log level** (`scripts/audit_daemon.py`): demoted from `WARN`
  to `DEBUG` when the `audit_log` table does not yet exist.  Previously every
  standalone Python audit run (C++ engine not started) emitted a spurious WARN;
  the message is now only visible at `--log-level DEBUG`.
- **`tests/test_models.py` — `pytest.mark.fast`**: added module-level
  `pytestmark = pytest.mark.fast`; 26 `unittest.TestCase` tests were previously
  invisible to the pre-commit gate (`make test-unit`) and are now included.
- **`tests/test_ui_kill.py` — `pytest.mark.fast`**: added module-level
  `pytestmark = pytest.mark.fast`; 10 Flask endpoint tests are now included in
  the pre-commit gate.

### Added
- **Audit check #23 — `check_log_file_sizes()`** (`scripts/audit_daemon.py`):
  scans all files matching `data/logs/*.log` and emits a `WARN` result for any
  file that exceeds 200 MB, catching runaway log growth before disk space is
  exhausted on the Oracle VM.

### Metrics
- Fast tests in pre-commit gate: +36 (26 from `test_models.py` + 10 from
  `test_ui_kill.py`)
- Audit checks: 22 → 23

---

## 2026-05-02 — Hermes iteration 16 — run_audit coverage, migrate_parquet tests, dev deps (a82fe68)

### Added
- **`TestRunAuditIntegration`** (`tests/`): verifies `run_audit()` orchestrates all
  6 check domains and returns a valid findings list whose entries contain the
  correct keys.
- **`tests/test_migrate_parquet.py` (11 fast tests)**: first direct test coverage
  for `scripts/migrate_parquet.py`, organised into three groups:
  - `_load_progress` (3): missing-file default, valid JSON round-trip, corrupt-file
    fallback to empty state.
  - `_save_progress` (3): JSON write correctness, automatic directory creation,
    tmp-file cleanup on success.
  - `_prep_df` (5): dtype coercion, column aliasing (`aggressor_side` → `side`),
    within-file deduplication, missing columns coerced to `None`, empty DataFrame
    passthrough, invalid `side` value handling.

### Fixed
- **`requirements-dev.txt`**: added `mypy>=1.0` and `ruff>=0.4`, which are invoked
  by the `make hermes` gates but were previously absent — fresh installs would
  silently fail those gates.

### Metrics
- Test count: 597 → 613

---

## 2026-05-02 — Hermes iteration 15 — _gate_db unit tests, cross_system_audit coverage (50285a0)

### Added
- **`TestGateDBUnit` (5 tests)** (`tests/`): covers `_check_db_connection` in
  `go_live.py` — env-var resolution, port fallback to `5432` when `PGPORT` is
  absent, missing required keys, psycopg2 connection error, and gate result
  wrapping (PASS / FAIL return shape).
- **`tests/test_cross_system_audit.py` (30 fast tests)**: first direct test
  coverage for all 7 check functions in `scripts/cross_system_audit.py` —
  `tick_value`, `point_value`, `symbol` consistency, Python defaults, micro-ORB
  point value, `trade_route`, and `risk_params_consistency`.  All tests are
  marked `@pytest.mark.fast` and run in under 2 s total.

### Metrics
- Test count: 562 → 597

---

## 2026-05-02 — Hermes iteration 14 — gate tests, eod_sync tests, CLAUDE.md fix (52813ad)

### Added
- **`TestGateAccountEquityUnit` (6 tests)**: unit tests for `_gate_account_equity`
  in `go_live.py` — equity above threshold (PASS), below threshold (FAIL), missing
  env var, DB unreachable, zero-equity edge case, and gate result shape.
- **`TestGateMlModelUnit` (5 tests)**: unit tests for `_gate_ml_model` in
  `go_live.py` — model file absent (FAIL), model fresh within 30 days (PASS),
  model stale beyond 30 days (FAIL), boundary at exactly 29 days 23 hours (PASS),
  and gate result wrapping.
- **`TestRunCppSync` (4 tests)** (`tests/test_eod_summary.py`): cover
  `run_cpp_sync` — script missing (WARN), exit 0 (PASS), nonzero exit (FAIL), and
  `--dry-run` flag forwarding.

### Changed
- **`go_live.py` — `_gate_ml_model`**: now enforces a 30-day staleness check on
  the ML model file (previously only checked for existence); stale models are
  blocked from live promotion.

### Fixed
- **`CLAUDE.md` — key files table**: "18-check quality daemon" corrected to
  "22-check quality daemon" to match current audit surface.

### Metrics
- Test count: 547 → 562

---

## 2026-05-02 — Hermes iteration 13 — complete audit check coverage, final cleanup (eec69e0)

### Added
- **`check_process_liveness` tests** (3): outside RTH window → INFO (no check
  performed), inside RTH with processes present → PASS, inside RTH with no
  processes → WARN.
- **`run_contamination_audit` tests** (4): script missing → WARN, script raises
  error → WARN, all checks pass → PASS, some checks fail → FAIL.

### Fixed
- **`requirements.txt`**: `psutil` entry moved from the `# Database` section to
  the correct `# System monitoring` section.

### Updated
- **`CHANGES.md`**: added changelog entries for Hermes iterations 10–12.

### Metrics
- Test count: 540 → 547

---

## 2026-05-02 — Hermes iteration 12 — full audit check test coverage 22/22 (f975c31)

### Added
- **28 new tests** covering 8 previously-untested audit check functions; all 22/22
  audit check functions in `scripts/audit_daemon.py` now have direct unit tests.
- **`check_zombie_trader` tests** (4): process-count variants — no processes, one
  process (healthy), two processes (FAIL), psutil unavailable (SKIP).
- **`check_hermes_session_freshness` tests** (4): weekend skip, today's session
  present (PASS), stale session (WARN), file absent (WARN).
- **`check_rejection_rate` tests** (3): below threshold (PASS), above threshold
  (WARN), no `live_trades` table (SKIP).
- **`check_gap_count` tests** (3): no gaps (PASS), gaps detected (WARN), no
  `ticks` table (SKIP).
- **`check_session_health` tests** (2): healthy session (PASS), unhealthy session
  (WARN).
- **`run_cpp_tests` tests** (3): binary missing (SKIP), tests pass (PASS), tests
  fail (FAIL).
- **`run_python_tests` tests** (3): pytest passes (PASS), pytest fails (FAIL),
  subprocess error (FAIL with message).
- **`run_type_check` / `run_lint_check` tests** (3 each): clean run (PASS),
  violations found (WARN), subprocess error (FAIL).

### Metrics
- Test count: 512 → 540

---

## 2026-05-02 — Hermes iteration 11 — audit_data tests, contamination logging (e2d7355)

### Added
- **22 new fast tests** for `scripts/audit_data.py`: cover `_load_env` (env
  present, env missing), `_pg_connstr` (full params, defaults), `check_schema`
  (tables present, table missing, DB error), `check_date_range` (in range, gap
  detected, no ticks), `check_tick_counts` (counts OK, low count, no ticks),
  `check_bars` (bars valid, bar anomaly), `check_side_parity` (balanced, imbalanced),
  and main CLI error handling (missing env, DB unreachable).

### Fixed
- **`scripts/contamination_audit.py` — 3 bare `except` swallows** in optional
  view queries: replaced with `except Exception` handlers that log at `DEBUG`
  level.  Runtime behaviour is unchanged (the views are best-effort); failures
  are now observable in the debug log stream rather than silently discarded.

### Metrics
- Test count: 490 → 512

---

## 2026-05-02 — Hermes iteration 10 — flatten + eod_summary test coverage, CHANGES update (81f7fd9)

### Added
- **16 new tests for `scripts/flatten.py`** — a critical emergency-flatten CLI
  that previously had zero test coverage.  Tests span import safety (no
  side-effects on import), argparse validation (missing args, bad symbol, bad
  direction, `--help`), coroutine structure (`flatten_position` is a coroutine),
  and end-to-end behaviour (dry-run path, AUDIT_HALT sentinel check).
- **28 new tests for `scripts/eod_summary.py`**: `_compute_max_drawdown` (7
  cases — empty, single, flat, rise-only, drawdown, multiple drawdowns, partial
  recovery), `write_eod_summary` (10 cases — normal write, DB error, zero trades,
  missing columns, date filter, duplicate key, crash-safe path, field types),
  `main` CLI (6 cases — no args, date flag, env missing, DB unreachable, output
  path), import safety (5 cases).

### Updated
- **`CHANGES.md`**: added changelog entries for Hermes iterations 7–9.

### Metrics
- Test count: 446 → 490

---

## 2026-05-02 — Hermes iteration 9 — zombie check, session freshness, formula tests (01bf0d7)

### Added
- **`check_zombie_trader()` audit check** (`audit_daemon.py`): FAILs if more than
  one `live_trader.py` process is running simultaneously, preventing the
  double-trading risk that arises when a prior instance is not fully shut down
  before the next one starts.
- **`check_hermes_session_freshness()` audit check** (`audit_daemon.py`): emits a
  WARN on weekdays if no Hermes session has been recorded for today, ensuring the
  quality loop is run on every trading day.
- **42 new fast tests** (`tests/`): five test classes covering all audit functions
  in `scripts/formula_audit.py`; tests are marked `@pytest.mark.fast` and run in
  under 2 s total.

### Fixed
- **`audit_daemon.py` — docstring**: updated check count from "16 checks" to
  "22 checks" to reflect the current audit surface.
- **`_emergency_flatten` — silent PID-unlink swallow** (`live_trader.py`): the
  last remaining silent `except` in the emergency-flatten path now logs at
  `DEBUG` level instead of discarding the error, making transient filesystem
  issues observable.

### Metrics
- Test count: 404 → 446

---

## 2026-05-02 — Hermes iteration 8 — gate unit tests, anyio dep (f746375)

### Added
- **`anyio>=4.0`** to `requirements-dev.txt`: the package was referenced by the
  async test marker but was not listed as a dev dependency, causing environment
  setup failures on clean installs.
- **14 new preflight unit tests** (`tests/`): cover `_gate_ssl_cert` (4 cases),
  `_gate_drift_halt` (4 cases), and `_gate_prop_firm` (6 cases), closing a gap
  in gate coverage that left three preflight checks untested.

### Metrics
- Test count: 390 → 404

---

## 2026-05-02 — Hermes iteration 7 — config schema, gate ordering, dep fix, audit logging (255b1a2)

### Fixed
- **`LiveConfig` schema** (`config/live_config_schema.py`): `commission_rt`,
  `tick_value`, and `starting_balance` added as required fields; the
  `tick_value` validator now rejects the NQ value (`5.0`), catching an
  instrument-mismatch that would silently corrupt P&L on MNQ.
- **`go_live.py` — `_ALL_GATES` ordering**: all instant file/dict gates are now
  executed first; the DB-connection gate (≤10 s) and ML-hash gate are moved to
  the end of the sequence, reducing average preflight time on the happy path.
- **`requirements.txt`** — `psutil>=5.9` added: the package was used by multiple
  modules (process checks, memory gates) but was absent from the production
  dependency list.
- **`audit_daemon.py` — 2 remaining bare exception swallows**: `check_drift_halt`
  and the PostgreSQL reconnect path now log at `WARN` level instead of silently
  discarding the exception, making connectivity and drift-halt errors observable
  in the audit log.

### Updated
- **`CHANGES.md`**: changelog entries for Hermes iterations 4–6 added.

---

## 2026-05-02 — Hermes iteration 6 — type annotations, temp cleanup logging (dde80cf)

### Added
- **`live_trader.py` — return type annotations**: `_pg_connect`, `_pg_connect_with_retry`,
  and `_make_position_from_db` now carry explicit return type annotations.
- **`live_trader.py` — `conn` parameter annotations**: 9 private methods that accept a
  database connection now declare `conn: psycopg2.extensions.connection` in their
  signatures, eliminating implicit `Any` types flagged by mypy.
- **`audit_daemon.py` — return type annotations**: 8 functions/methods annotated with
  explicit return types, bringing mypy coverage in line with `live_trader.py`.

### Fixed
- **`_promote_config` — silent OSError swallow**: temp-file cleanup failure is now logged
  at `DEBUG` level instead of being silently discarded, making transient filesystem errors
  observable without cluttering normal output.

---

## 2026-05-02 — Hermes iteration 5 — silent swallows, KeyError guards, test coverage (957532f)

### Fixed
- **5 silent exception swallows**: bare `except`/`pass` blocks replaced with explicit
  handlers that log at `WARNING`, `DEBUG`, or `ERROR` level as appropriate, ensuring
  all suppressed exceptions are now observable in logs.
- **`config.get("orb", {})` guard** (`live_trader.py`, 2 sites): direct `config["orb"]`
  key access replaced with `.get("orb", {})` to prevent `KeyError` when the `orb` section
  is absent from the config.
- **Zero-price close warning** (2 sites): an `ERROR` log is now emitted when a trade close
  is attempted at `price=0.0` with no tick available, converting a silent data-integrity
  issue into an observable fault.
- **`audit_daemon.py` — stdlib UTC**: `pytz` dependency removed; all UTC references now
  use stdlib `datetime.timezone.utc`, eliminating a soft dependency.
- **`_load_live_config`**: logs `WARNING` on failure instead of silently returning
  a default/empty config.
- **`_emergency_flatten`**: `commission_rt` is now passed consistently, preventing a
  `NameError` / wrong-value path that could corrupt P&L on forced flattens.

### Added
- **11 new tests**: `_gate_trade_route` (×4), `_gate_audit_daemon` (×3),
  `Trade.for_date` (×2), `Trade.get` (×2) — covering previously untested public
  interfaces.

### Metrics
- Test count: 379 → 390

---

## 2026-05-02 — Hermes iteration 4 — deploy target, flatten argparse, CHANGES.md update (3fbd11c)

### Added
- **`make deploy` target** (`Makefile`): runs the `hermes-fast` gate, pushes to
  `origin main`, SSH-es to the Oracle VM, runs `git pull`, and conditionally restarts
  the `live_trader` systemd service — full one-command deploy pipeline.
- **`make deploy-dry` target** (`Makefile`): prints every step of the deploy sequence
  without connecting to the remote, enabling safe rehearsal of the deploy path.
- **`CHANGES.md`**: changelog entries documenting all Hermes iterations 1–3 added
  (this file).

### Changed
- **`scripts/flatten.py` — argparse refactor**: replaced raw `sys.argv` indexing with
  `argparse`; the script now provides `--help`, validates arguments, and produces clear
  usage errors on bad input.

---

## 2026-05-02 — Hermes iteration 3 — reconcile_position, help text, session logging, model tests (ecef05f)

### Fixed
- **`_reconcile_position()`** (`live_trader.py`): replaced the stub implementation
  with a real query against the `live_trades` DB table; on startup the method
  finds any open trade for today's session date and returns it so the engine
  resumes the correct position state without manual intervention.
- **`start()` — active trade ID restore**: `_active_trade_id` is now set from
  the reconciled open trade returned by `_reconcile_position()`, eliminating a
  class of bugs where a restarted process treated an existing open position as
  flat.

### Added
- **`strategy/micro_orb.py` — `restore_position()`**: new state-hook called by
  `live_trader` during startup reconciliation; allows the strategy object to
  re-synchronise its internal state from a persisted trade record without
  altering any signal logic.
- **4 reconciliation tests** (`tests/`): cover the full startup-reconciliation
  path — open trade found, no open trade, malformed row, and DB error handling.
- **4 `models.py` tests** (`tests/`): cover `write_crash_safe` round-trip,
  `for_date` query, `_Date` type alias, and `SessionSummary` field validation.
- **`hermes_session.py` — per-iteration diff block**: each Hermes run now
  appends a `git diff HEAD~1..HEAD` block to `data/logs/hermes_session.log` so
  the exact changes for every iteration are preserved in the session history.
- **`--help` text** across 11 scripts (`go_live.py`, `live_trader.py`,
  `audit_daemon.py`, `use_env.py`, `hermes_session.py`, and six supporting
  scripts): all entry points now expose a consistent `--help` interface
  describing flags, environment variables, and exit codes.

### Metrics
- Test count: 371 → 379

---

## 2026-05-02 — Hermes iteration 2 — audit hardening, integration tests, architecture doc (46941c3)

### Fixed
- **`audit_daemon.py` — ctest handling**: `run_ctest_check` now distinguishes
  between a missing test binary (result: `SKIP`) and an actual test failure
  (result: `FAIL`); previously both cases were reported as `FAIL`, masking
  environment issues.
- **`audit_daemon.py` — silent except blocks**: three bare `except: pass` blocks
  replaced with `except Exception` handlers that log at `WARN` level, ensuring
  errors in `check_pnl_sanity`, `check_open_position`, and
  `check_trade_table_consistency` are always observable.

### Added
- **5 integration tests** (`tests/test_integration.py`): cover the full
  tick → signal → DB write path for both LONG and SHORT entries; each test
  spins up an in-process `live_trader` instance against a test database, feeds
  synthetic bars, and asserts that a `live_trades` row is written with the
  correct direction, entry price, and trade ID.
- **`engine_architecture.html` v4**: updated architecture diagram now includes
  the Hermes improvement loop, end-of-day deploy pipeline, all 14 preflight
  gates in `go_live.py`, and all 20 audit daemon checks.

### Metrics
- Test count: 366 → 371

---

## 2026-05-02 — Hermes iteration 1 — crash safety, gate hardening, test coverage (5bbbf81)

### Fixed
- **`live_trader.py` — crash-safe session write**: `start()` now wraps the main
  run loop in a `try/finally` block that guarantees `_write_session_summary()`
  is called even if an unhandled exception escapes the loop.  A
  `_session_summary_written` boolean flag prevents the summary from being
  written twice when the `finally` block runs after a clean shutdown path that
  already called the method.

### Added
- **`go_live.py` — Gate: RAM check (`_gate_ram`)**: preflight now asserts that
  at least 2 GiB of free RAM is available before promoting to live; exits with
  a clear error message if the threshold is not met.
- **`go_live.py` — Gate: C++ build check (`_gate_cpp_build`)**: preflight
  verifies that the C++ executor binary exists and is executable; blocks
  promotion if the build artefact is absent or stale.
- **13 new unit tests**: cover `_write_trade_close` (commission deduction, zero
  qty guard, DB error path), `_write_session_summary` (normal write, crash-safe
  finally path, duplicate-write guard via `_session_summary_written`), and
  `_cancel_trade_open` (success, already-cancelled, network error).
- **`CHANGES.md`**: added this changelog with full project history from initial
  commit through the Hermes loop foundation (see commit `4d8ef9a`).
- **`.gitignore`**: added `NO_DEPLOY_DOES_NOT_EXIST_TEST` to prevent the test
  artefact created by `tests/test_go_live.py` from appearing as an untracked
  file in `git status`.

### Metrics
- Test count: 290 → 366

---

## 2026-05-02 — Hermes quality loop + audit checks #17/#18

### Added
- **Hermes improvement loop** (`scripts/hermes_session.py`): runs pytest, mypy,
  ruff, and the audit daemon in sequence; writes `data/hermes_findings.json`
  for the agent to act on each iteration.
- **Makefile targets**: `make hermes` (full check), `make hermes-fast` (tests +
  mypy + ruff, no slow audit), `make push-eod` (gates must be green before
  pushing to `origin main`).
- **CLAUDE.md**: documents the Hermes agent role, what-to-improve priority
  order, file boundaries (strategy/ is off-limits), and Oracle deployment
  notes.
- **Audit check #17** (`run_type_check`): mypy is run on key source files every
  audit cycle; any type error raises a WARN in the audit daemon.
- **Audit check #18** (`run_lint_check`): ruff (F, E7, E9, W6 rules) is run on
  sources every cycle; violations raise a WARN.
- **Test suite expansion**: 335+ tests passing; new test modules
  `test_live_trader.py` (EOD-flatten, reconnect-limit), `test_use_env.py`
  (21 tests for `_parse_env`, `_write_env_updates`, `cmd_switch`).
- **Audit checks**: `check_trade_table_consistency` (duplicate open-position
  detection) and `check_config_schema` (Pydantic validation every cycle).
- **Feature tests**: `tests/test_features.py` — 69 unit tests covering all 74
  ORB/ML features produced by `strategy/features.py`.
- **deploy/live_trader.service**: systemd unit for `live_trader.py`; blocks
  start on `NO_DEPLOY` or `AUDIT_HALT` sentinel; `Restart=no` (manual review
  required after crash); 30 s graceful shutdown for EOD flatten.

### Fixed
- **`models.py`**: introduced `_Date` type alias so `SessionSummary.date` no
  longer shadows the built-in `date` name; fixed `write_crash_safe` and
  `for_date` type annotations to pass mypy.
- **`live_trader.py` — type safety**: `conn` typed as
  `psycopg2.extensions.connection`; `current_position()` None-return guarded;
  `session_date` asserted non-None before DB writes; `rollback` guarded on
  None connection.
- **`live_trader.py` — silent failures**: `_update_trade_order_id` now logs a
  WARNING instead of swallowing the error silently; `_reconcile_position` logs
  unexpected errors (non-`UndefinedTable`).
- **`live_trader.py` — emergency flatten**: writes `AUDIT_HALT` sentinel and
  fires a Slack alert when `emergency_flatten` fails.
- **`live_trader.py` — reconnect limit**: `_bar_loop` halts and writes
  `NO_DEPLOY` after 10 consecutive reconnect failures.
- **`live_trader.py` — alert delivery**: failures from `_send_alert()` are now
  logged at WARNING rather than swallowed.
- **`audit_daemon.py` — Python 3.9 compatibility**: added
  `from __future__ import annotations` to resolve forward-reference errors.
- **`audit_daemon.py` — missing tables**: `check_pnl_sanity` and
  `check_open_position` now handle absent `ticks`/`live_trades` tables
  gracefully.
- **`audit_daemon.py` — escalation constants check**: extended to also validate
  `sl_points`, `trail_step`, and `qty` against prop-firm limits.
- **`audit_daemon.py` — alert delivery**: failures are now logged at WARNING
  instead of being silently swallowed.
- **`go_live.py` — Gate L**: now accepts a locally running `live_trader`
  process in addition to the systemd service; removed erroneous
  `trade_route=simulator` block from the preflight gate.
- **C++ tests (`test_orb_strategy.cpp`)**: updated to reflect `in_position`
  semantics (see 2026-05-01 below); tests now pass with the new state-machine
  API.

### Infrastructure
- `data/alerts/` directory created by `audit_daemon` on startup.
- `requirements.txt` updated to pin production dependencies.

---

## 2026-05-01 — MD provider selection + strategy and recon fixes

### Added
- **Dynamic `RITHMIC_MD_PROVIDER` selection** (`live_trader.py`): market-data
  provider is now chosen at runtime from the `RITHMIC_MD_PROVIDER` environment
  variable, supporting `legends`, `tradeify`, and `amp` without code changes.

### Fixed
- **`fix(strategy)` — `in_position` flag** (`src/execution/orb_strategy.hpp`,
  tests): replaced the two separate `long_taken` / `short_taken` boolean fields
  in `OrbSession` with a single `in_position` flag. Eliminates a class of
  state-machine bugs where one flag could be set while the other was stale.
- **`fix(recon)` — startup reconciliation** (`live_trader.py`):
  - Auto-cancels any residual open orders detected in the broker on startup,
    preventing ghost orders from prior sessions.
  - Stops the process (exits cleanly) when the market-data gateway returns an
    auth rejection, rather than spinning in an infinite reconnect loop.
- **`fix(client)` — `LoginError` on auth rejection**: the Rithmic client now
  raises `LoginError` immediately on auth rejection to break the retry loop
  instead of retrying indefinitely.

### Chore
- `.gitignore`: added `*.wal` and `*.docx` patterns.
- Untracked `__pycache__` directories removed from git history (already covered
  by `.gitignore`).

---

## 2026-05-02 (early) — Systemd control plane, order-flow, Slack alerts

### Added
- **Slack alert wiring** (`live_trader.py`): `_send_alert()` is now called on
  entry, exit, flatten, and prop-firm gate failures (non-dry-run only).
- **UI endpoints** (`ui/routers/live.py`): `/api/live/state` and
  `/api/live/orb` added for frontend consumption.
- **`scripts/test_live_cycle.py`**: integration-style test script for the full
  live trading cycle.

### Fixed
- **Phantom trade on restart** (`live_trader.py`): `_replay_historical_bars`
  no longer calls `_on_signal`; state-machine is updated from historical bars
  without submitting any orders.
- **`_write_state` tick poll**: only polls the latest tick when
  `current_position` is not None.
- **`compute_live_features`**: now passes `orb_period_minutes` from config
  (was hard-coded to 5 while config specified 15).
- **`strategy/features.py` — MACD history loop**: rewritten from O(N²) nested
  loop to O(N) incremental EMA pass.
- **`strategy/features.py` — `prev_day_*` features**: explicitly zeroed with
  documentation; were previously returning silent wrong data.
- **Architecture diagram** (`engine_architecture.html`): updated for systemd,
  order-flow, and alert paths (v2).

---

## 2026-05-02 (audit) — 24/7 audit daemon with escalation engine

### Added
- **Escalation engine** (`scripts/audit_daemon.py`): enforces
  `quality_rules/escalation.yaml` at runtime — three WARNs within 60 min
  escalate to ERROR; 30 min unresolved escalates to CRITICAL; two clean passes
  auto-resolve. State persists to `data/escalation_state.json` (atomic write)
  so restarts never amnesty open incidents.
- **`check_trading_constants`**: validates `point_value`, `tick_value`, symbol,
  and `commission_rt` against MNQ spec every cycle; a mismatch writes the
  `data/AUDIT_HALT` sentinel.
- **`check_pnl_sanity`**: flags any `|pnl_usd| > $500` in `live_trades` over
  the last 24 h.
- **`audit_daemon.service`** (`deploy/`): `Restart=always`, `EnvironmentFile`,
  journald logging.
- **Gate L** (`go_live.py`): preflight check that the audit daemon is active
  and `data/AUDIT_HALT` is absent before promoting to live.
- **`_check_audit_halt`** (`live_trader.py`): startup gate — refuses to start
  if `AUDIT_HALT` sentinel is present.
- **46+ unit tests** (`tests/test_audit_checks.py`): cover all check functions
  and all `EscalationEngine` branches (WARN accumulation, INFO exemption,
  native-critical gate, state-persistence round-trip, auto-resolve,
  clean-counter reset).

### Fixed
- Table name `trades` corrected to `live_trades` throughout audit daemon.
- Three false-positive escalations corrected.
- `pytest` subprocess no longer triggers disk writes from the log handler inside
  audit subprocess context.

---

## 2026-04-30 — C++ quality, security hardening, reliability

### Added
- **C++ unit tests** (`tests/test_orb_strategy.cpp`): `RiskManager` and
  `OrbStrategy` covered; integrated with CMake/ctest.
- **AuditLog integration** (`src/executor_main.cpp`): C++ executor now writes
  structured entries to the audit log on entry, exit, and risk events
  (H-AUD-1, H-AUD-2).
- **`check_trade_table_consistency`** (`audit_daemon`): detects duplicate open
  positions in `live_trades`.
- **`check_config_schema`** (`audit_daemon`): runs Pydantic validation of
  `live_config.json` every cycle.
- **`use_env.py`** environment switcher: `_parse_env`, `_write_env_updates`,
  `_discover_envs`, `cmd_switch` for switching between `legends`, `tradeify`,
  and `amp` credential sets without editing `.env` manually.

### Fixed
- **`_submit_order`** (`live_trader.py`): `NotImplementedError` replaced with
  `CRITICAL` log + `sys.exit(1)` — unimplemented paths can no longer silently
  pass (C7).
- **`_reconcile_position`** (`live_trader.py`): now queries the `live_trades`
  C++ table for open positions on startup instead of relying on Python-only
  state (H-PY-1).
- **Trade record ordering** (`live_trader.py`): DB record is written before
  order submission, eliminating orphaned positions if the process crashes
  immediately after submitting (H-PY-2).
- **`commission_rt`**: moved from hard-coded constant to `live_config.json`
  parameter (H-PY-3).
- **P&L sanity check** (`live_trader.py`): warns before DB write if
  `|pnl_usd| > $5000` (H-PY-4).
- **`config/live_config_schema.py`**: `MES`, `MYM`, `M2K` added to valid
  symbol set (N-5).
- **`_load_config`**: `ImportError` and validation error now produce distinct
  log messages and exit codes (N-3).
- **Trailing-DD spike** (`RiskManager`): equity snapshot is now taken
  atomically, eliminating a race that caused spurious trailing-drawdown
  breaches.
- **SIGTERM flatten** (`executor_main.cpp`): `asio::signal_set` registered for
  `SIGINT`/`SIGTERM`; position is flattened immediately on signal (H-REL-2).
- **15 s login timeout** (`sdk/`): MD and `ORDER_PLANT` login loops now time
  out after 15 seconds rather than blocking indefinitely (H-REL-1).
- **Concurrent latency tracking**: replaced single `pending_` scalar with a
  map to support simultaneous entry + stop latency measurement (H-REL-5).
- **Security — SQL injection** (`orb_db.hpp`): all SQL in `flush()` now uses
  parameterized queries; `libpq` keyword=value connection strings quote values
  to prevent injection (H-SEC-3).
- **Security — credential leakage**: Rithmic username masked in login log
  lines; DB password redacted from `--status` output; hardcoded
  `NQ_FIRE_TEST_ORDER` live-order hook removed (H-SEC-4, H-SEC-5).
- **`news_blackout_min` default**: raised from 2 to 5 minutes per industry
  practice (N-4).
- **Exit order type**: exit orders now use LIMIT instead of MARKET (Legends
  rejects MARKET on exits); stale-stop unwind also uses LIMIT.
- **Partial-fill guard** (`order_manager`): fill notification handler now
  validates fill quantity before updating position.
- **Audit buffer cap** (`audit_daemon`): re-queued batch is capped at
  `MAX_BUF` to enforce the buffer invariant (N-2).
- **`tick_value`** added to `live_config.json`: `0.50` for MNQ
  (0.25 pts × $2/pt), preventing an off-by-two P&L calculation.

---

## 2026-04-29 — Rithmic R|API+ SDK, environment switcher, order routing

### Added
- **Native Rithmic R|API+ SDK v13.7.0.0** (`sdk/`): TCP market-data path
  integrated; `ALERT_FORCED_LOGOUT` handled in the alert callback.
- **`use_env.py`** initial version: `.env` restructured with named env-sets;
  `config/envs/` overrides directory for per-environment config overlays.
- **Paper env override**: `--paper` flag applies paper-trading config to all
  relevant files; `account_id`/`fcm_id`/`ib_id` cleared when switching to
  paper/test.
- **`update.sh`** (`deploy/`): one-command Oracle redeploy handling `git stash`
  and untracked-file conflicts; includes SELinux `chcon` step.

### Fixed
- **Order routing** (`executor_main.cpp`): trade-route discovery made async
  with a 5 s timeout; template ID comments corrected (312 = `RequestNewOrder`,
  314 = `RequestModifyOrder`).
- **EOD cancel race** (`order_manager`): handles the case where a cancel and
  fill arrive simultaneously at EOD without leaving a stuck `PENDING_EXIT`
  state.
- **Exit orders** (`order_manager`): exit rejection retries capped at 3;
  entries halted after cap is hit.
- **Stop unwind** (`order_manager`): `last_stop_for_unwind_` cleared when
  position goes FLAT.
- **DB null-result guard**: `PQexecParams` result is now checked for null;
  throws instead of silently returning.
- **`status_log` coroutine** (`collector`): converted to a proper coroutine to
  prevent dangling reference.
- **`is_news_blackout()`** (`live_trader`): implemented with CPI/FOMC/ISM
  schedule (was always-false stub).
- **Equity seeding** (`RiskManager`): `peak_equity` initialised from
  `starting_balance` config value, not hard-coded `50000`; historical P&L is
  loaded on startup.
- **SQL injection in sentinels**: `write_sentinel_alerts` now uses
  `PQexecParams`.
- **`_bar_loop` import**: deferred strategy imports moved to module top-level
  to avoid import latency on first bar.
- **Security**: hardcoded credentials removed from docstrings.

---

## 2026-04-28 — MNQ migration, multi-instrument, initial audit system

### Added
- **Multi-instrument tagging**: `instrument` + `strategy` columns added to
  `live_trades`/`live_position`; per-instrument config files supported.
- **Schema rename**: `nq_*` tables renamed to `live_*` prefix
  (`live_trades`, `live_position`, `live_session_summary`).
- **NQ 15-min ORB executor** (`src/execution/`): full Legends order plant
  integration, AMP MD feed, dry-run simulation, and order fill detection.
- **Initial audit system** (`scripts/formula_audit.py`,
  `scripts/cross_system_audit.py`, `scripts/python_standards_check.py`,
  `scripts/cpp_standards_check.py`): `make audit` and `make quality-gate`
  targets.
- **`quality_rules/`**: YAML definitions for MNQ contract constants, config
  invariants, and escalation thresholds.
- **`nq_executor.service`** / **`nq_executor@.service`**: systemd template
  units for the C++ executor on Oracle.
- **`legends_price` column** (migration): added to `live_position` for
  Legends-specific fill price tracking.

### Fixed
- **MNQ constants**: `point_value` corrected to `2.0`; `tick_value` to `0.25`;
  `commission_rt` to `0.50/side`; symbol set to `MNQ`.
- **`sl_points` / `stop_loss_ticks` mismatch**: aligned to prevent 3.75×
  error in stop distance.
- **`_poll_latest_bar`** SQL fixed; commission correctly deducted in
  `_write_trade_close`.
- **Deploy path**: all Python files purged from the Oracle push path
  (C++ executor only on Oracle).
- **`live_trader.py`**: Pydantic config validation added at startup.
- **`go_live.py` — Gate K**: blocks `trade_route=simulator` from live
  promotion.

---

## 2026-04-23 — Live trader foundation, UI dashboard, preflight gates

### Added
- **`live_trader.py`**: Python ORB trading loop with `NO_DEPLOY` gate,
  position reconciliation on startup, SIGTERM handler for clean shutdown,
  EOD flatten, and configurable reconnect logic.
- **`go_live.py`**: formal paper→live promotion script with multi-gate
  preflight (gates A–L including model checksum, equity check, audit daemon).
- **`strategy/features.py`**: `compute_features()` producing all 74 ORB/ML
  features used for signal generation.
- **`backtest.py`**: re-exports `compute_features` for backtesting use.
- **`MicroORBStrategy`** (`strategy/`): state-machine strategy with ORB logic.
- **`models.py`**: `Trade` and `SessionSummary` dataclasses with PostgreSQL
  read/write helpers; `write_crash_safe` for atomic EOD writes.
- **`migrations/001_trades.sql`**: unified `live_trades` + `session_summary`
  schema.
- **`migrations/002_*`**: reconciles `live_trader`/`models.py` schema
  divergence; adds `crash_exit` column.
- **Flask UI dashboard** (`ui/`): position panel, kill switch
  (`/api/live/kill` restricted to localhost), reconnect status, real-time
  chart via `NOTIFY live_tick`.
- **`no_deploy.py`**: `NO_DEPLOY` lockfile management.
- **C++ ORB parity binary** (`build/orb_strategy`): compiled from
  `src/execution/orb_strategy.hpp` for Python/C++ signal parity tests.
- **Pytest markers**: `@pytest.mark.fast`, `@pytest.mark.slow`,
  `@pytest.mark.live`, `@pytest.mark.feature_parity` — wired into
  `pytest.ini` with `make test-unit` / `make test-fast` targets.
- **`kill_test_suite.py`** + `model_checksums.json` for CI infrastructure.
- **1 s position flush** + `NOTIFY live_tick` in C++ executor for chart
  streaming.

### Fixed
- `live_trader.py`: `daily_pnl` accumulated in `_on_exit`;
  `_write_trade_close` returns float.
- Dashboard SQL queries updated to canonical column names.
- `bar` dict accepted with both `'ts'` and `'timestamp'` keys in features.
- Duplicate DDL removed from `live_trader.py` (now lives in `models.py` only).

---

## 2026-04-14 — Lifecycle layer, schema alignment

### Added
- Full lifecycle layer: `DataSentinel`, session tracking, contamination audit.
- PG schema aligned with bot's SQLite: missing columns and tables added.

### Fixed
- `parallel_group` heartbeat replaced with Beast timeout to prevent 60 s
  disconnect loop.
- Migration index creation moved after `ALTER TABLE` to fix re-run on existing
  DBs.

---

## 2026-04-09 to 2026-04-12 — C++ engine rewrite

### Added
- **C++ engine** (`src/`): full rewrite with PostgreSQL + TimescaleDB storage;
  WAL crash recovery, tick validation, non-blocking flush.
- BBO/depth support, async audit logging, `test_connection` binary, ncurses
  live dashboard.
- Oracle Linux 9 deploy script (`build.sh`) with Boost 1.83 from source.
- `TCP_NODELAY`, flush-threshold tuning, process priority for sub-100 ms
  tick-to-PG latency.

### Fixed
- Timestamp drift threshold widened to 48 h to cover Fri→Sun weekend gap.
- `pg_class` estimate used for tick count (avoids 270 M-row full scan).
- `ensure_schema` no longer drops `idx_ticks_unique` on every startup.
- One-sided BBO updates accepted; dedup unique index fixed.
- Rithmic `RequestHeartbeat` (template 18) now acknowledged to prevent 60 s
  disconnect loop.

---

## 2026-04-07 — Initial commit

- Rithmic AMP 24/7 tick engine with DuckDB + Cloudflare R2 storage.
