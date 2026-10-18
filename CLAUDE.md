# rithmic_engine — Claude Code Instructions

## Role
You are the Hermes agent for this project. Your job is to **fix and improve** the codebase — not to touch strategy logic. Every session follows the loop below.

## The Loop

```
make hermes          ← check current state (C++ build + tests + audit_daemon)
  → PASS: find improvements (see "What to improve")
  → FAIL: fix failures first, then add/update tests
make hermes          ← verify your changes
repeat
make push-eod        ← end of day, only when fully green
```

## Fleet Mode (multi-agent)

```
make hermes-fleet    ← launches 4 specialist agents in parallel, coordinator synthesizes + fixes
```

| Agent | Scope |
|---|---|
| `regression-watcher` | Compares current run vs last 7 Obsidian hermes notes — flags new/recurring failures |
| `code-scanner` | Scans C++ files changed in last 5 commits for quality issues and coverage gaps |
| `audit-analyst` | Deep-reads all 17 audit checks, flags thresholds approaching limits |
| `test-sentinel` | Verifies test coverage for recently changed execution layer code |
| `doc-syncer` | Audits CLAUDE.md, CHANGES.md, DATA.md, RUNBOOK.md, Obsidian decisions — fixes stale content and adds missing entries |

After agents report: coordinator fixes CRITICAL/HIGH issues, re-runs hermes, writes fleet note to Obsidian.
Agent prompts live in `scripts/fleet_agents.json`.

## StarNet strategy desk (since 2026-09-30)

A StarNet crew (`~/starnet`, :8787, user unit `starnet`, via `starnet-adapter` :8099 → claude-bridge) researches
this repo from a **read-only clone** and never touches live trading. **Budgeted to the WEEKLY Claude limit**
(2026-10-01, founder: mix models + slower — every 2 h at 50 calls/h on Opus had spent the whole week in ~21 h):
routine agents on **Sonnet 5**, judgment on **Opus 5.5**, a cycle every **4 hours**, adapter caps 40 calls/h and
250/day. Times are Cyprus:

