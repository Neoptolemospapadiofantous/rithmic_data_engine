# NQ/MNQ ORB — Operator Runbook

Authoritative checklist for the C++ executor lifecycle: dry-run baseline, Oracle deploy, go-live, and emergency procedures.

---

## Prerequisites

- [ ] `make hermes` fully green (0 FAIL, 0 WARN)
- [ ] `.env` present with valid `PG_*`, `RITHMIC_TRADEIFY_*` (plus `.env.<account>` per account, e.g. `.env.tradeify1`)
- [ ] Oracle VM reachable: `ssh opc@170.9.233.177 -i ~/.ssh/id_ed25519`
- [ ] Prop firm account funded (check portal)
- [ ] Per-account config validated (e.g. `config/tradeify_config.json`): `trade_route="simulator"`, correct `symbol`, `account_id`

Configs are per-account: systemd instance `%I` maps to `config/%I_config.json` and `.env.%I` (see `deploy/nq_executor@.service`). `config/live_config.json` is frozen — no binary reads its `dry_run` flag.

---

## Step 1 — Verify dry-run baseline (5 sessions)

Set `dry_run: true` in the per-account config (e.g. `config/tradeify_config.json`), then run locally:

```bash
./build/nq_executor --config config/tradeify_config.json
```

Local mode: the dashboard (http://localhost:3000) launches the backend (`:8080`, `CPP_LOCAL=1`), collector, and executor as `nohup` subprocesses — no systemd locally. See "Local mode" in CLAUDE.md.

For each session confirm in `data/logs/nq_executor.log`:
- `ORB_BUILDING` fires at session open (`session_open_hour:session_open_min`)
- `WATCHING` fires after `orb_minutes` of range building
- Signal fires on breakout bar close outside range
- `trade_open` logged with correct SL and target
- `trade_close` logged on SL/target hit or EOD
- EOD flatten logged and the day's `live_trades` rows all closed (`exit_time` set) — there is
  no `session_summary` table any more (dropped 2026-09-26, it was never written)

---

## Step 2 — Run Hermes (full check)

```bash
make hermes
```

All 9+ checks must PASS. Fix any FAIL before proceeding. The `trading_constants` WARN (trail_be_offset) is a config decision — document it but it does not block deploy.

---

## Step 3 — Deploy to Oracle VM

```bash
make deploy
```

This:
1. Runs `make hermes-fast` — aborts if not clean
2. Pushes git to remote
3. SSH pulls + rebuilds on Oracle (`cmake --build`)
4. Copies `deploy/nq_executor@.service` and `deploy/nq_executor_24x7@.service` to `/etc/systemd/system/`
5. Runs `systemctl daemon-reload`

---

## Step 4 — Promote to live

Edit the per-account config (e.g. `config/tradeify_config.json`) — set `dry_run: false`. Then redeploy:

```bash
make deploy
```

> **Rollback:** Set `dry_run: true` in the same per-account config and `make deploy` again.
> Do not edit `config/live_config.json` — it is frozen and its `dry_run` flag is not read by any binary.

---

## Step 5 — Start the service on Oracle

The systemd instance name is the account label: `%I` → `config/%I_config.json` + `.env.%I`.
For the Tradeify account, standard RTH session (09:30–16:00):
```bash
ssh opc@170.9.233.177
sudo systemctl enable --now nq_executor@tradeify
sudo systemctl status nq_executor@tradeify
```

For 24×7 mode:
```bash
sudo systemctl enable --now nq_executor-24x7@tradeify
```

(The two units conflict with each other per instance — never run both for the same account.)

Expected within 60 s:
- `startup complete — entering trading loop`
- `position_reconciliation: no open position found` (clean start)
- systemd: `Active: active (running)`

---

## Step 6 — Monitor first live session

**Live logs on Oracle VM:**
```bash
journalctl -u nq_executor@tradeify -f
```

**Key events:**

| Event | Meaning |
|-------|---------|
| `position_reconciliation: no open position` | Clean start |
| `position_reconciliation: found open position` | Restarted mid-trade — state restored |
| `trade_open ... dry_run=FALSE` | Real order submitted |
| `trade_close ... reason=SL_OR_TARGET` | Stop or target hit |
| `EOD: flattening all positions` | Clean end-of-day |
| `emergency_flatten` | SIGTERM received — flatten initiated |
| `Daily trade limit reached (N/N) — shutting down` | Max trades hit, clean shutdown |

---

## Local: which strategy is live, and switching it

One executor per account (the instance lock is account-wide). The live strategy is chosen by
WHICH unit instance runs; each instance reads `config/<instance>_config.json` and `.env.<instance>`
(symlink to `.env.tradeify`).

| Instance | Config | Strategy |
|---|---|---|
| `nq-executor-local@tradeify` | `tradeify_config.json` | ORB, 09:30 ET, 2 MNQ (production) |
| `nq-executor-local@tradeify_trend` | `tradeify_trend_config.json` | trend engine (`"engine": "trend"`, `mode` + params) |
| `nq-executor-local@tradeify_orbtest` | `tradeify_orbtest_config.json` | ORB on an 18:05 ET range (test only) |

Switch (only when FLAT, between sessions): `make stack-status` → `systemctl --user stop
nq-executor-local@<old>` → `pgrep -x nq_executor` is empty → `systemctl --user start
nq-executor-local@<new>` → within ~5 s the log shows `PNL_PLANT position subscription OK` and a
`[BROKER] … exchange_net=0` line. No `[BROKER]` line = no position truth = stop it. Pick a trend
variant from the paper fleet (`/strategies`); copy its `params` into the trend config and give it
the RTH window (`win_start 930`, `win_end 1555`, `eod_flatten 15:56`). Modes `rs_continuation` and
`book_imbalance` are refused (no ES/BBO feed in the executor).

Daily 09:00 ET: `pre-rth-check.timer` sends a readiness line to Telegram. Before trading, keep
Chrome under its cap: quit it fully and relaunch from the dock (`scripts/chrome-capped`) —
`make stack-status` shows how much Chrome sits outside `browsers.slice`.

Overnight test (`scripts/overnight_live_test.sh`, schedule with
`systemd-run --user --on-calendar='YYYY-MM-DD 01:01:00' --collect -p WorkingDirectory=$PWD
$PWD/scripts/overnight_live_test.sh tradeify`): drill → ORB 18:05 ET → trend 19:00–20:00 ET →
production. Log `data/logs/overnight_live_test.log`; progress + result on Telegram.

---

## Emergency procedures

### Stop immediately
```bash
# Via systemd (Oracle)
sudo systemctl stop nq_executor@tradeify

# Local mode: kill the nohup'd executor directly
pkill -SIGTERM -f 'nq_executor --config'

# Or direct signal if systemd unresponsive
kill -SIGTERM $(pgrep nq_executor)
```

SIGTERM triggers: `g_running=false` → flattens open position → flushes DB → exits.

### Revert to paper mode
Edit the per-account config (e.g. `config/tradeify_config.json`) — set `dry_run: true` — then `make deploy` and restart the service (Oracle), or restart the executor from the dashboard (local).

### Block restarts (NO_DEPLOY lockfile)
```bash
# On Oracle VM
touch ~/rithmic_engine/NO_DEPLOY

# Clear it
rm ~/rithmic_engine/NO_DEPLOY
```

The audit daemon and service `ExecStartPre` check for this file and refuse to start if present.

### Check Oracle service logs since last boot
```bash
journalctl -u nq_executor@tradeify -b
```

---

## Definition of done (before first live trade)

- [ ] 5 clean dry-run sessions completed (RTH open to EOD flatten)
- [ ] `make hermes` passes (0 FAIL)
- [ ] Position reconciliation tested: restart mid-session, confirm state restored
- [ ] `trade_route` confirmed as `"simulator"` (never `"Rithmic Order Routing"`)
- [ ] `dry_run: false` set in config — verified in startup log
- [ ] Oracle VM service starts clean with `Active: active (running)`
