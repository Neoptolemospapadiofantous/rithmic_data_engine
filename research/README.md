# edge_lab — how a strategy idea becomes (or does not become) an edge

Every idea goes through the same machine, the same way. An idea is one JSON file in `research/ideas/`.
The verdict lands in `data/edge_lab/reports/<id>.md` and in Postgres (`research_ideas`, `research_runs`).

```
scripts/edge_lab.sh export            # bars_1m NQ + ES (2010-07 → now) and FOMC dates → data/edge_lab/cache (≈15 s)
scripts/edge_lab.sh run research/ideas/x.json [more …]    # all gates, then load into the DB (seconds per idea)
scripts/edge_lab.sh list              # every idea ever tested and its verdict
```
During RTH use `EDGE_LAB_BIN=build-exp/edge_lab` (never rebuild `build/` 09:25–16:05 ET). `--quick` skips the
controls and two re-runs: a first look, never a verdict.

## The gates (an idea passes only if all five pass)

| # | gate | passes when |
|---|---|---|
| 0 | **why** | the spec says who is forced to trade against us (no "why" = refused) |
| 1 | **screen** | the best variant with ≥ `min_trades` (100) trades nets > 0 at the REAL cost (NQ 1.0 pt, ES 0.35 pt per round trip) |
| 2 | **walk-forward** | each month trades the variant best over the previous 12 months; ≥ 30 out-of-sample trades, net > 0, PF ≥ 1.05 |
| 3 | **controls** (200 seeds each) | the out-of-sample trades beat **random direction** in ≥ 95 % of seeds, AND beat **random time** (same day, a random bar of the entry window, the rule's own direction there) OR **random day** (same month, same time of day, the rule's own direction there) in ≥ 95 % |
| 4 | **robustness** | ≥ 5 of 7: each era 2010–18 / 2019–22 / 2023–26 positive, cost × 1.5, entry one bar late, the same spec on ES, ≥ 50 % of variants positive — and the latest era MUST be positive |
| 5 | **prop fit** | the out-of-sample trades as the micro (MNQ $2/pt at 1.5 pts, MES $5/pt at 0.6 pts) through `select_daily_50k` (eval → funded until breach → new eval): break-even eval fee > $150 |