| When | Who (model) | What |
|---|---|---|
| 00/04/08/12/16/20 :00 | `rithmic-research-sync.timer` → `scripts/starnet/desk_cycle.sh` | **since 2026-10-16** `desk_edge.sh` judges every new `*-RESEARCHER*.md` (one ```json `{"ideas":[…]}` block of edge_lab specs, ids forced to new `desk_*`, ≤40 per run) through `edge_lab` (copied binary from `build-exp/`) on 1m NQ/ES **2010-07 → 2024-12 only** → `…-EDGELAB.md` + `research_data/edge_lab/` (README, registry = the multiple-testing count, report cards); an in-sample PASS gets one look at the **sealed holdout 2025-01 → today** (≥30 trades, net > 0) + meta/exits → `~/starnet-work/holdout/…-EDGE-HOLDOUT.md`. Then syncs the clone and exports `research_data/`. Nothing touches Postgres, the fleet or `data/edge_lab/`. (`desk_replay.sh`, the old paper-variant replay, is no longer called.) |
| :27 | SKEPTIC (Opus) | `…-SKEPTIC.md` — attacks every idea at stage ≥ 3: look-ahead, selection/re-rolls, era concentration, the known volatility bet again → ROBUST / FRAGILE / NOISE |
| :40 | SABLE steer (Opus) | `…-STEER.md` — goal status, `## Matrix coverage`, `## Stop`, `## Research next` for RESEARCHER / REGIMES / FLOWLAB / FLOWS |
| :48 :54 | two researchers per cycle (Sonnet): RESEARCHER (families × session windows) + REGIMES (families × regime filters) at 00/08/16, FLOWLAB (order flow inside the 5m candle, 2023-03+) + FLOWS (forced/scheduled flows) at 04/12/20 | `…-RESEARCHER-<LANE>.md` — ≤10 edge_lab ideas each, judged at the next cycle |
| 07:30 15:30 | BRIEF (Opus) | `…-BRIEF.md` — the ONLY reader of `~/starnet-work/holdout/`; recommends a paper-fleet add only with in-sample PASS + holdout PASS + SKEPTIC ROBUST, and says whether it is just the known volatility bet |
| OFF since 2026-10-16 | QUANT, PROPDESK, FILLS (paper-fleet ranking) and the code crew REGRESSION, DOCSYNC, SCANNER, TESTGAP, RISKGUARD, HERMES | disabled to spend the weekly budget on the edge search — re-enable in StarNet → routines (job backup `~/.local/share/rithmic-desk/backups/cron-jobs-pre-edge-20261016.json`) |
| Sun 10:00 | CURATOR (SABLE, Opus) | prunes/merges the learning memory, promotes repeated lessons to `memory/DESK.md` |

**Weekly limit hit = pause, not retry:** disarm StarNet's scheduler (`POST /api/cron/arm {"enabled":false}`),
`systemctl --user stop rithmic-research-sync.timer`, write the reason to `~/.local/share/rithmic-desk/PAUSED`
(the watchdog then stays quiet), and let a one-shot timer re-arm after the reset (`starnet-desk-resume.timer`
did this for 2026-10-04 10:05). StarNet disables a job after 5 consecutive failures — re-enable the ones it
switched off when you resume.

**Run only on change:** every chained job starts with its own cheap skip check in the prompt (is my input — research data hash, the upstream desk file, the clone commit, the live fills — newer than my last output?); if not, it writes nothing and replies `no new input` (~2 calls instead of a full run). **Do NOT gate with StarNet `contextFrom` or a pre-run `script`:** both taint the run and StarNet withdraws the agent's terminal ("untrusted-content-lockout") — even a silent script, because the shell tool reports empty output as the text "(no output)" (found and reverted 2026-09-30). **Learning memory:** every agent reads `~/starnet-work/memory/DESK.md` (the shared playbook — only SABLE edits it) and its own `memory/<AGENT>.md` first, and appends 1-3 evidence-cited lessons (`- YYYY-MM-DD [worked|failed|rule] <lesson> (evidence: <file> <numbers>)`) at the end of every real run; the BRIEF only reads (it is the one holdout reader). SABLE's CURATOR job (Sundays 10:00) merges, retires contradicted/stale lessons and promotes repeated ones to DESK.md, logging `memory/CURATION-<stamp>.md`. **Holdout:** the loop (QUANT, PROPDESK, SKEPTIC, STEER, researchers) is told never to read `~/starnet-work/holdout/`; only the BRIEF judges desk variants out-of-sample. The fleet's own forward results on held-out dates are still visible to the loop, so the only fully clean test is future sessions. Night Shift is off (autonomy initiative `propose`). **Ops:** StarNet runs as the user unit `starnet` (`deploy/starnet.service`; log `~/starnet/logs/sidecar.log`); `scripts/starnet/desk_watch.sh check` (cron :20 hourly) alerts once via grid-notify/Telegram when StarNet, the adapter or the scheduler is down, the desk goes stale, a run is tainted, the subscription limit or pace cap is hit, Night Shift re-enables, or a stale StarNet tab overwrites the roster/goal; `desk_watch.sh cleanup` (03:40) keeps 14 days. Reload any open StarNet tab after a restart — a stale tab can push an old roster/goal over the station's.

The desk works toward ONE StarNet goal (set 2026-09-30): find a strategy that passes the Tradeify evaluation on 1 NQ and prove it on paper (≥20 forward trades / 3+ sessions, SKEPTIC ROBUST, PROPDESK >50% to +$3,000 before breaching −$500 daily / $1,000 trailing / 30% consistency, ≤5 trades/day). Night Shift is steered to it. Going live stays the founder's decision: the desk only proposes. The `desk` label is research
only (the live paper fleet is `tradeify`); `desk_replay.sh` deletes and re-writes rows of that label
for the ids it replays and nothing else. Uncommitted work in this checkout is invisible to the crew.
Jobs live in StarNet (`/api/cron`) and fire only while its scheduler is ARMED (`GET /api/cron` → `enabled: true`, `health.lastTickAt` recent; arm with `POST /api/cron/arm {"enabled":true}`). The adapter caps StarNet at 40 calls per rolling hour and 250/day, 4 in flight, and backs off 15 min
after a subscription-limit error. Findings are not auto-applied — triage them in a session here.

## Stack
Everything is C++. There is no Python in this project.

- **`rithmic_engine`** — WebSocket tick collector (market data → PostgreSQL)
- **`nq_executor`** — order execution (production trader); one engine per process — `orb` (OrbStrategy), `trend` (TrendStrategy), or `mtf_scalper` (MtfScalperStrategy) via `config`'s `"engine"` key
- **`audit_daemon`** — 17-check quality daemon (local/testing only — NOT on Oracle)
- **Build**: `cmake -B build && cmake --build build -j$(nproc)`
- **Tests**: `build/test_orb_strategy`, `build/test_trend_strategy`, `build/test_mtf_scalper`, `build/test_risk_manager`, `build/test_order_manager`, `build/test_validator`, `build/test_db`, `build/test_lifecycle`, `build/test_incident_replay`, `build/test_trade_end_invariant`, `build/test_paper_broker`, `build/test_bracket_broker`, `build/test_parity_paper_vs_live` (the full list `scripts/hermes.sh` runs)

## What to improve (in priority order)

1. **Failing tests or audit checks** — fix these first, always
2. **C++ build warnings** — treat as errors on new code
3. **Silent failures** — unchecked return values, ignored error codes
4. **Missing tests** — new logic in `src/execution/` needs a corresponding test in `tests/execution/`
5. **Infrastructure gaps** — missing service files, config validation, logging
6. **Code quality** — dead code, unclear error messages

## What NOT to touch

- `src/execution/orb_strategy.hpp` — no changes to C++ strategy logic, entry/exit conditions, or indicators
- `config/live_config.json` — no changes to live trading parameters
- Python files — there are none; do not create any

## CRITICAL — Order Routing

**NEVER use `"Rithmic Order Routing"` as a trade route.**
This route causes immediate silent order cancellation on Legends Trading accounts (rp_code=1043, notify_type=15, total_fill=0 — order never reaches the exchange).
The correct trade route for Legends Trading accounts is **`"simulator"`**.
If `RequestTradeRoutes` (tid=310) returns rp_code=1043 (no routes found), fall back to `"simulator"` — never to `"Rithmic Order Routing"`.

## After making changes

Always run `make hermes` (or `make hermes-fast` for a quick loop) before reporting done.
If tests break because of your changes: fix the code OR add tests that cover the new behavior.

## Key files

| File | Purpose |
|---|---|
| `data/hermes_findings.json` | Output of last `make hermes` — read this to decide what to fix |
| `data/logs/hermes_session.log` | History of all session results |
| `data/audit_status.json` | Full audit_daemon output (local/testing) |
| `scripts/hermes.sh` | C++ Hermes runner (build → tests → audit_daemon) |
| `src/audit_daemon_main.cpp` | 17-check quality daemon source |
| `src/execution/executor_main.cpp` | nq_executor entry point |
| `src/execution/orb_strategy.cpp` | ORB strategy implementation |
| `src/execution/trend_strategy.hpp` | Configurable trend engine (2 dozen modes) used by the paper fleet and, since 2026-09-25, live (`"engine": "trend"`) — `tests/execution/test_trend_strategy.cpp` |
| `src/execution/mtf_scalper_strategy.hpp` | "Momentum Scalper — MTF Flag AutoPilot v5" (Pine port); self-managed bracket via `cur_stop()`/`cur_tp()` — live since 2026-09-25 (`"engine": "mtf_scalper"`) — `tests/execution/test_mtf_scalper.cpp` |
| `src/execution/mtf_scalper_config.hpp` | mtf_scalper's own config fields, read from the same JSON file as `OrbConfig` (flat snake_case, like trend) |
| `src/paper/paper_bracket_broker.hpp` | Paper-fleet fill simulator for mtf_scalper (strategy-owned bracket) — the live executor's `OrderManager::check_external_stop()` mirrors its stop/target semantics — `tests/paper/test_bracket_broker.cpp` |
| `src/paper/paper_main.cpp` | Paper fleet runner (engines orb / mtf_scalper / trend, replay mode, feed-gap guard) |
| `scripts/sql/strategy_leaderboard.sql` | Postgres `strategy_leaderboard()` — the ONE ranking of every paper + live strategy over a period / slot; read by the dashboard Leaderboard tab and by the rotation |
| `scripts/daily_plan.sh` + `scripts/daily_plan_live.jq` | The dashboard Plan page (`data/plan/<day>.json`), rebuilt every minute 09:00–13:59 ET by `daily-plan.timer`: setups, "allowed now", EMA box, momentum boxes (`live.mom` 30-min + EMA-20, `live.mom45` 45-min + VWAP + plan agreement), payout sprint. `scripts/plan_alerts.sh` sends the Telegram alerts (once per key per day) |
| `src/execution/trend_strategy.hpp` mode `mom_dir` | 1-min momentum direction + 5-min entry (any / ema / vwap / dip, `mom_need_plan`); paper ids `mom_dir_30_any`, `_30_ema20`, `_45_vwap`, `_45_vwap_plan`, `_45_dip`, `_45_ema9_tight`. Break-even in R = PaperBroker `be_r` (paper only — the executor refuses it) |
| `scripts/local_backup.sh` | Nightly 03:30 (`local-backup.timer`) copy to the second drive `/media/theone/381C48C51C48803C/rithmic_backup`: Postgres dumps (14 there, 3 on the main disk), git bundles, owned market data. The Oracle off-box copy has failed since ≥ 2026-09-27 (founder: ignore Oracle for now) |
| `scripts/manual_review.sh` + `scripts/working_fleet_report.sh` | Evening checks (user timers `manual-review` 16:15 ET, `working-fleet-report` 16:30 ET): the founder's fills export (`data/manual_fills/`) vs the Plan page replayed at each entry minute + signed fill cost (`data/manual_review/`); the forward scorecard of `config/working_fleet.json` with Telegram on verdict changes and Fridays |
| `research/programs/` + `scripts/research_repro.sh` + `scripts/plan_parity.sh` | Every one-off research program with its saved output; re-run and compare; Plan page vs paper engine |
| `scripts/rotate_handoff.sh` | Weekly rotation of the hand-off engine (`config/rotation.json` guardrails, `--dry-run`, `--force <paper id>`); validates the generated config with `nq_executor --check-config` |
| `config/tradeify_handoff_config.json` | The hand-off instance ORB hands to at 10:00 ET — rewritten by the rotation; `paper_source` = the paper twin it is compared on |
| `scripts/sql/bars_1m.sql` + `scripts/refresh_bars.sh` | 1-minute OHLCV rollup of `ticks` (`bars_1m`, per symbol, exact parity; `refresh_bars_1m()` incremental ~35 ms) refreshed every minute by `bars-1m.timer`. The dashboard's /chart and /models read it (in-progress minute still from `ticks`) — 100–400 ms tick aggregations became ~1 ms. Any TF aggregates exactly from it. |

**Database notes (2026-09-26):** covering indexes `idx_ticks_symbol_ts (symbol, ts_event DESC) INCLUDE (price,size,seq,is_buy)` and `idx_bbo_symbol_ts (symbol, ts_event DESC) INCLUDE (bid/ask…)` make every symbol-scoped range (pages, executor pg polls, replays) index-only. `idx_ticks_ts` (173 MB) stays: the collector's per-minute `MIN/MAX(ts_event)` summary needs a symbol-less time index. Postgres tuned the same evening (`shared_buffers` 2 GB, `effective_cache_size` 8 GB, `work_mem` 64 MB, `random_page_cost` 1.1). Growth ≈ 200–250 MB per trading day (`ticks` ≈ 720 k rows, `bbo` ≈ 500 k); `audit_log` no longer takes a row per write batch (was 99.98 % of it — trimmed 252 MB → 160 kB). Founder decisions 09-26: **retention = keep everything**, partitioning/TimescaleDB = leave as is; the old Python bot's mirror tables were dropped (`migrations/20260926_drop_legacy_tables.sql`). **Backup**: `pg-backup.timer` (02:40 nightly, `scripts/pg_backup.sh --verify`, custom-format dump ≈ 65 MB to `data/backups/pg/`, keep 14, Sunday off-box copy to Oracle — best-effort) — the 03:00 bot backup never included Postgres. Derived tables, all refreshed every minute by `bars-1m.timer` → `scripts/refresh_bars.sh`: `bars_1m`, `book_1m` (quotes per minute: spread, imbalance, bid/ask-heavy share), `session_stats` (one row per NY session: range, gap, ORB-5 width, ATR-14, volume vs 20d, day_type), views `bars_5m/15m/1h`; `contracts` + `calendar` (`scripts/sql/contracts_calendar.sql`: third-Friday expiries, roll = Thursday 8 days before — **NQ/MNQ/ES roll to Z6 on 2026-12-10**, holidays, FOMC, NFP; `upcoming_events(days)`); `live_order_events` (every order message the executor sends or receives, written by `OrbDB::write_order_event`, never throws). Every process reconnects on its own after a Postgres restart (collector, paper engine, executor incl. its audit connection).
| `src/execution/order_manager.cpp` | Order lifecycle management; `check_trail_and_stop()` (ORB/Trend, cfg-driven BE/trail) vs `check_external_stop()` (mtf_scalper, strategy-driven ratchet — deliberately NOT a shared refactor, see its header comment) |
| `src/execution/risk_manager.cpp` | Pre-trade risk checks |
| `src/client.cpp` | WebSocket client (Boost.Beast) |
| `src/db.cpp` | PostgreSQL I/O (libpq) |
| `src/collector.cpp` | Tick collector pipeline |
| `config/live_config.json` | Runtime config (do not edit) |
| `deploy/nq_executor@.service` | Systemd template unit (RTH sessions) |
| `deploy/nq_executor_24x7@.service` | Systemd template unit (24×7 mode) |

## Local mode

Local runs do not use systemd — `deploy/*.service` are **Oracle-only**.

- **Dashboard** on http://localhost:3000 launches and supervises everything locally.
- **Backend** on `:8080` runs with `CPP_LOCAL=1`; it spawns the **collector** and
  `nq_executor` as `nohup` subprocesses (per-account config, e.g.
  `./build/nq_executor --config config/tradeify_config.json`).
- To stop a local executor: `pkill -SIGTERM -f 'nq_executor --config'` (SIGTERM flattens and exits cleanly).
- **Tick collector runs 24/7 as a systemd USER unit** `rithmic-collector-local`
  (`deploy/rithmic-collector-local.service`, installed in `~/.config/systemd/user/`;
  `systemctl --user status rithmic-collector-local`, log `data/logs/collector.log`). It is
  the box's ONLY Rithmic market-data session (a prop login allows one TICKER_PLANT
  session) and feeds the paper fleet, the chart and pg-mode executors.
