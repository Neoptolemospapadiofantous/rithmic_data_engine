# Lighter Market Maker: Architecture and Shipping Plan

Written 2026-10-25. The shareable copy with drawings is the Claude Doc "Lighter Market Maker: Architecture
and Shipping Plan" (https://claude.ai/artifact/1pp2WBmAXg6H5DZmdNeSCg). Companions: `docs/market_making_system.md`
(how it works today), `docs/market_making_plan.md` (gates and decision log).

## 1. Executive summary

The system is a passive market maker for small perpetual futures on Lighter. It quotes one bid and one ask
around the mid, earns part of the spread on each fill, and steps aside when price is about to run over it.
The frozen setting was chosen on a year of history and judged on a second year it never saw: +$446 over 12
sample days (mean day +$37, 8 of 12 positive, daily Sharpe 0.68), and +$341 on 15 coins never used in tuning.
Today it runs 24/7 on paper against Lighter's live books, with no key and no order path.

Shipping means a small server in the same cloud region as Lighter's matching engine, running the recorder, the
paper engine and later the live engine, while the home box keeps research, history and the daily overview. The
move is worth about 100 ms of the 300 ms assumed order delay (stress tests: 100 ms +$750 vs 300 ms +$446 on the
same days). The 200 ms that Lighter's Standard account imposes stays wherever the server sits.

Nothing goes live until G1 (Lighter re-test) and G2 (paper) pass; then a $250 pilot (G3), then capital in steps
to $10,000 (G4). Gates have fail rules as well as pass rules. What makes the result durable is the process:
pre-registration, holdout tests, KPIs with thresholds, and a regression suite (tests, golden replay, live-vs-
replay parity) before every deploy. The 25 October validation found a real defect this way (Lighter's price grid
differs from Hyperliquid's on 8 of 24 markets).

## 2. Objective, constraints and current state

| Constraint | Value | Why |
|---|---|---|
| Venue | Lighter, Standard account | 0 fees; the edge (~+0.5 bps) dies at 0.5 bps of fee |
| Capital | Founder's own | no prop firm trades crypto |
| Size | $100 quotes, $300 cap per market, 24 markets | what the backtests measured; capacity is thin |
| Delay assumed | 300 ms | 200 ms venue + network; 1 s loses |
| Tick unit | Hyperliquid ticks, orders rounded to Lighter's grid | the unit every backtest used |
| Changes | pre-registered → holdout → paper → live | nothing skips a step |

Running today (home box, systemd user units): `mm-paper-local`, `lighter-recorder-local`, `hl-recorder-local`,
`mm-gates.timer` (00:10 UTC). Not yet: Lighter account/key, order signing, live engine, server, BTC-lead pause,
hedge, `mm_opt`/`mm_robust` on Lighter archives (tick-rule option).

## 3. Architecture

Two sites, one rule: the server near the exchange runs what must be fast and always on; the home box runs what
thinks. Data flows venue → server; archives and the Postgres replica flow to the home box nightly; only a
versioned deploy flows forward.

Server (Phase 4): `lighter_recorder` (archive), `mm_paper` (control run, same binary), `mm_live` (new; same
strategy and risk code, orders via the signer), signer sidecar (new; the only key holder), Postgres (results +
audit, replicated nightly), `guard.timer` (watchdog: stale feed, engine down, orphan position, clock drift).

Home box: `hl_recorder`, Tardis history, `mm_sim`/`mm_opt`/`mm_robust`, `mm_gates.timer`, dashboards, deploy.

Formats unchanged across sites: archive lines `{"r": <recv µs>, "m": <raw message>}`, zstd per UTC day, each
Lighter day file opening with a synthetic snapshot. One shared header (`src/dex/hl_mm.hpp`) for the fill model
and strategy: paper, live and research never diverge.

## 4. Strategy and fill model

Quote 5 Hyperliquid ticks either side of the mid (`max(10^(floor(log10 p) − 4), 10^−(6 − szDecimals))`), only
when spread ≥ 4 ticks; imbalance pull 0.6; 2 s pause after a ≥ 2-tick jump; keep queue place for ≤ 1-tick
target changes; no skew; $100 / cap 3; 0 fees; 300 ms. Frozen in `mmpaper::frozen_params`.

Fill model: join the back of the level; trades at our price eat the queue first, trades through fill us whole;
pessimistic cancel bound; markouts at 1/5/10/30/60 s. Load-bearing: the jump pause, the delay, the fee.

## 5. Shipping plan: the server near Lighter

Today from Cyprus: 53 ms round trip, 142 ms exchange→box lag. Both Lighter and Hyperliquid resolve to AWS
CloudFront edges in Frankfurt from here (nearest edge, not the matching engine).

1. Pick the region (half a day, ~$5): one-hour VPS in AWS Frankfurt, N. Virginia, Tokyo, Singapore; run
   `lighter_recorder` 10 min each; lowest median (receive − exchange timestamp) wins; tie → Frankfurt.
2. Provision (one day): 2 vCPU / 4 GB / 80 GB NVMe + 200 GB volume at /data; Ubuntu 24.04, chrony; same
   toolchain; Postgres 16 on localhost; SSH keys only, fail2ban, ufw from home IP, unattended security updates;
   ~$25–40/month.
3. Deploy like Oracle: `make deploy-mm` = tag, push, pull, build, run both test suites, install units, restart;
   a failing test stops before restart; the tag is written to the engines' start events.
4. Parallel run (7 days): server recorder + paper beside the home copies; archives must agree and paper fills
   within the measured live-vs-replay tolerance (parity gate).
5. Cutover: home `mm-paper-local` stops; home recorder stays as the second archive copy; gate job reads the
   server replica.
6. Live (after G1 + G2, founder decision): signer + `mm_live` on the server; key created there, never leaves.

## 6. The live path

Same code as paper; where paper asks the fill model, live asks the exchange; paper keeps running as the shadow
(G3 compares them). One Standard account with the pilot's $250; key root-owned, readable by the signer only.
Signing: Go sidecar (documented exception to the C++ rule, ~300 lines) or a C++ port verified against the
official SDK's vectors; founder's call. `mm_live` emits intents (place post-only / cancel) with client ids;
acknowledgements, fills, positions from the account stream → `mm_live_orders/_fills/_positions`.

Controls in firing order: pre-trade check ($300/market, gross cap, 2 open orders/market) → rate limit (never drop
a cancel) → dead man's switch (signer cancels all after 5 s without heartbeat) → feed down (15 s market data /
30 s account stream → cancel all) → reconciliation every 10 s (mismatch → cancel, flatten, halt, alert) → daily
loss stop (pilot −$25; IOC flatten, halt to 00:00 UTC) → kill file → day roll. P&L comes from the account stream
(fills, fees, funding); daily comparison with the exchange; two weeks of fill-by-fill comparison vs the shadow.

