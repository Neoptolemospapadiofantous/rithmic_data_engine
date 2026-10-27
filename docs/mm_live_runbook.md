# mm_live runbook — the live Lighter market maker

`mm_live` quotes ONE Lighter perp market on MAINNET with $11 post-only orders under the $5-pilot risk limits
(`src/dex/live/risk.hpp`: 1 market, 1 quote of inventory each way on worst-case exposure, gross $12, daily stop
−$1.50, total stop −$2.50 from the persisted high-water mark, leverage 5). Testnet is never used: the testnet host is
refused by name. Code: `src/dex/mm_live_main.cpp`, `src/dex/live/` (engine, account feed, sender, REST, journal,
alerts, dead-man, reconciler). Tests: `tests/dex/test_mm_live.cpp` (offline only).

> **What `max_gross_usd` measures.** The cap is on WORST-CASE net exposure, not on the sum of everything resting:
> max(position + Σ resting bids, −position + Σ resting asks), valued at the higher of the order price and the mid. A
> bid and an ask cannot both add to one position, so with the pilot default of $12 the engine quotes BOTH sides at $11,
> and after a fill the closing order on the other side stays allowed (it lowers the worst case) while the order that
> would add to the position is refused. `24` permits two quotes' worth of net exposure in one direction (with
> `max_position_quotes` 1 the position cap still binds at one quote). Default 12; the founder chooses.

## 1. Setup (once)

### 1.1 The API key file — never echoed, 0600, outside every git tree

The key is a Lighter API key (index 4–254) registered in the Lighter app for the dedicated pilot sub-account. Never the
L1 wallet key.

```bash
mkdir -p ~/.config/lighter && chmod 700 ~/.config/lighter
ACCT=<account_index>; IDX=<api_key_index>
( umask 077
  read -rs -p "Lighter API private key (input hidden): " K; echo
  printf '{"network":"mainnet","account_index":%s,"api_key_index":%s,"private_key":"%s"}\n' "$ACCT" "$IDX" "$K" \
    > ~/.config/lighter/mainnet.key
  unset K )
stat -c '%a %U %n' ~/.config/lighter/mainnet.key        # must print: 600 theone …
```

`read -s` keeps the key off the screen and out of the shell history; `printf` is a bash builtin, so the key is never
in any process's argv. mm_live refuses the file if it is not mode 0600, not owned by you, a symlink, under a symlinked
directory, inside the repo or inside any git work tree, or if its account/key index differ from the config.

### 1.2 Leverage — set by hand in the Lighter app

The engine NEVER changes leverage (`SignUpdateLeverage` is not even resolved from the signer library). Set the
market's leverage in the app to the config's `risk.leverage` (pilot: 5× = initial margin fraction 20 %). mm_live reads
the market's IMF from the account and refuses to quote while it differs (`halt: leverage`); the probe report prints the
IMF it read.

### 1.3 Config — `config/mm_live_<name>.json`

```json
{
  "mode": "live",
  "host": "mainnet.zklighter.elliot.ai", "chain_id": 304,
  "account_index": 0, "api_key_index": 4,
  "key_file": "/home/theone/.config/lighter/mainnet.key",
  "arming_file": "/home/theone/Desktop/rithmic_engine/data/mm_live/ARM_MAINNET",
  "no_deploy_file": "/home/theone/Desktop/rithmic_engine/data/mm_live/NO_DEPLOY_MM",
  "state_dir": "/home/theone/Desktop/rithmic_engine/data/mm_live/state",
  "market": "AAVE",
  "risk": {"quote_usd": 11, "max_order_usd": 12, "max_position_quotes": 1, "max_gross_usd": 12,
           "daily_stop_usd": -1.5, "total_stop_usd": -2.5, "leverage": 5},
  "engine": {
    "market_spec": {"market_index": 27, "size_decimals": 3, "price_decimals": 3, "min_base": "0.045", "min_quote": "10"},
    "hl_sz_decimals": 2,
    "journal_dir": "/home/theone/Desktop/rithmic_engine/data/mm_live/journal",
    "status_file": "/home/theone/Desktop/rithmic_engine/data/mm_live/status.json",
    "max_collateral_usd": 50
  }
}
```

`market_spec` is checked against `GET /orderBookDetails` at start (a difference refuses to trade). `hl_sz_decimals` is
Hyperliquid's size precision for the coin — the frozen strategy's tick unit (`mm_paper.hpp`). The probe uses the
same file with `"mode": "probe"`. Unknown keys anywhere are refused.

### 1.4 Validate without touching the network

```bash
build/mm_live --config config/mm_live_aave.json --mode live --check-config     # opens no socket
```

## 2. Arming