- **Tick fan-out ports (2026-10-10)**: the tick collector publishes every trade + BBO on 127.0.0.1:8198
  (`RITHMIC_FANOUT_PORT`) and crypto_collector every trade/bookTicker/mbo minute on 127.0.0.1:8197
  (`CRYPTO_FANOUT_PORT`; 0 = off), JSON lines, `src/tick_fanout.hpp`. Never blocks the feed or the DB writer
  (drops its own lines, counted in the status/summary line). Consumer: the dashboard's `/ws/market`. The
  executor still reads Postgres — the fan-out is display only.
- **Live executors run on the collector's feed**: `RITHMIC_MD_PROVIDER=pg` in
  `.env.<account>` (set for `tradeify`) makes `nq_executor` open no MD session and poll
  the Postgres `ticks` table instead (`md_feed_symbol`, default NQ). Do not start an
  executor in WebSocket MD mode (any other provider) while the collector runs — Rithmic
  force-logs-out one of them every ~35s.
- **Live-executor rules since 2026-09-30 (audit fixes, CHANGES.md):** a `dry_run: true` config writes under a label ending in `_dry` (appended automatically — simulated fills never reach the live tables); risk seeds are ACCOUNT-WIDE on the live label; the broker balance from tid=451 drives a persisted high-water mark (`live_account_hwm`) and halts entries at HWM − `trailing_drawdown_cap` (log `[BROKER-HWM]`); the break-even stop move always reaches the exchange; multi-lot rows record VWAP; `commission_rt` is the measured fee (1.82 MNQ — NQ unknown, see `_commission_note`).
- **SECOND-STRATEGY TEST, 10:30–12:00 ET (founder 2026-10-04: "keep the ORB as is", find the best bounce / trend / momentum strategy for the slot after it):** paper only. `config/slot2_fleet.json` holds 11 windowed copies (`s2_*`) of the best live-runnable variants per family; `slot2-eval.timer` (weekdays 16:20 ET) runs `scripts/slot2_eval.sh`, which replays each new session under label `slot2` and rewrites `data/slot2/report.md`. The candidates were SELECTED on sessions ≤ 2026-10-02, so only later sessions count: PASS = ≥20 trades on ≥8 days, PF ≥1.3, net >0, both halves positive, breakeven scratches ≤0.5, worst day ≥ −$500 (NQ $). First PASS sends one grid-notify alert. A pass earns a dry-run instance after the ORB — live stays the founder's call. Why this slot: 15-min range averages 75 pts (ORB hour 106, after 14:00 ~47).
- **HISTORY (2026-10-10, founder: all MBO NQ history on the chart and strategies pages):** owned Databento data, READ-ONLY, in `/home/theone/Desktop/bot/data/raw` (`mbo/` GLBX NQ.c.0 MBO 2023-03 … 2026-03-19, `ohlcv-1m/` NQ 2010-07 …, `es-ohlcv-1m/` ES 2010-07 …, DBN v1 + zstd). `src/history/dbn.hpp` (DBN reader), `mbo_feed.hpp` (book rebuild → ticks + top-of-book quotes), `binance_feed.hpp` (Binance aggTrades files), `build/dbn_tool` (`info|dump|bars|mbo-stats|mbo-1m|ticks|agg-bars`). Loaded: `bars_1m` NQ + ES back to 2010-07 (buy_volume/ticks = 0 where only OHLCV exists; MBO months filled in by `mbo-1m`), `session_stats` back to 2010 (baselines now bounded to 30/45/10 calendar days so the 2026-03-20 … 09-18 gap starts fresh), per-minute NQ order flow in `mbo_1m` (venue `GLBX`, symbol `NQ`) + `book_1m`. Replays: `paper_engine … --dbn-mbo FILE…` / `--binance-agg FILE…` / `--dbn-bars FILE…` (1m bars → 4 synthetic prints, label `nq_bars`, NQ 2010-07…2023-02, binary `build-hist/`) read files directly (no ticks into Postgres); the NQ fleet's history is label **`nq_hist`** (MBO months + the collector's own months from 2026-09), crypto's is `crypto_hist` (Binance aggTrades in `data/history/binance/<SYM>/`, `scripts/binance_history_download.sh`). One month = `scripts/history_month.sh {nq|nqx|nqbars|btc|eth} YYYY-MM` (nqx = night grid + staged winners/newmodes, same label nq_hist, id-scoped deletes); the whole set = `scripts/history_queue.sh run` (≤2 replays, 6 while CME is closed Fri 17:00→Sun 18:00 ET, SIGSTOPs them in weekday RTH and the Asia window, status `data/history/queue_status.json`, log `data/logs/history_queue.log`); results `scripts/history_report.sh` → `data/history/report.md`. Each month replays cold; history has no NQ data 2026-03-20 … 2026-09-18 and no crypto quotes. Crypto fleets use RELATIVE costs (fleet keys `commission_pct` / `slippage_bps`, PaperBroker + PaperBracketBroker; 0 = the fixed `commission_rt` / `slippage_ticks`, so NQ and golden are untouched) and history months replay `crypto_fleet.sh month-config` (point knobs × month mean close / `knob_ref_price`). Never edit a script a running queue/worker is executing in place (bash reads it as it runs — 2026-10-10 a worker died mid-run): write a new file and `mv` it over.
- **DAILY-CONTEXT HOLDS (2026-10-11, founder: multi-day holds allowed):** `hold` mode takes prior-session filters
  (`hold_prev_ibs_max`, `hold_down_streak`, `hold_rsi2_max`, `hold_donchian`, `hold_above_sma`) and multi-session exits
  (`hold_days`, `hold_exit_ibs_min`, `hold_exit_up_close`); 18 NQ + 18 ES ids (`wc_*`, `ds*_xup`, `fear18_*`, `rsi2_20_*`,
  `dc20_h5`) forward-test the sweep's survivors (weak close → buy 18:00 → hold 1–3 sessions / exit on strength: PF 1.5–3,
  15/17 years, ES confirms). `hold_days > 1` / `hold_exit_*` are PAPER ONLY — the executor refuses them and the leaderboard
  marks them not live_runnable; such variants run with `eod_flatten_hour 24` (the strategy owns the exit).
  **BEST STRATEGY SO FAR — `docs/strategy_weakness_vix_book.md`** (the trade, evidence, simulator numbers, fleet ids, go-live checklist).
  **VIX regime (2026-10-11):** `scripts/vix_fetch.sh` (cron 16:45 ET) keeps `data/regime/VIX*_History.csv`; `hold`
  mode's `hold_vix_min / hold_vix_rise_min / hold_vix_pct_min / hold_vix_any` gate on the prior close's regime (paper
  runner + executor call `set_vix`; no file = gated holds never enter). Buying weakness is ≈ 0 edge when VIX < 15 and 3–8×
  when VIX ≥ 20 / rising / top tercile — the conviction-sized `anyweak*` + `wc_ibs30*` book is the payout candidate
  (the 2026-10-11 evening sim's 53 %-of-windows / 28 %-blown figures are INVALID — per-unit rows; as one position the stack
  blows ~12×/yr on a $2,000 trail, ledger P3 — and the hold is illegal on Tradeify anyway). `hold_weak_any` = OR over the weakness filters.
  **Tradeify (help centre, 2026-10-11): flat by 16:45 ET every day, no overnight holds — multi-session variants are
  research only; the 18:00 → 15:55 one-session hold is inside one 18:00–17:00 trading day (confirm with support before
  going live).** `payout_sim` carries the official Select eval / Flex / Daily rules and an eval-phase model (CHANGES
  2026-10-11); the answer so far: no biweekly payout with current rules — `__lv_conf` bases on Select Daily 50K at 1–2 NQ
  are the safe path (eval pass ~30–54 % in 4 months, 0 % fail; ~$300–600/month funded, 1–1.5 payouts/month in steady state).