## 7. Operations

Guard every minute, alert once on transition and once on clear (engine down, silent stream, orphan position,
clock > 50 ms, disk > 80%, loss stop/kill, mismatch). Cadence: 00:00 day roll, 00:10 gates + Telegram, 02:30
archive sync + pg_dump to home, Monday review + decision log, gate decisions by the founder, monthly updates on
staging first. Runbooks: halt (`touch data/mm_live/KILL`), restart (flatten first if live), rotate key, deploy,
re-test (`mm_robust`), data gap. Incident: halt first, record in `mm_live_events` (kind `incident`) and the
decision log, add a regression test or guard check before resuming.

## 8. Continuous optimization

One door: pre-register → holdout (`mm_opt`/`mm_robust`, pessimistic, HL tick unit; must beat incumbent and stay
on the plateau) → shadow paper 10 days beside the incumbent → promote (tag, deploy, log; old parameters kept for
rollback) → watch (first 10 days count double). Queue: BTC-lead pause (Binance fan-out :8197), BTC/ETH hedge,
60–100 markets, full re-optimization on Lighter's archive at 30 complete days. May change: width, spread filter,
imbalance, pause, requote, market list, size per G4. Fixed: zero-fee venue, pessimistic queue, flat-to-flat UTC
days, gate numbers (except by logged founder decision). Drift: 20-day rolling KPIs vs 60-day means; markout
worse by 0.5 bps or fill rate halved opens a research item.

## 9. KPIs per engine

Recorders: gaps/day 0; reconnects ≤ 3; hours ≥ 23.5 per complete day; receive lag p50 ≤ 60 ms on the server
(142 ms home); archive bytes within 50% of mean, disk < 80%.

Paper engine: connected and books/min > 0; db drops/errors 0; day P&L per G2; 20-day net bps ≥ +0.25 (drift alert
−0.5 vs 60-day); 10 s markout ≥ −1.0; capture ≥ +0.6; fills per market-day within ½–2× of the Lighter re-test;
≤ 1 loss stop / 20 days; live-vs-replay within 10% fills, same sign.