Then: paper fleet (forward, pre-registered pass rule) → dry executor → live (the founder's call).

## Writing an idea

```json
{ "id": "strong_open", "name": "…", "why": "who is on the other side and why they must trade",
  "symbol": "NQ", "confirm": "ES",
  "params": {"k": [0.2, 0.25, 0.3], "stopk": [0.25], "rr": [2, 3]},
  "checks": [{"at": "09:50", "when": "abs(px - open) > k * atr", "dir": "sign(px - open)"}],
  "stop": "stopk * atr", "target": "rr", "flat": "15:55" }
```
- `from` / `to` ("YYYY-MM-DD"): the days used; keep a holdout after `to` and judge it once, at the end.
- `checks`: fixed ET times, evaluated in order, first true trades (`max_trades`, default 1). Or `scan`:
  `{"tf": 5, "from": "10:00", "to": "15:00", "when": …, "dir": …}` = every 5-minute boundary in the window.
- `dir` > 0 long, < 0 short, 0 no trade. `stop` in points (0 = none), `target` in R (0 = none), `flat` ET time,
  `max_bars` minutes (0 = none). One position at a time. Times may cross midnight (trading day 18:00 → 17:00 ET).
- Decision at bar i sees only bars before i: `px` = open of bar i, the fill is open[i] (+1 bar in the late-entry test).
  Inside a bar the stop counts before the target; a bar that opens through the stop fills at its open.
- `px` is the OPEN of the decision bar, so `px > hi_since(…)` / `px > hod` is almost never true (an open rarely clears the
  previous bar's high). For a breakout use a completed bar: `cl(5,1) > lag(5, hi_since(9,30))`.
- Keep the grid small: every extra variant raises the luck bar (the report prints the t a best-of-N needs).

### Variables (today, at the decision bar)
`px` `t` (minutes after midnight ET) `open` (09:30 open) `hod` `lod` (RTH so far) `dhi` `dlo` (since 18:00)
`onh` `onl` (overnight 18:00–09:29) `vwap` `vsd` (RTH-anchored, before 09:30 session-anchored) `cdelta` `cvol`
(RTH aggressor delta / volume — delta only where the data has aggressor side, 2023-03 on) `rvol` (RTH volume so far ÷ its
20-day average at the same minute).

### Variables (known before the day)
`atr` (14 prior RTH ranges) `pc` `ph` `pl` `pc2` `pibs` `prng` `pret` ((pc − pc2)/atr) `gap` ((open − pc)/atr)
`sma50` `sma100` `sma200` `xma` (net 50/100/200-day cross of yesterday's close) `rsi2` (of RTH closes) `vix` `vixc1` `vixc5` `vixpct` `vts` (VIX / VIX3M) `vvix` `skew` `y10` `y2` (Treasury yields, %) `y10c` (10-year's last daily change, bp) `usdc` (dollar index's last daily change, %) — the macro ones from `scripts/edge_lab.sh macro` (free FRED / CBOE) `dow` (1 = Mon) `tdom` `tdleft` `fomc` `opex` `nfp` (the real Employment Situation day once `releases.csv` exists, else the first Friday) `cpi` `ppi` (BLS release days, 08:30 ET — calendar table, migrations/20261017_calendar_bls_releases.sql)
`earn` (a big-tech earnings reaction session, `data/edge_lab/cache/events.csv` from SEC 8-K item 2.02) `mtd` (month-to-date return to the prior close) `prehol` (a holiday follows) `rollwin` (roll Thursday −3…+6 days: prices across days jump there — use `!rollwin` with `pc`/`gap`) `year` `month` `flow`.

### Functions
`abs sign min max hm(h, m)`; `lag(k, expr)` (expr k minutes earlier, same day);
`ema sma rsi atrb hh ll vsum dsum (tf, n)` on completed tf-minute bars; `cl op hi lo (tf, k)` = k-th last completed
tf bar; `rhi(m) rlo(m)` = RTH opening range of the first m minutes; `dwin(h1,m1,h2,m2)` / `vwin(…)` = aggressor delta /
volume of today's bars in [h1:m1, h2:m2) (known once the window is over); `bk(field, n)` = mean of an order-book field
(`qimb` = (bid − ask)/(bid + ask) resting size within 10 bps, `cxl` cancel-to-add, `bidq`, `askq`) over the n minutes
before the bar (NQ, 2023-03 … 2026-03; `meta --book` adds flow/book inputs to the take/skip model); `at(h, m)` = open of the bar at h:m today;
`hi_since(h, m) lo_since(h, m)`.

## After a PASS: meta-label and thesis exits (`src/research/meta.hpp`)

```
scripts/edge_lab.sh meta  research/ideas/so_cascade.json   # take / skip model  → reports/so_cascade__meta.md
scripts/edge_lab.sh exits research/ideas/so_cascade.json   # thesis exits       → reports/so_cascade__exits.md
```
Both start from the idea's walk-forward trades (same entries).
- **meta**: features at the signal (move, gap, overnight range/position/break, VWAP, room to yesterday's extreme,
  pullback, yesterday's IBS/return/range, trend vs SMA50, VIX, order flow, ES confirmation, check slot, weekday,
  events). Every January a model fit on EARLIER years only predicts the year's trades in R; take when > 0. Primary:
  boosted depth-2 trees; cross-check: ridge. Gates: better per trade · beats random skip (same count per year, 1000
  seeds) ≥ 95 % · beats the model trained on shuffled results (100 seeds) ≥ 95 % · better in ≥ 2 of 3 eras incl.
  2023–26 and the ridge also better · higher prop break-even fee than the base.
- **exits**: vwap / open (5-minute close back through VWAP / the RTH open), stall (< 0.5 R after 60 min), vwap + stall,
  and runners without a target (vwap, open, 15-minute trail), and `flow` (a 5-minute bar's aggressor delta against the
  trade by ≥ 20 % of its volume; days with order flow only). `mext` / `mext_vwap_stall` (2026-10-17) move the hard stop to 1 tick beyond the RTH extreme made
  against the trade before entry (never tighter than the idea's stop); their control is a **stop-width twin** (the rule's own widths
  shuffled across trades), not the timing twin. Cascade result: mext +5.84 vs base +4.80 pts/trade but the twin does as well (34.5 %) —
  the gain is the wider stop, not the level. The hard stop always stays; fills at the next bar's open.
  Gates per rule: better per trade · beats a path-blind time exit with the rule's own exit rate and holding times in
  ≥ 99 % of 200 seeds (8 rules tested) · better in ≥ 2 of 3 eras incl. 2023–26 · higher prop break-even fee.
  Never re-time only the trades the rule exited: those are selected on their future (they went back through the
  level), and any early exit of them looks good.

## Validating the pipeline itself (`scripts/edge_lab_validate.sh`)

Ideas whose right answer is known (`research/validation/*.json`, field `expect`; never loaded into the registry):

| check | expected | what a different answer would mean |
|---|---|---|
| 12 coin-flip rules on real NQ (`null_*`) | all FAIL | a false positive |
| the same 12 on a synthetic random-walk market | all FAIL | a false positive on pure noise |
| look-ahead traps (`leak_*`: a 15:50 price / unfinished order-flow window read at 10:00) | no trades | the evaluator leaks the future |
| `drift_long` (buy every day) | FAIL at CONTROLS | the controls cannot tell drift from an edge |
| `plant_even_day` on a synthetic market with a planted edge (`build/synth_market`, tests/research/synth_market.cpp) | PASS at 8 pts, FAIL at 0 | the gates cannot find a real edge |
| the same at 3 and 5 pts | info | the smallest edge the gates can see (2026-10-17: 8 passes, 5 fails at controls, 3 at walk-forward) |
| `so_cascade` twice | identical numbers | the run is not deterministic |
| every PASS idea re-run | same verdict (numbers may move only when the data was updated after the stored run) | code changed a result |

`--fast` = the real-data checks only (≈ 15 s); the full run ≈ 2 min. Output `data/edge_lab/validation/summary.{json,md}`,
shown on the dashboard's **Lab** page (`/lab`, bot repo `ui/routers/lab.py`) next to every idea's per-gate results and the
paper-fleet diagnosis. 2026-10-17: 46/46 as expected. Lesson from it: 3 of the 12 real-data coin flips passed screen AND
walk-forward — only the controls stopped them, so a leaderboard (gates 1–2 at best) cannot tell luck from edge.

## One-off research programs and reproducibility (`research/programs/`, 2026-10-17)

Tests that are not edge_lab ideas (sweeps, payout simulations, execution and regime studies) are C++ programs in
`research/programs/` (index + build line in its README); each one's printed result is kept in `research/programs/out/`.
`scripts/research_repro.sh` rebuilds and re-runs them and compares with the saved output (2026-10-17: 20 of 20 identical).
`scripts/edge_lab_validate.sh` checks the pipeline itself (2026-10-17: 60 of 60 as expected). `scripts/plan_parity.sh FROM TO`
checks that the Plan page flags the momentum entries the paper engine takes (2026-10-17: 12 of 13; the one miss is 2026-09-22,
when the engine's ATR still spanned the March–September data gap).

## Ledger — 2026-10-17 (every file in research/preregistered/2026-10-17_*; verdict in one line)
| Pre-registration | Verdict |
|---|---|
| regime_optimize | 9,408 regime rules, walk-forward + shuffles: FAIL all sessions — only "trending by 10:30 in the gap direction" (= the cascade) carries |
| fleet_entries_exits | the fleet's exits lose the move; its signals add nothing over "go with the day since 09:30" |
| reentry | 2nd / 3rd trade only after a winner (+5.7 vs −0.8 random); no faster 25K payout |
| ema_permission / ema_stops / ema_entries_trails / ema_sweep / open_regime_ema | EMA pullback ≈ breakeven out of sample whatever the stop / trail / entry / filter (3,300+ variants) |
| manual_execution | 1 min late or 5 pts worse kills the sprint edge → alerts within the minute, bracket at once |
| mtf_1m_dir_5m_entry | 1-min momentum DIRECTION (straight last 30 min, VWAP + open side) carries a small edge; the EMA entry adds nothing |
| momentum_round2 | NQ New York only (ES and the night sessions fail); the frozen exit kept |
| momentum_optimize | 12,000 settings: walk-forward +0.168 vs +0.079 but a narrow spike → not adopted |
| momentum_entries | straightness (not size) + PULLBACK entries; best = 45-min momentum + VWAP pullback (+0.118 R, all eras) |
| momentum_addons | VWAP rule only when the plan direction agrees: +0.249 R vs −0.001 → adopted on paper (mom_dir_45_vwap_plan); order flow / VIX sizing / scaling out not adopted |
Engine replays of every year (label `mom_hist`, 2010-07 → 2026-03, the fleet's real costs) confirm the momentum ranking:
mom_dir_45_vwap_plan > mom_dir_45_vwap > mom_dir_45_dip > mom_dir_30_ema20 / _any; all lose a little or break even in 2010-18.

## Known limits
- 1-minute OHLC only (no queue position); cost is a fixed real round trip per contract in every year.
- The continuous contract is not back-adjusted (see `rollwin`). NQ has no data 2026-03-20 … 2026-09-18.
- Overnight variables use RTH ATR. Multi-day holds are not modelled (Tradeify: flat by 16:45 ET).