- **ORDER-FLOW ORB (2026-10-14, paper only):** `range_break` + `rb_of_filter 1` (range window's net aggressor volume must
  agree with the break), `rb_first_only`, `rb_stop_range` (stop = other side of the range, paper broker only — the
  executor refuses it), `rb_last_entry`. Fleet ids `ofo_ny5` (09:30–09:35 range, hold to 15:55) and `ofo_asia15`
  (20:00–20:15, hold to 02:00) + controls `_dis` / `_all`. Research PF 1.27 / 1.24 on 2023–26 (in-sample, the only
  years with aggressor volume); nothing passes 2011–26 without order flow. Payout sims: use it to pass the EVAL faster,
  not in the funded account (CHANGES 2026-10-14). `ofo_ny5_vfall` / `ofo_asia15_vfall` (`vix_chg1_max -0.03`) trade only
  after the prior VIX close fell ≥ 3 % — with the base id that is 2× size on those days.
- **PRICE LEVELS (2026-10-11, paper only):** `src/paper/paper_levels.hpp` LevelState (prior day, overnight, opening range, VWAP bands, value area, naked POCs, week/month, round numbers; seeded from bars_1m, one per engine) + PaperBroker `lv_room_atr` / `lv_near_atr` gates, `lv_target`, `lv_stop` (× ATR; `lv_set` picks families). Variants `<base>__lv_room|lv_conf|lv_tgt|lv_stop` on every ORB/trend base (`ONLY=lv_ scripts/fleet_add_variants.sh`). The executor refuses lv_* keys; the leaderboard marks them not live_runnable. History: they run with `nqx`, not `nq`.
- **PAYOUT SIMULATOR (2026-10-11, founder: a 25k/50k account paid every 1–2 weeks):** Daily rules (profit stop / loss stop / max trades, 64 combos) are chosen on the first 60% of days and scored on the rest; greedy portfolios use one member per root strategy; filters include `trend_up50`/`trend_dn50` (last closed session vs its 50-session average, table `session_trend`). `scripts/payout_sim.sh` exports `nq_hist` trades (alone and per regime known at the open: gap vs ATR14, yesterday's day type; series ≥40 trades and net > 0) and `build/payout_sim` runs each through Tradeify Select Flex / Growth 25k/50k rules at 1/2/3/5 MNQ and 1/2 NQ from every 2nd day for 60 days → `data/payout/report.md`. Breaches tested at each trade's MAE (Growth trails intraday incl. MFE, Flex end-of-day), prop trading days 18:00→17:00 ET, paper P&L is per **MNQ** (fleet point_value 2; NQ = 10× gross). Plan rules + assumptions (profit share 0.5, floor lock start+100) in `src/paper/payout_sim.hpp`; tests `test_payout_sim`. Read-only; a hit is a candidate, not a go-live. **History reads go through `hist_trades` / `hist_trades_regime`** (`scripts/sql/history_done.sql`, synced from done markers by `scripts/history_done_sync.sh`): finished months only, regime known at entry (gap = latest 09:30 open ≤ entry, prev = latest 16:00 close ≤ entry) — never read `paper_trades` for history research directly (half-replayed months, look-ahead). Queue order: `data/history/priority.txt` goes first, read every loop. **Walk-forward** (`scripts/walk_forward.sh` → `build/walk_forward`, `src/paper/walk_forward.hpp`, `test_walk_forward`): per family × session window, each month trades the variant that was best over the previous 3/6/12 months (stand aside if ≤ 0), compared with blind (average variant) and hindsight (best variant) → `data/walk_forward/report.md`. This is how parameters for a session are chosen — never from an in-sample ranking.
- **OVERNIGHT GRID (Asia + London, 2026-10-04):** `config/night_fleet.json` (948 `nt_*` variants) is replay-only research — `night-eval.timer` (weekdays 16:30 ET) runs `scripts/night_eval.sh` (label `night`, refuses in RTH and the Asia session) → `data/night/report.md`, stats by window × family × liquidity gate. The liquidity gate (`liq_vol_min` / `liq_impact_max` / `liq_bars`, RegimeState) works live and on paper. **A trend variant's window outside RTH now also sets the paper broker's entry window** (before 2026-10-04 every overnight trend variant was silently blocked); a LIVE overnight trend config must still set `session_open_*`/`eod_flatten_*`/`last_entry_hour` itself.
- **CRYPTO FEED (2026-10-10):** `build/crypto_collector` (user unit `crypto-collector-local`, log
  `data/logs/crypto_collector.log`) writes Binance USDⓈ-M futures BTCUSDT/ETHUSDT trades to `ticks` and
  top of book (last quote per 100 ms) to `bbo`, exchange `BINANCE`, source `binance_fut` — the historical
  backfill uses source `binance_hist`, same row rules (size in 0.001 lots, side B/A, seq = agg id % 2e9).
  Bitstamp BTC/ETH market-by-order is archived raw FOREVER in `data/mbo/bitstamp/<pair>/<date>.jsonl.zst`
  (never delete) with a per-minute summary in `mbo_1m`. One Postgres connection, async commit, drops its
  own rows when its queue is full. `--dry-run` connects and counts without writing.
  **Crypto strategy test (paper only):** `scripts/crypto_backfill.sh FROM TO` (Binance history → `ticks`;
  `BARS_ONLY=1` → `bars_1m` for ATR warm-up), `scripts/crypto_fleet.sh build|register|replay|report` — every
  NQ strategy mirrored as `btc_*`/`eth_*` (0.1 coin, knobs × range ratio to NQ), replay label `crypto_hist`,
  forward label `crypto`, report `data/crypto/report.md`. /live has an NQ · Rithmic | Crypto VIEW switch (bot
  `ui/routers/crypto.py`); it never changes what the live account trades.
- **MANUAL SIGNALS (2026-10-08):** the `sig_*` strategies in `config/paper_fleet.json` (`publish_signals: true`)
  publish entry / move-stop / exit Telegram alerts for 1 MNQ with the BRACKET to set; `live-trade-alerts` (user
  unit, `scripts/live_trade_alerts.sh`) sends the same format for the live executor's own trades. Pages:
  /signals, /chart?signals=<id> (the /live Manual signals strip was removed 2026-10-09). Paper ORB ≠ live ORB — do not publish the paper copy.
- **LIVE ROTA (2026-10-08, founder: "all sessions … based on regimes"):** systemd user timers `live-rota-{asia,ldn_e,ldn_l,ny}.timer`
  (`~/.config/systemd/user/`, installed by hand — not in `deploy/`) run `scripts/live_rota.sh <window>` 2 min before each window. It reads
  eff = |last − open| / range (NY: from 09:30; overnight: prior 120 min), picks the sig_* strategy for that regime or stands aside,
  generates `config/tradeify_handoff_config.json` (1 MNQ), validates it with `--check-config` and runs `strategy_handoff.sh`
  (NY hard 12:00 so an open ORB trade finishes). Picks (clean replay 09-21..10-08, tight trails): Asia band fade / London EMA pullback /
  London Donchian only when trending; NY VWAP trend when middle, RSI-2 when trending, aside when choppy. Log `data/logs/live_rota.log`
  (`--check` runs stay off it); /live shows the holder + per-window decision (`/api/signals/rota`). The execution audit merges the
  `_handoff` log and checks limits per strategy. Off: `systemctl --user disable --now live-rota-{asia,ldn_e,ldn_l,ny}.timer`.
- **ORDER TEST (2026-10-07):** `scripts/order_probe.sh tradeify` proves the live account can place an order with NO fill (1 BUY LIMIT 100 pts below market → accepted → cancelled), then restarts the live unit. Never run it inside 09:25–10:35 ET (it refuses). Do not invoke `build/nq_executor` by hand to explore flags.
- **EXECUTION VALIDATION, EVERY DAY (founder 2026-09-30):** three timers guard the live path —
  `pre-rth-check.timer` 09:00 ET (config + broker readiness incl. contract/roll, commission, drawdown
  room), `execution-watch.timer` every 2 min in RTH (`scripts/execution_watch.sh`: unit, mode,
  broker sessions, feed, standing halts, exchange-stop lag; alerts on transition) and
  `execution-audit.timer` 16:10 ET (`scripts/execution_audit.sh [account] [date]`: signals → orders →
  fills → live_trades → broker P&L reconciliation, stop integrity, limits, feed gaps; JSON in
  `data/execution_audit/`). Run the audit by hand for any past date. A red line from any of them
  before 09:30 ET means do not expect fills.
- **2026-10-11 EVENING — ORB STOPPED, THE BOOK RUNS DRY (founder: "switch strategies to the ones that will work").**
  `nq-executor-local@tradeify` is stopped (unit still enabled — do not restart it without the founder), `tradeify_mtfasia`
  and the `live-rota-*` timers are disabled. `nq-executor-local@tradeify_book` (`config/tradeify_book_config.json`,
  `dry_run: true`, label `tradeify_book_dry`, tag `WEAK_STACK`) runs the any-weakness hold with the CONVICTION STACK
  (`stack_wc_units / stack_vix_units / stack_both_units`, `qty 1`, `qty_max 4` — 1 MNQ, +1 weak close ≤0.3 IBS, +1 VIX fear,
  +1 both) with NO orders; the executor sizes from `TrendStrategy::last_units()` via `OrderManager::set_entry_units()`
  (CHANGES 2026-10-11). **INTRADAY ONLY (founder, same night): the book is research-only under Tradeify's flat-by-close
  rule.** `nq-executor-local@tradeify_day` (`config/tradeify_day_config.json`, `dry_run: true`, label `tradeify_day_dry`,
  tag `WEAK_DAY`) runs the legal form: any-weakness close AND VIX ≥ 20 → buy the 09:30 open, sell 15:55 (1 MNQ, +1 at
  VIX ≥ 25, no stop — every stop/dip/early exit lowers the edge; $53/MNQ, PF 1.60, 14/16 yrs, 33/yr; ledger S1,
  `docs/intraday_search_2026-10-11.md`). Every other intraday door (mechanics, regime-conditioned fleet, at-open
  conditions, last hour, crypto sessions, MBO order-flow ML) is measured empty — do not re-run. The lines below are history.
- **LIVE ORB = 2 MNQ SINCE 2026-10-09 (founder; was 1 MNQ from 2026-10-07; rota windows are sized from the room, `scripts/live_rota.sh`):** the Tradeify Growth 25k has $90.38 of trailing-drawdown room (Tradeify floor $24,189.86, intraday trailing incl. open profit) — below one 1-NQ stop ($301.82), so every 1-NQ signal on 10-05/10-06 was refused by the broker-room guard. Signals still come from NQ ticks; NQ copy in `config/archived/live_history/tradeify_config.pre-mnq1-*.json`. Back to NQ only after a reset or once room > one NQ stop.
- **LIVE SINCE 2026-09-30 10:47 ET: ORB only, 1 NQ (NQZ6), 3 trades/day, entries 09:35–10:30 ET
  (`last_entry_hour 10` + `last_entry_min 30`), commission_rt 1.82 (MNQ figure — replace from a
  Tradeify NQ statement).** The dry-run day 2026-09-30 was cleared on the founder's word.
- **FEED-DROP TRAP (2026-09-30):** the paper fleet's Postgres write rate can starve the collector's
  tick writer (queue 8192 batches now; "Writer queue full — dropped tick batch" in
  `data/logs/collector.log` = live feed gaps). Paper flushes are change-only, batched, async-commit.
  Never add a per-tick DB write to the paper engine; check the collector log after any fleet change.
- **DRY-RUN HISTORY (founder 2026-09-29 evening: "test it for now")** — `config/tradeify_config.json` carries `dry_run: true`; nothing reaches the account until the founder says to flip it (then `--check-config` + `systemctl --user restart nq-executor-local@tradeify`). While dry, the 09:00 ET pre-RTH check reports red ("no [BROKER] line") by design.
- **LIVE = ORB ONLY, 1 NQ (founder 2026-09-29: "lets run NQ for 1 contract not MNQ and we only keep ORB")**: `config/tradeify_config.json` is `symbol NQ / trade_contract NQZ6 / point_value 20 / qty 1` (previous 5-MNQ copy in `config/archived/live_history/`); the hand-off config moved to `config/archived/` (off the live board) and `strategy-rotation.timer` is disabled with it. The pg feed already ran on NQ ticks, so the executor now trades the instrument it watches. Risk knobs unchanged: SL 15 pts = $300/trade at $20/pt, daily_loss_limit −$500, trailing cap $1,000.
- **HAND-OFF REMOVED FROM LIVE 2026-09-29 (founder: "remove FIB_PB_1M_DEEP from the live")**: `strategy-handoff.timer` is disabled, `config/tradeify_handoff_config.json` is `dry_run: true` (previous live copy in `config/archived/handoff_history/`), and ORB runs alone all day. To restore: set `dry_run: false`, validate with `--check-config`, `systemctl --user enable --now strategy-handoff.timer`. The description below is how it worked while live.
- **ORB → hand-off engine (tradeify, 2026-09-25 → 2026-09-29)**: ORB (`tradeify`) opens 09:30 ET;
  `scripts/strategy_handoff.sh tradeify tradeify tradeify_handoff` waits until ORB is done
  (5/5 trades, or flat at 10:00 ET = 17:00 Cyprus), stops it, starts
  `nq-executor-local@tradeify_handoff` (`config/tradeify_handoff_config.json` — whichever
  strategy the rotation installed there; window 10:00–12:00 ET, 2 MNQ) and restores ORB at
  16:00 ET. **Since 2026-09-26 the installed hand-off strategy is `FIB_PB_1M_DEEP`** (trend
  engine, fib_pullback 1m; promoted with `rotate_handoff.sh --force fib_pb_1m_deep` on the
  founder's call — the per-contract leaderboard put it first in the slot while the momentum
  scalper was the weakest engine in the fleet). The Monday 09-28 rehearsal
  (`config/tradeify_dryscalper_config.json`, label `tradeify_dry`) is a dry-run copy of it. Armed every weekday 09:29 ET by `strategy-handoff.timer`
  (`deploy/strategy-handoff.{service,timer}`, installed in `~/.config/systemd/user/`):
  `systemctl --user enable --now strategy-handoff.timer` to run it daily, `disable --now` for
  ORB-only. The hand-off needs the instance LIVE (`dry_run: false`) — a dry-run executor opens
  no PNL plant and fails the script's "[BROKER] + flat" readiness check (it is then stopped
  and `NO_DEPLOY` set). A dry-run instance opens NO Rithmic session at all, so it can rehearse
  in parallel with the live ORB (`config/tradeify_dryscalper_config.json`, label `tradeify_dry`
  so its DB rows stay separate). Retired instances go to `config/archived/` — that is what
  takes them off the dashboard's live board (it lists `config/*_config.json` minus `archived/`;
  strategy tags with trades but no config show there as "retired" rows, stats kept).
