# Changelog

All notable changes to rithmic_engine are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Dates are in ISO-8601 order (newest first).

---

## [Unreleased]

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