probe and live need `data/mm_live/ARM_MAINNET` containing `<account_index> <YYYY-MM-DD>` (today, ≤ 7 days old),
written BY HAND, and no `NO_DEPLOY_MM`. The arming file is re-checked before every request: removing it (or creating
`NO_DEPLOY_MM`) stops new orders immediately; cancels still go out.

## 3. First start and the probe

```bash
build/mm_live --config config/mm_live_aave_probe.json --mode probe --init-risk     # FIRST start only: --init-risk
cat data/mm_live/state/probe_report.json
```

`--init-risk` creates the persisted risk baselines; without it a missing `risk_state.json` is refused (deleting the
file can never reset the loss stops). The probe: cancel-all + adopt position → one post-only BID ≥ 20 % below the best
bid at the minimum size → ack → cancel → REST shows 0 open orders → one dead-man re-arm (abort + scheduled cancel-all)
confirmed by the sequencer → clean stop (cancel-all, dead-man abort). Exit 0 = PASS. The report records the IMF read,
ack and cancel latency, the dead-man deadline. Anything else → do not go live; read the journal.

## 4. Go live

1. Probe PASS on the same day. Leverage verified (probe report `imf`). Collateral ≤ `max_collateral_usd`.
2. `cp deploy/mm-live@.service ~/.config/systemd/user/ && systemctl --user daemon-reload`
3. `systemctl --user start mm-live@aave` (do not `enable` during the pilot).
4. Watch: `tail -f data/logs/mm_live_aave.log`, `cat data/mm_live/status.json` (written every 5 s: quoting / quoting_block,
   halts, position vs exchange position, equity, day P&L, orders, nonce, requests/min, dead-man deadline, stream state).
5. First hour supervised: compare one place/cancel cycle with the Lighter UI.

## 5. Stop, kill, pause, recovery

| Action | How | What happens |
|---|---|---|
| Stop | `systemctl --user stop mm-live@aave` (SIGTERM) | stop quoting → cancel-all → wait ≤ 10 s → the dead-man is aborted ONLY if the cancel-all was applied (server nextNonce past it) AND REST shows 0 open orders; otherwise it is LEFT ARMED (the exchange cancels whatever remains at the deadline), the alert says `NOT clean … dead-man LEFT ARMED`, exit 1. Position kept. Verify with `pgrep -x mm_live` (never `pkill -f`). |
| Kill | `touch data/mm_live/state/KILL` | sticky halt, cancel-all, flatten with reduce-only IOC capped 0.5 % through the touch (≤ 5 tries, then an alert "FLATTEN FAILED"). Deleting the file does NOT resume: the halt is also persisted (`halt_kill: true` in `<state_dir>/risk_state.json`). To resume: stop the unit, investigate, `rm <state_dir>/KILL`, set `halt_kill` to false and `reason` to "" in risk_state.json (keep mode 0600; nothing else in that file may change — it holds the loss-stop baselines), restart. |
| Pause | `touch data/mm_live/state/PAUSE` | quotes cancelled, position kept; delete the file to resume. |
| Crash / kill -9 / box down | nothing | the exchange's scheduled cancel-all fires ≤ 5 min 15 s after the last re-arm; post-only quotes also expire after 10 min. On restart: cancel-all, wait for 0 open orders, ADOPT the exchange position, re-arm. |
| Restart loop | systemd | 3 starts in 10 min stop the unit (StartLimitBurst). |

Recovery checklist after any unplanned stop: `status.json` (last state), the journal (`data/mm_live/journal/
lighter_main-<day>.jsonl`: every intent, signed tx hash + nonce, send result, ack, cancel, fill, reconcile decision,
halt), the Lighter UI (open orders, position). A sticky halt (kill, risk_stop, journal, unowned, mismatch_repeat,
self_trade, liquidation, standard_limit, unconfigured_position, capital_guard, nonce, two_orders) never clears by itself:
fix the cause, restart. Loss-stop halts persist in `risk_state.json` across restarts (the daily one until 00:00 UTC).

## 6. Alerts (grid-notify → Telegram; rate-limited per key, 5 min)

