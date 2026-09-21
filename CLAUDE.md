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
- **`nq_executor`** — ORB strategy + order execution (production trader)
- **`audit_daemon`** — 17-check quality daemon (local/testing only — NOT on Oracle)
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

**WARNING — Oracle failback:** before starting an executor on Oracle (failback or
deploy), kill any locally running executors for the same account. There is currently
**no cross-host single-writer guard**, so a local executor and an Oracle executor can
trade the same account simultaneously.

## Oracle deployment

Oracle VM: `170.9.233.177`, user `opc`, key `~/.ssh/id_ed25519`
Deploy: `make deploy` (builds locally, pushes git, SSH pulls + rebuilds on Oracle)
Production binaries on Oracle: `rithmic_engine`, `nq_executor`
**audit_daemon is local/testing ONLY — do NOT start it on Oracle.**
