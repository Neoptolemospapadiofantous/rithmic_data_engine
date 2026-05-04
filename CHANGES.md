# Changelog

All notable changes to rithmic_engine are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Dates are in ISO-8601 order (newest first).

---

## [Unreleased]

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