- **Weekly rotation of the hand-off engine by performance** (`scripts/rotate_handoff.sh`,
  `strategy-rotation.timer` Sunday 18:30 ET, guardrails in `config/rotation.json`): ranks every
  paper strategy with the Postgres function `strategy_leaderboard()`
  (`scripts/sql/strategy_leaderboard.sql` — apply with `psql -f`; the dashboard's
  `/strategies` → Leaderboard tab shows the SAME ranking) over the last 7 days in the
  10:00–12:00 slot; a live-runnable candidate that clears ≥20 trades / ≥3 sessions / PF ≥ 1 /
  net > 0 and beats the incumbent's paper twin (`paper_source` in the hand-off config) by 20%
  gets its config generated from its paper params, validated with `nq_executor --check-config`,
  and installed (old one to `config/archived/handoff_history/`). The strategy tag becomes the
  upper-cased paper id (`MTF_FLAGS_V5`, `FIB_PB_1M_DEEP`, …) so each promoted strategy keeps its
  own live history. It never flips `dry_run` (carried across) and never runs while the
  instance is live. `--dry-run` rehearses, `--force <paper id>` is the manual promotion.
  The ranking also carries Sharpe/Sortino (daily P&L series, annualised √252 — noisy under
  ~10 sessions, read next to `thin`). **Ranking is per CONTRACT** (`net_per_contract`, since
  2026-09-26): paper mtf variants size 3 lots (`qty_max`; 25 session variants were uncapped
  at 8–16 lots until then), ORB/trend variants 1, live 2 — raw net P&L rewarded size, not
  edge. Every paper_trades / live_trades row records its `qty`; the board shows avg qty.
