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
- **Live executors run on the collector's feed**: `RITHMIC_MD_PROVIDER=pg` in
  `.env.<account>` (set for `tradeify`) makes `nq_executor` open no MD session and poll
  the Postgres `ticks` table instead (`md_feed_symbol`, default NQ). Do not start an
  executor in WebSocket MD mode (any other provider) while the collector runs — Rithmic
  force-logs-out one of them every ~35s.
- **ORB → hand-off engine (tradeify, since 2026-09-25)**: ORB (`tradeify`) opens 09:30 ET;
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
  1m scalps. (4) **SUPERSEDED 2026-09-29 — founder: "we want all to keep running for ever": every registered strategy runs (2,170 incl. the 09-28 research grids); nothing is retired again, judge by the Leaderboard periods and the Week · month · year tab.** Historical note: the 7-day forward test had retired (`enabled:false`, rows kept) `nr7`, `supertrend`,
  `roc_momentum`, `trend_day`, every `globex_*` whole-day id and the book overlays `__micro __imb
  __all __sz __wait __bx __bbe` — 635 of 1,675 configured strategies run. `__inv` is the only
  overlay that beats its bases, and it does NOT combine with trend entries (it blocks them).
  (5) Golden replay is deterministic since the ORB cooldown moved to the engine clock; re-freeze
  (`make golden-freeze`) after every fleet change and read a FAIL by its diff.

**WARNING — Oracle failback:** before starting an executor on Oracle (failback or
deploy), kill any locally running executors for the same account. There is currently
**no cross-host single-writer guard**, so a local executor and an Oracle executor can
trade the same account simultaneously.

## Oracle deployment

Oracle VM: `170.9.233.177`, user `opc`, key `~/.ssh/id_ed25519`
Deploy: `make deploy` (builds locally, pushes git, SSH pulls + rebuilds on Oracle)
Production binaries on Oracle: `rithmic_engine`, `nq_executor`
**audit_daemon is local/testing ONLY — do NOT start it on Oracle.**
