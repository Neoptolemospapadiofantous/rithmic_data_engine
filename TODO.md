## Build queue 2026-09-28 (founder: "start getting data from more instruments" + "do all you said")
Status legend: [ ] todo · [~] in progress · [x] done. Each item: tests + gate green, backfill replay where a strategy is involved.
- [x] 16. Volume / volatility filters: rvol gate + vol-targeted size (generic, both sides), atr_break + vprofile modes, 90-variant matrix in the fleet (label volgrid). Verdicts on six sessions: rvol ≥1.5 helps per contract, quiet-tape gate hurts, sizing is size only, new modes flat/negative.
- [x] 15. `beta_on_1800` (+__htf, __wide, es_) — the Tradeify-legal overnight hold; found and fixed the 18:00 rollover-vs-gap-reset ordering bug in paper_main (every 18:00-reopen strategy was mis-counting its day).
- [x] 14. Fleet audit triage (founder pasted 4 FAILs): resume catch-up bug fixed in both paper brokers (+test); audit checks re-scoped (exit-side overlays exempt from the subset check, coverage/KPIs on the fleet label, new exit-after-entry check); insights KPIs fleet-label only. The 14 impossible rows now sit under label `invalid_resume_20260923` (reversible); audit 0 fail / 1 warn / 57 ok.
- [x] 13. Beta exposure (founder: "for now add the rest in leaderboard rotation"): trend mode `hold` (clock-time entry, window-end exit, survives the 18:00 reset; day filters hold_tom / hold_pre_event / hold_up_day; paper-broker windows wrap midnight). Eight NQ variants (beta_on, __htf, _tom, _prefomc, _short, beta_close, beta_day, __htf) + three ES. Paper only: Tradeify forbids overnight holds and the live EOD logic does not wrap. Six-session replay is noise (overnight long −$433, short +$224, day long +$649). Known: a Friday entry exits at the first tick after the weekend gap.
- [x] 12. Founder 2026-09-29: ALL 2,170 registered strategies run for ever (09-26 retirements re-enabled, research grids added to the fleet; 4.5 % CPU); Leaderboard gains this month / this year / 90d / 365d; new `Week · month · year` tab (`/api/cpp/strategies/periodic`: per-strategy net per calendar period, +periods count, search/sort/paging).
- [x] 11. Strategies page: registered ids that are not in `config/paper_fleet.json` show as status `research` (hidden unless the status filter says research) and leave the fleet count — 664/1704 fleet + 466 research, not 664/2170 (2026-09-29).
- [ ] 10. Follow-ups from the queue: trail-width sweep (25/35/50) on the top 24 bases; decide retention before the 8-instrument tick volume fills the disk (~2 months); judge the 29 forward-test keepers (`__rg_*`, `__tp*`, `__tr25`, gap_fade, news_break, orb_retest, `es_*`) after ≥2 weeks on the Leaderboard.
- [x] 1. Collector records 8 instruments since 2026-09-28 22:47 EEST: NQ (depth) + ES, RTY, MNQ, MES, YM:CBOT, CL:NYMEX, GC:COMEX (`RITHMIC_EXTRA_SYMBOLS`, `SYMBOL:EXCHANGE` form new in config.hpp); contracts rows for MES/RTY/YM (quarterly); CL/GC roll monthly — not modelled. Disk 93 % (66 GB free): budget ~2.5x today's ~110 MB/day of ticks — revisit retention within ~2 months.
- [x] 2. Fixed take-profit (`tp_points` / `tp_r`): built both sides, parity-tested (same tick, same reason). Grid verdict: a fixed target LOSES on average at every R (it caps the trailing winners); 3R ≈ neutral on classic ORB only. Five ORB `__tp*` keepers forward-test; do not put a target on fib/trend bases.
- [x] 3. `orb_retest` mode built + tested; grid: flat-to-negative on six sessions (retests mostly scratch at BE). One variant forward-tests.
- [x] 4. `gap_fade` mode built + tested (strategy-owned `gap_filled` exit); grid: 2 gap days, 2 winners (+$124) — too few to judge; forward-tests.
- [x] 5. `news_break` mode built + tested, `set_event_day` fed from `calendar` (fomc/nfp) in paper_main + executor; grid: 14:00 5/5 small winners, 08:30 flat (needs `session_open_hour` 8 in params); event-only variant forward-tests (first NFP 2026-10-02).
- [x] 6. `ib_break` mode built + tested (strategy-owned `ib_target` exit); grid: negative (PF 0.35) — not kept.
- [x] 7. ES mirror LIVE 2026-09-28 23:06 EEST: `config/paper_fleet_es.json` (24 top NQ bases as MES $5/pt, point knobs × 0.144 = ES/NQ ATR ratio, ids `es_*`, label `es`), unit `paper-engine-local-es.service` (deploy/ + installed). Leaderboard data: label `es`; NOT on the Fleet tab (one fleet config).
- [x] 8. Exit-side regime built both sides (`regime_exit_min_eff`, `trail_step_trend/_range`, `tp_r_range`), parity-tested. Grid 8/8 better with trend trail 25 — but the plain-trail-25 control beat it on 7/8: the win is the WIDER TRAIL. Eight `__tr25` keepers forward-test. Follow-up: trail sweep 25/35/50; propose trail 25 for the live hand-off (founder's call).
- [x] 9. Combined book-fade + regime variants: backfilled (label combo) — 1–2 trades each, all losers; the gates block each other. Not kept (rows disabled).

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
  notify time, run twice, diff.
- 2026-09-28 evidence: two binaries (before/after the regime gate) replayed the golden window
  byte-identically, twice each (504 trades) — the drift is NOT code-path timing inside one run.
  The file frozen at 20:11 had 408; `bbo` autovacuumed at 21:03 in between (ticks autoanalyze
  20:34). First divergence = the FILL price of `orb_t1030_2nd_5`'s 14:35:28.500174 entry
  (30953.75 frozen vs 30954.00 fresh): that microsecond holds ticks (seq 0, 30954), (seq 0,
  30954.25), (seq 1, 30954), (seq 1, 30954.25)… — `ORDER BY ts_event, seq, price, size` is total
  over them, so the order should not change; suspect the page-boundary guard / a plan change
  after (auto)vacuum instead. Reproduce: freeze, `VACUUM ANALYZE ticks, bbo`, replay, diff. Until then `make hermes` can report a golden FAIL of that size —
  read the diff: only ORB re-entry rows shifting by minutes = this defect, anything else = real.

## Decided 2026-09-26 (not to do, for now)
- Retention: keep everything. Partitioning / TimescaleDB: leave as is.