- **Order flow live (since 2026-09-25, pg feed only)**: the executor polls the collector's `bbo`
  table and merges quotes into the tick stream by time (paper_main's ordering), driving (a) the
  OrbConfig **book entry gates** — `spread_gate_ticks/_rel`, `imbalance_min`, `imbalance_max`
  (inverted: fade the stacked side — the only book overlay with an edge, PF 2.19 vs 0.92),
  `microprice_lead` — via the same `paper::QuoteState::gate()` the paper brokers use, (b) the
  **book exits** `book_exit_flip` / `book_tp_imbalance`, and (c) `strategy.on_quote()` for the
  trend engine (`book_imbalance`, and `bi_invert: true` = book fade). So the paper fleet's
  `__sg/__imb/__micro/__inv/__all/__bx/__btp` overlay variants are live-runnable and the rotation
  keeps their gate keys; `__sz/__wait/__bbe` (paper-fill semantics) are not. A gated config
  without the pg feed is refused at startup. **Tape modes** in the trend engine (aggressor side
  per tick → bar delta): `absorption_reversal`, `delta_divergence`, `volume_burst` — paper
  variants `absorption_rev_*`, `delta_div_*`, `volume_burst_*` (+ `__inv` overlays on the two
  promising ones). Replay over 2026-09-21..25 (`paper_engine --replay-from/--to`, an
  isolated `--account-label`, 9 s): `volume_burst_1m` loose/no-trend +$248 PF 8.1 and
  `delta_div_1m` +$90..190 PF 2–3.5 on 16 trades each; absorption rarely fires; **book
  imbalance as a TRIGGER loses** (`bi_invert` fade 1 win in 16, follow likewise — variants
  removed) while as a GATE (`imbalance_max`, the `__inv` overlay) it is the fleet's best edge.
  Replay trades for ids not registered in `paper_strategies` are dropped (FK) — read the
  engine log's `EXIT` lines for scratch ids, or register them first.

- **Paper fleet research rules (2026-09-26)**: (1) **Any new variant gets a backfill replay
  the same day** — `build/paper_engine --config config/paper_fleet.json --account-label backfill
  --replay-from 'YYYY-MM-DD 09:25' --replay-to 'YYYY-MM-DD 16:05'` over the recorded sessions
  (~2 min per 600 strategies per week), then rank with
  `strategy_leaderboard(…, p_paper_label => 'backfill')` or the Leaderboard tab's **data:**
  selector. The forward test (label `tradeify`) is what the rotation decides on; a replay is how
  a strategy added on Saturday is judged on Saturday. Never replay under `tradeify`; the
  leaderboard reads only that label by default. (2) **Time of day is a first-class variable**:
  rank by slot (`p_handoff_only=true` with `p_slot_start/end`) before judging a mode — the same
  mode wins in one hour and loses in another. Windowed siblings are `__open` (09:30–10:00),
  `__am` (10:00–12:00); `__pm` was tested and dropped. (3) Tape modes (`volume_burst`,
  `delta_divergence`) work on **1-minute bars only**; chandelier / time-stop knobs are inert on
  1m scalps. (4) **SUPERSEDED 2026-09-29 — founder: "we want all to keep running for ever": every registered strategy runs (2,170 incl. the 09-28 research grids; 4,818 since 2026-10-01, when `scripts/fleet_add_variants.sh` gave every base the five `__rg_*` regime presets, `__rvol15` and `__tr25` — regime presets only on regular-hours bases); nothing is retired again, judge by the Leaderboard periods and the Week · month · year tab.** Historical note: the 7-day forward test had retired (`enabled:false`, rows kept) `nr7`, `supertrend`,
  `roc_momentum`, `trend_day`, every `globex_*` whole-day id and the book overlays `__micro __imb
  __all __sz __wait __bx __bbe` — 635 of 1,675 configured strategies run. `__inv` is the only
  overlay that beats its bases, and it does NOT combine with trend entries (it blocks them).
  (5) Golden replay is deterministic since the ORB cooldown moved to the engine clock; re-freeze
  (`make golden-freeze`) after every fleet change and read a FAIL by its diff. (6) **A restart is a
  warm-up, not a catch-up (2026-09-30)**: `paper_engine` replays the last `warmup_bars` (1600) completed
  1m bars with entries suppressed before going live, and logs "warm-up complete … N entry signal(s)
  suppressed"; signals fired while the engine was down are NOT taken. Every start lands in
  `paper_engine_runs`, and the leaderboard's `restart_sessions` flags sessions that began after a
  03:00 ET start — `qualifies` needs 0 of them. Paper costs are the measured live ones
  (`commission_rt` 1.82, `slippage_ticks` 3, stops fill at the crossing print); read `scratch_rate`
  next to `win_rate` (breakeven scratches count as wins) and `trade_set_size` before calling a variant
  distinct. Rows under `invalid_resume_*` / `invalid_warmup_*` labels are quarantined restart artefacts.
  (7) **Fleet expansion is STAGED, not live (2026-10-01, founder: "expand our fleet" — all four ways)**:
  `scripts/fleet_expand.sh` writes `config/staging/` only (gitignored, regenerate with `all` + `newmodes`):
  winners (673 exit grids + gates around the strategies that held up forward AND in replay), newmodes (124:
  the new trend modes `level_fade` / `vwap_reclaim` / `range_break`), markets (every NQ base mirrored to
  ES/YM/RTY/GC/CL as MES/MYM/M2K/MGC/MCL, point knobs × the market's range ratio to NQ, floors 8/4/1 ticks,
  ~1,610 each, labels es/ym/rty/gc/cl) and combos (12,031 cross-category filter pairs, label `nq_combo`).
  Ids are registered in `paper_strategies` under account_label `staging`. `replay` (research labels
  `exp_<name>`, refuses 09:25–16:05 ET weekdays — the live executor polls the same tick table) and
  `activate {nq|markets|combo|all} --yes` (merge / own unit `paper-engine-local-<code>` per
  `config/paper_fleet_<code>.json`, which the fleet audit and stack_status discover by that name). Activate
  only after a reload has proven the current load (no "Writer queue full" in the collector log).

**WARNING — Oracle failback:** before starting an executor on Oracle (failback or
deploy), kill any locally running executors for the same account. There is currently
**no cross-host single-writer guard**, so a local executor and an Oracle executor can
trade the same account simultaneously.

## Oracle deployment

Oracle VM: `170.9.233.177`, user `opc`, key `~/.ssh/id_ed25519`
Deploy: `make deploy` (builds locally, pushes git, SSH pulls + rebuilds on Oracle)
Production binaries on Oracle: `rithmic_engine`, `nq_executor`
**audit_daemon is local/testing ONLY — do NOT start it on Oracle.**
