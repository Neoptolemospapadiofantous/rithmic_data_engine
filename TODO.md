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
  notify time, run twice, diff. Until then `make hermes` can report a golden FAIL of that size —
  read the diff: only ORB re-entry rows shifting by minutes = this defect, anything else = real.

## Decided 2026-09-26 (not to do, for now)
- Retention: keep everything. Partitioning / TimescaleDB: leave as is.