| Alert | Meaning / action |
|---|---|
| `start …` / `stop (…)` | process lifecycle; `stop … NOT clean` = orders may remain — check the UI now |
| `refusing to trade: …` | startup refused (metadata, capital guard, position in another market, cancel-all not confirmed, journal) — exits 1 |
| `adopted exchange position X` | a position existed at start; it is inventory now (quoting only the reducing side if above the cap) |
| `HALT <reason>` / `resumed after <reason>` | quoting stopped / restarted; `(sticky)` = needs a human |
| `position mismatch: local … vs exchange …` | the exchange position differed for > 3 s; adopted the exchange's, quoting halted until two consistent reconciles; 3 in a UTC day → sticky |
| `account used by something else …` | an order/fill not placed by any run of mm_live — cancelled, sticky halt. Someone traded the sub-account by hand. |
| `an open order from run N is still on the book` | a leftover of an earlier run — cancelled |
| `order N Unknown for more than 30 s` | no ack/cancel confirm and REST has not resolved it — check the UI |
| `send outcome unknown …` / `HALT nonce_frozen` | a sendTx got no answer: nonces frozen until nextNonce + reconcile decide (identical bytes re-sent if needed) |
| `invalid nonce from the API` | our nonce view was wrong; resynced |
| `sequencer rejected nonce N before applying` | that tx and every later one failed; orders reconciled, nonces resynced |
| `rate limited (429/405) …` | nothing sent for 60 s; > 3 in 10 min → sticky `rate_limit_burst` |
| `dead-man re-arm failed …` | a re-arm was not confirmed (one alert per episode). The previous confirmed schedule stays armed; quoting continues only while a CONFIRMED deadline is ≥ 60 s ahead, otherwise `HALT deadman` and the quotes are pulled |
| `JOURNAL FAILURE …` | the journal cannot be written (disk full / IO): sticky halt, cancel-all. Free space, restart. |
| `HALT kill` / `flat after N IOC(s)` / `FLATTEN FAILED …` | KILL file path; on FLATTEN FAILED close the position by hand |
| `HALT risk_stop …` | daily or total loss stop — flattens; daily clears at 00:00 UTC, total needs a human |
| `HALT leverage …` | the market's IMF ≠ config leverage (or unreadable): set it in the app |
| `HALT capital_guard` | collateral above `max_collateral_usd`: wrong/funded account |
| `HALT standard_limit` | Lighter 52002 — Standard trading limit reached; only reducing is possible |
| `first fill of the day …` / `day …: N fills …` | daily heartbeat |
| `probe PASSED / FAILED …` | probe result |

Silence is not health: `grid-notify --check` before going live.

## 7. Offline soak (no network)

```bash
build/mm_live --config <offline config> --mode offline --archive /media/theone/381C48C51C48803C/rithmic_dex/lighter/AAVE/2026-10-25.jsonl.zst
```

Replays a recorded day through the whole pipeline against the in-process fake exchange (state in
`<state_dir>/offline`, alerts to the log) and exits 0 only if journal, engine and fake exchange agree at the end.

## 8. Mainnet facts the offline tests cannot prove (the probe and the first live hour must)

* Scheduled cancel-all: the 5-minute minimum (`MinOrderCancelAllPeriod`), and what a second schedule does while one
  is queued. The engine first sends the new schedule ALONE ("replace" mode: if it fails, the old one stays armed). If
  Lighter answers 21712 ("account has a queued cancel all orders request") it switches to abort + schedule in one
  request ("abort_first": the only way left to extend). In abort_first mode quotes are pulled from the moment the
  [abort, schedule] request is sent until the new schedule's code-0 result is seen — a short quoting gap every 20 s,
  accepted so the engine never quotes without a confirmed schedule; an abort that settles without its own result is
  read as executed (nothing armed). Dead-man health comes ONLY from a Scheduled tx's own sequencer result with code 0.
  The probe re-arms once and reports `deadman_mode` — read it before going live.
* Whether a cancel-all / scheduled cancel-all error (21712/21713/21714) consumes the nonce. The code assumes nothing:
  the stream freezes and nextNonce decides (a known rejection is never resent).

## 9. Other rails

* Two instance locks: `<state_dir>/mm_live.lock` and, for probe/live, `~/.local/state/mm_live/acct<A>_key<K>.lock`
  (two configs on one API key can never both run).
* Rate budgets survive a restart (`<state_dir>/rate_window.json`): all requests ≤ `rate_budget_per_min` (54) and sendTx
  alone ≤ `engine.send_budget_per_min` (default 30, under the unconfirmed per-tx-type 40/min).
* Loss stops run on REST total asset value (incl. unrealised), read every reconcile — the same measure as the start
  baseline. user_stats feeds only the capital guard and margin usage.
* A step that blocks the engine thread > 2 s (slow REST, WebSocket open) pulls the quotes for 10 s (`HALT stall`).
* KILL flattens even when disarmed (reduce-only IOC passes the arming gate like cancels).
* The `account_tx` / `account_all_orders` message shapes, the `ae` error format, the `accountActiveOrders` /
  `accountInactiveOrders` / `trades` REST shapes and pagination, and that `account` positions carry
  `initial_margin_fraction` for a market with no position.
* Whether a live subscription survives token expiry (renewal re-subscribes every 6 h regardless).
* That a batch counts as one request against the 60/min Standard limit; 429/405 behaviour.
* The v1.1.6 signer's signatures being accepted by the current sequencer.
