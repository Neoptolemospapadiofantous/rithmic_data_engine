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

## Stack
Everything is C++. There is no Python in this project.

- **`rithmic_engine`** — WebSocket tick collector (market data → PostgreSQL)
- **`nq_executor`** — ORB strategy + order execution (production trader)
- **`audit_daemon`** — 17-check quality daemon (local/testing only — NOT on Oracle)
- **`dashboard`** — ncurses TUI
- **Build**: `cmake -B build && cmake --build build -j$(nproc)`
- **Tests**: `build/test_orb_strategy`, `build/test_risk_manager`, `build/test_validator`, `build/test_db`

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
| `src/execution/order_manager.cpp` | Order lifecycle management |
| `src/execution/risk_manager.cpp` | Pre-trade risk checks |
| `src/client.cpp` | WebSocket client (Boost.Beast) |
| `src/db.cpp` | PostgreSQL I/O (libpq) |
| `src/collector.cpp` | Tick collector pipeline |
| `config/live_config.json` | Runtime config (do not edit) |
| `deploy/nq_executor.service` | Systemd unit for Oracle production |
| `deploy/nq_executor@.service` | Systemd template unit |

## Oracle deployment

Oracle VM: `170.9.233.177`, user `opc`, key `~/.ssh/id_ed25519`
Deploy: `make deploy` (builds locally, pushes git, SSH pulls + rebuilds on Oracle)
Production binaries on Oracle: `rithmic_engine`, `nq_executor`
**audit_daemon is local/testing ONLY — do NOT start it on Oracle.**