Live engine/signer: ack latency p50/p99 ≤ 300/600 ms; rejects ≤ 2%; reconciliation mismatches 0; heartbeat
misses 0; real vs shadow fills within 20%; slippage ≤ 1 bps; live markout ≤ 2× paper's; fees 0, funding
reported; same P&L sign as paper ≥ 70% of days.

Research/gates: 28 + 10 + 18 tests pass per deploy; golden replay hash unchanged unless explained; gate line by
00:15 UTC; monthly `mm_robust` on Lighter.

Infrastructure: clock ≤ 50 ms; memory < 256 MB per engine; backups < 26 h; running tag = deploy tag.

## 10. Sustainability checklist

Before deploy: pre-registered; tests pass (new logic has a test); golden replay hash unchanged or explained;
live-vs-replay parity; tick unit checked per market; plan ↔ `mm_gates.sh` thresholds in the same commit; risk
controls exercised; dependencies pinned; rollback written.
After deploy: start rows carry tag + tick lines; first day compared; guard/gates/Telegram fire; CHANGES, system
doc, plan log, explainer updated.
Calendar: daily gate line + every incident explained; weekly drift KPIs + log entry; monthly `mm_robust` on
Lighter, fee/terms check, staged updates, restore test; quarterly market list + thresholds + capital plan.
Invariants: one shared header; flat-to-flat UTC days; key on one machine/process; engines halt on what they
cannot judge; nothing deleted (append-only).

## 11. Roadmap

Phase 1 Record and paper (running, 25 Oct) → G1 + G2 (earliest 15 Nov). Phase 2 Server in parallel (region,
provision, deploy, 7-day parity run, cutover; earliest 5 Nov). Phase 3 Harden after G1 (Lighter re-tests, BTC
pause, hedge, wider universe; each via the loop). Phase 4 Live after G1 + G2 + parity + founder decision ($250
pilot = G3, then $2k/$5k/$10k = G4, two weeks per step).

## 12. Risks

Edge not on Lighter (G1 fail rule; first 1.5 h markout −1.8 to −2.3 bps at 60 s) · fee/tier change (monthly
check; kill above 0.25 bps maker) · optimistic fill model (shadow engine, G3) · crash day (loss stop, caps, hedge,
sizing ≤ 7% worst day) · funding/impact unmodelled (measured in pilot) · key compromise (one machine, one process,
rotation, pilot capital) · engine bug (dead man, reconciliation, kill, exchange-side post-only) · venue outage
(cancel on silence) · regulatory grey area (Cyprus not restricted; accountant before G3) · clock drift (chrony,
50 ms) · single box (second archive copy, guard) · overfitting (one door) · survivorship (quarterly list review).

## 13. Appendix

Files: `src/dex/hl_mm.hpp` (fill model + strategy), `hl_events.hpp` (loader), `lighter_feed.hpp`, `lighter_ws.hpp`,
`mm_paper.hpp` + `mm_paper_main.cpp`, `lighter_recorder_main.cpp`, `hl_recorder_main.cpp`, `mm_sim_main.cpp`,
`mm_opt_main.cpp`, `mm_robust_main.cpp`; `scripts/mm_gates.sh`, `scripts/hl_history_download.sh`;
`deploy/{mm-paper-local,lighter-recorder-local,hl-recorder-local}.service`, `deploy/mm-gates.{service,timer}`;
`migrations/20261025_mm_paper.sql`; tests `tests/dex/`.

Commands: `systemctl --user status mm-paper-local lighter-recorder-local hl-recorder-local mm-gates.timer` ·
`cat data/mm_paper/status.json` · `cat data/mm_gates/report.md` · `touch data/mm_paper/KILL` · `build/mm_sim --dir
<archive> --params <file> --daily out.json` · `build/mm_robust --dir <archive> --split <date>` · `build/test_hl_mm
&& build/test_lighter`.

Dashboards: Market-Maker Gates https://claude.ai/artifact/8fy6onCp2FmirrCE7RjZm9 · DEX Market-Making Sim
https://claude.ai/artifact/WtGeoKFdUaPKcxkv9cCtzs · Lighter Market Maker explainer
https://claude.ai/artifact/McFBtvFL2ZKykTjgJcipUZ.
