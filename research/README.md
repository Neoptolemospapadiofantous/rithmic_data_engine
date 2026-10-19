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
Wyckoff phase inputs (2026-10-18_wyckoff_phases, prior ok sessions): `e20` (efficiency of the last 20 RTH closes, 0–1) `r20` ((C[-1] − C[-21]) / atr) `r60` ((C[-21] − C[-61]) / atr) `uv20` (share of the last 20 sessions' RTH volume on up-close sessions) `p20` (last close's place in the 20-close range, 0–1). Same path / direction over 5 and 10 sessions (2026-10-18_daily_path_direction): `e5` `r5` `e10` `r10`.

### Functions
`abs sign min max hm(h, m)`; `lag(k, expr)` (expr k minutes earlier, same day);
`ema sma rsi atrb hh ll vsum dsum (tf, n)` on completed tf-minute bars; `cl op hi lo (tf, k)` = k-th last completed
tf bar; `rhi(m) rlo(m)` = RTH opening range of the first m minutes; `dwin(h1,m1,h2,m2)` / `vwin(…)` = aggressor delta /
volume of today's bars in [h1:m1, h2:m2) (known once the window is over); `bk(field, n)` = mean of an order-book field
(`qimb` = (bid − ask)/(bid + ask) resting size within 10 bps, `cxl` cancel-to-add, `bidq`, `askq`) over the n minutes
before the bar (NQ, 2023-03 … 2026-03; `meta --book` adds flow/book inputs to the take/skip model); `at(h, m)` = open of the bar at h:m today;
`hi_since(h, m) lo_since(h, m)`; `eqhi(tf, n, tol) eqlo(…)` = a pool of equal highs / lows among the last n tf bars (within tol × ATR), else NaN.
Market structure on completed tf bars since 18:00 (2026-10-18, LuxAlgo SMC; NaN when there is none — all O(day) with a per-day cache):
`swh(tf, n) swl(tf, n)` = the most recent CONFIRMED swing high / low (strictly above / below the n bars on each side, known n bars later);
`obh(tf, n) obl(tf, n)` = top / bottom of the most recent UNMITIGATED bullish order block — on the first close above the last unbroken
swing high, the last down-close bar between the swing bar and the breaking bar; mitigated by a later close below its low (`obhs obls`
= the bearish mirror, mitigated by a close above its high); `fvgt(tf) fvgb(tf)` = top / bottom of the most recent UNFILLED bullish fair
value gap (bar-1 high < bar-3 low; filled by a close below the bottom; `fvgts fvgbs` = the bearish mirror). A block or gap created by
the last completed bar is already visible, so an entry rule must guard with `lag(tf, obh(tf, n)) == obh(tf, n)` to skip the breaking bar.
Optional last argument `mode` (2026-10-18_luxalgo_faithful): `obh(tf, n, 1)` … = LuxAlgo’s block (the lowest-low / highest-high bar between
the swing bar and the break); `fvgt(tf, 1)` … = LuxAlgo’s gap (bar 2 closes beyond bar 1 with |body %| > 2 × the mean |body %| of all
earlier tf bars; filled when a bar TRADES through the far side). Default 0 = the definitions above.

## After a PASS: meta-label and thesis exits (`src/research/meta.hpp`)

```
scripts/edge_lab.sh meta  research/ideas/so_cascade.json   # take / skip model  → reports/so_cascade__meta.md
scripts/edge_lab.sh exits research/ideas/so_cascade.json   # thesis exits       → reports/so_cascade__exits.md
scripts/edge_lab.sh meta  research/ideas/so_cascade.json --page   # + the ORB page's 12 reads, permutation importance, refits → reports/so_cascade__meta_page.md (+ .features.csv)
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
  the gain is the wider stop, not the level. `liq` / `liq_run` (2026-10-18) take profit at the nearest liquidity pool ≥ 1R beyond the entry,
  `orbt` (2026-10-18, LuxAlgo ORB targets) at the nearest 09:30–09:44 range high/low ± k × half the range width (k = 1, 2) ≥ 1R beyond
  the entry, both 2 ticks before the level (else the idea's target; `liq_run` none); their control is a **target-distance twin** (the
  rule's own distances in ATR shuffled across trades). The hard stop always stays; fills at the next bar's open.
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

## Ledger — 2026-10-18 (research/preregistered/2026-10-18_session_cockpit.md)
| Test | Verdict |
|---|---|
| ORB-15 narrow / price beyond ORB-15 / beyond ORB-5 as cascade filters | beyond-ORB-15 PASSES the gates (+5.33 vs +4.27 base 2010–24) but on 2025+ its complement did better (+28 vs +9) → not adopted; the others fail controls and do not beat the base on 2025+ |
| London / Globex direction agreeing with the cascade | fail controls (Globex by one seed); 2025+ no better than the base → context only |
| Globex-open ORB 5 / 15 break, ORB 15 fade | all lose (−0.95 … −1.46 pts/trade) |
| London-open ORB 5 / 15 break, ORB 15 fade | all lose (−0.71 … −0.99) |
| NY ORB-5 failed-break fade | loses (−1.42) |
| Asia sweep (flow) forward on collector sessions | not checkable — `flow` covers only 2023-03 → 2026-03 |
| BOS (1h vs prior 2h) then EMA pullback — NY / NY 15m / London / Globex (2026-10-18_bos_ema) | all FAIL (+0.78 / +1.01 / −0.70 / −1.29) |
| the same on the gap side only (`bos_ema_gap`) | PASS +4.03 (1,204, EMA-9, stop 0.3 ATR, 2R); 2025+ +8.45 on 141 (t 0.9); flat 2010–18; weaker than gap_or_retest on the same days → paper candidate |
| 1-min direction + BOS / CHoCH + EMA pullback (2026-10-18_m1dir_structure_ema) | all FAIL: BOS +10.81 on 215 (13/yr) fails direction control and loses 2025+ (−7.73 on 25); CHoCH 3 trades/yr; no structure +4.71 fails controls |
| Rithmic order flow as direction at 10:00 (2026-10-18_rithmic_direction, 2023-03 → 2026-03) | 1-min momentum PASS +34.8 (90); delta adds nothing to it; delta agreeing with price +16.4 vs +8.2 disagreeing but FAILS controls; overnight delta −6.6; absorption too rare |
| 1-min momentum at 10:00 only, all years (rdir_m1 without flow) | PASS +13.7 (22/yr, 11/14 years); 2025+ +59 on 50 |
| 1-min momentum scanned 09:45–13:55, ≤ 3/day (2026-10-18_momentum_daily) | VWAP-pullback entry PASS +3.09 (128/yr), 2025+ +23 on 201; market entries fail controls |
| momentum scan with EMA pullback / VWAP side + EMA pullback (follow-ups, 2026-10-18_momentum_daily) | both FAIL controls (+2.09 / +2.24 per trade, ~155/yr); the VWAP-pullback momentum scan stays the passing version |
| momentum scan with EMA-14 pullback (follow-up 3) | FAIL +1.61 (137/yr), 2023–24 negative — EMA 9/14/20 all fail as the momentum entry |
| a trade every day: forced 10:05 direction / no-cascade-day fallbacks at 11:35 (2026-10-18_daily_trade) | all FAIL; quiet-day trades ≈ 0 per trade — no edge on days without a setup |
| 1 trade a day, 20–30 pt target (≈ 0.06–0.08 ATR) (2026-10-18_scalp_daily) | every-day and momentum-pullback scalps lose (all variants); 10:00 momentum scalp PASS (19/yr, win 77 %, target 0.08 / stop 0.15 ATR) |
| first tested setup of the day (cascade > gap retest > 10:00 momentum > momentum pullback) (scalp_daily follow-up) | 2R PASS +4.01 (≈ 80 % of days, 12/14 years), 2025+ +5.5 on 249 (t 0.7); same entries with a 20–30 pt scalp target FAIL |

## Ledger — 2026-10-18 (research/preregistered/2026-10-18_plan_checks.md — on the real engine's all-year trades, label `mom_hist`)
| Test | Verdict |
|---|---|
| Stacking cascade + gap retest + momentum-VWAP-plan | one bet: daily correlation +0.59 / +0.62 / +0.72, mostly the same direction. On a 25K LIGHTNING (2 MNQ, 40 days) stacking is worse — cascade alone paid 8.4 % / blown 41.8 %, cascade + gap 3.7 % / 56.4 %, all three 1.6 % / 59.2 % — because the 20 % best-day rule punishes big stacked days (without it stacking pays 42 % in 14 days). Not adopted on Lightning; the 10-17 Select / Flex result (stack when agreeing) stands |
| News days (CPI / NFP / PPI / FOMC), 6 strategies | pass by the letter: cascade CPI / PPI (≈ $0 / trade), gap retest FOMC (−$9 / trade, 52) → plan page stands aside, paper id `gap_or_retest__nofomc`; momentum-VWAP-plan nothing; 30 cells, some by chance |
| Quiet-market stand-aside (prior VIX, prior ATR %), walk-forward by year | FAIL both: fixes 2013-18 (−2,547 → −59 / +722) but gives back more in 2019-24; 2010-18 is ≈ 0 or negative at every VIX and ATR % level — not fixable by a filter known at the open |
Engine all-year replays (label `mom_hist`) now also hold `so_cascade_2r_nofomc`, `so_cascade_2r_vwap_stall`, `gap_or_retest`,
`gap_or_retest__nofomc` ($ per MNQ per trade by era: cascade −0.84 / +8.41 / +16.50, gap retest −0.29 / +27.35 / +25.70).
| BDH (Patrick Wieland / D-Money) rules from 43 transcripts (2026-10-18_bdh) | 9 tests: 1:1 prev-candle scalp, 14/50 EMA retest, ORB retest, 5-min ORB, failed-break reverse, 15:50 all lose; 15-min ORB midpoint stop +2.9–4.0 FAIL controls (only hold-to-close positive); + gap side +6.4 (FAIL controls by a hair, 2025+ not confirmed) = the gap retest we already have |
| BDH ORB gap-side optimisation, 72 variants (2026-10-18_bdh_optimize) | PASS 2010–24 +6.32 (13/14 years, broad plateau) but every variant NEGATIVE on 2025+ (gap retest +7.15 there) → not adopted |
| ORB15 trend-or-reverse by regime, 7 reads × 4 (2026-10-18_orb_regime) | no regime verified: the fade is negative under every read; trend separates on gap (+4.2 vs −11.3) and 1-min straightness (+6.0 vs +2.6) but fails controls (random time on those days does as well) → trend regime = trade the day direction, range regime = no trade |
| feature study: 33 features (21 + the ORB page's 12), permutation importance, refits (2026-10-18_feature_study; `edge_lab meta --page`) | gap retest PASS on the 14 features that work (+15.7 vs +11.0); cascade / momentum / union refits fail a control; reg_score, acr60, or15_w, ema_touches carry signal, struct / CHoCH none (already agree at the signal), ema_dist / pullback hurt |
| exits: regime-gated trail (`trail_reg`) and liquidity-pool targets (`liq`, `liq_run`) on the 4 bases (2026-10-18_feature_study step 4, 2026-10-18_liquidity_exit) | all FAIL — never better than the fixed 2R; liq on the gap retest beats a same-size-target twin only 89–92 % (need 99) |
| LuxAlgo SMC entries (2026-10-18_luxalgo_smc; new DSL `swh/swl obh/obl obhs/obls fvgt/fvgb fvgts/fvgbs`, exit `orbt`): order-block retest, FVG fill, premium/discount gate on cascade + gap retest (each with complement), ORB 50 % targets | nothing adopted — OB retest +0.59/trade on 109/yr (FAIL walk-forward, direction 53 %), FVG fill −3.11 (FAIL screen); the discount/premium gate keeps 7–10 trades/yr of either base and they lose 2010–22 (both FAIL), both complements = the ungated base (+4.45 / +8.94); `orbt` +4.53 vs +4.80 and +8.72 vs +9.03 (FAIL, not better than base) |
| LuxAlgo SMC with LuxAlgo’s own definitions (2026-10-18_luxalgo_faithful; DSL `mode` 1): lowest-low order-block retest, threshold + wick-filled FVG fill | nothing adopted — OB retest −0.62/trade on 56/yr (FAIL screen, every variant < 0), FVG fill −1.65 on 83/yr (FAIL walk-forward, every era < 0); the definitions were not why the first test failed |
| SMC retests with the day DIRECTION (09:50 cascade / gap side) and PATH (eff ≥ 0.30 / 0.55) (2026-10-18_smc_path_direction; `dir` = the day direction so the time control is the same-direction random bar): `smc_ob_dp`, `smc_fvg_dp`, reference `dp_base` | nothing adopted — all positive in walk-forward (+0.97 / +2.43 / +3.72) and all beat random direction, none beats random TIMING (1 % / 65 % / 59 %): the direction is the edge, the levels add no timing (order block worse than a random bar, +722 vs +2,745); straight path (0.55) > middle (0.30) everywhere |
| Wyckoff phases (2026-10-18_wyckoff_phases; new daily vars `e20 r20 r60 uv20 p20`): markup / markdown from the open (`wy_trend`), accumulation long / distribution short (`wy_range`, `wy_range_vol` with up-volume effort), phase as a filter on the plan direction (`dp_phase` / `dp_phase_not`) | nothing adopted — none beats random direction (74–86 % vs 99 %); each positive average sits in one era (range reversal all in 2019–22, trend all in 2023–26); trades against the phase +2.20 vs with it +1.43 |
| Direction-trade follow-ups (2026-10-18_direction_followups; new exit rule `trail_path` + gate-shuffle twin): path ≥ 0.55 at entry as a size / skip rule on so_cascade and gap_or_retest; runner on straight-path days; intraday Wyckoff (midday range after the morning move breaks with / against it) | gap_or_retest_eff PASS every gate (+11.19/trade, t 4.36, eras +2.35 / +20.27 / +22.16, holdout 69 trades +2,349) but only 1.44 × its complement (+7.77, need 1.5 ×) → not adopted by the pre-registered rule; cascade: path does nothing; `trail_path` worse than 2R on both; intraday Wyckoff ~5 setups / yr, too few |
| Path × direction map (2026-10-18_path_direction_map): 90 cells 2010–18 → 3 picks → confirmation 2019–24 → holdout 2025+ | **CONFIRMED**: gap side (`pdm_gap_confirm`, +28.26/trade, t 4.26, holdout 50 trades +919) and 45-min momentum side (`pdm_mom_confirm`, +22.16, t 3.36, holdout 53 trades +2,448) at the 10:00 candle close on a straight path (eff ≥ 0.55); cascade side fails (2023–24 < 0); the two share ~half their days → paper ids `pd_gap_1000` / `pd_mom_1000` (trend mode `path_dir`) |
| Course indicators as filters on the path × direction trade (2026-10-18_indicator_filters): RSI, MACD, VWAP ±1 SD, Keltner, EMA, VWAP side, prior value area, CVD (2023+) | nothing adopted — RSI / EMA / MACD / VWAP / CVD are ON on (nearly) every straight-path day (redundant); Keltner (gap) fails the both-halves check; the VWAP-band stretch (momentum) passes the gates 2019–24 (+19.95) but is below the unfiltered rule (+22.16) and its own complement |
| Path × direction robustness (2026-10-18_path_direction_robustness) | plateau PASS (every neighbour of entry 09:50–10:15, path 0.45–0.65, stop 0.20–0.35, target 1.5–3R positive, weakest 58 %); no thesis exit beats 2R; both-agree days corroborated on 2010–18 (+4.54 vs gap-only +1.89, momentum-only −1.77) → "2 units when both agree" to paper as a hypothesis; momentum-only days are era-dependent |
| Daily path × direction (2026-10-18_daily_path_direction; new DSL vars `e5 r5 e10 r10`) | daily direction alone FAILS (−2.63 / −1.04 / +0.56 per trade for 5 / 10 / 20 sessions); the daily PATH filters the 10:00 gap rule: CONFIRMED — only when the 10-session daily path is not straight (e10 < 0.429): 2019–24 +29.44/trade (119, t 3.75) vs +28.26 unfiltered, complement +18.30, holdout 2025+ 36 trades +1,485 (excluded days 7 trades −3) → size rule |
| Mean reversion on RANGE days = 10-session daily path not straight + choppy intraday path < 0.30 (2026-10-18_range_day_mr): VWAP ±1.5 SD fade, first-hour range poke fade, 0.3 ATR fade to the open | all FAIL (−2.78 / −2.35 / +0.07 per trade walk-forward; every one negative 2023–26) → range day = stand aside |
| Path × direction extensions (2026-10-18_path_direction_extensions) | second chance at 10:30 / 11:00 / 11:30 FAILS 2019–24 (controls 92 / 3 %); ES replication PASSES 2019–24 (+4.86 ES pts/trade, t 3.28) but 2025+ is flat (39 trades +7 pts); news days are not worse (+32.9 vs +20.9 2019–24) |
| London-open path × direction: Asia move side, straight 03:00–03:30 path, 03:30 candle close (2026-10-18_london_path_direction) | FAIL at discovery (+0.25/trade 2010–18, 2015–18 −0.17) — the effect is NY-open specific |
| Gap rule checks (2026-10-18_gap_rule_checks) | long and short legs both pay in every period (not drift); VIX and ES agreement give no size rule; monthly correlation with `weak_day_long` 0.19 (both lose in 10 % of months) — the two edges diversify |
| Two-edge book, path definition, weekday (2026-10-18_book_and_definition) | GAP + WEAK long on one account: opposite-direction days (18) skip the GAP short at no cost; book 2011–26 +14,170 pts, max drawdown 566 (WEAK alone 1,395, GAP alone 304); path on 5-min closes PASSES (85 %); every weekday positive |
| Every-day book of all confirmed setups, 1 NQ (2026-10-18_daily_book, description) | trades ~85 % of sessions; pts/session by year 2019 +3.3, 2020 +13.9, 2021 +8.5, 2022 +10.4, 2023 +3.3, 2024 +14.5, 2025 +21.7, 2026 +27.1 (1 trade/day; up to 3 adds little); 2010–18 ≈ 0–2 |; per-day at the measured 2-pt cost: +10.6 pts/session 2019–26, median day 0, 36 % of days ≥ +20 and 39 % ≤ −20, max drawdown 1,156 pts; 2025 +20.9 is +12.2 without 2025-04-09 (+2,117, the no-stop weakness long); edge_lab `--trades` flag added
| High-win-rate exits for the every-day book (2026-10-18_high_winrate_book): targets 0.5–2R × stops 0.25–0.5 ATR × weakness stop, 1 or 3 trades/day | no variant qualifies (every ≥ 60 % win-rate form loses in 2010–18 at 2 pt); 2019–26: 0.5R ≤ 3/day wins 68 % of trades, 51 % green days, +8.5 pts/session vs the 2R form 49 % / 41 % / +11.1; variants.csv gains a `win` column |
| Trade every day, take the P&L and stop, blown accounts allowed (2026-10-18_prop_daily_stop; payout_sim on the book trade lists) | daily profit / loss stops never win; best per account: the plain 2R book at 3 MNQ on a 50K plan — Select Daily 50K $1,801 / account (test 2023-05 → 2026-10), Topstep 50K 5 MNQ $2,237; ~⅓ of evals pass, most accounts blow within 60 sessions after payouts; the 0.5R high-win book and the Asia cascade pay less |
| Book ablation (2026-10-18_book_ablation; leave-one-out × points × prop EV) | keep the weakness long (without it 2025–26 +21 → +4, drawdown up), the cascade and the gap retest (each removal −⅓ prop EV); drop the 10:00 gap and momentum rules from the book (redundant behind the 09:50 cascade: EV +7–13 % on all three plans without them, points unchanged) |
| Daily trend scalp, 1–3 trades of 10–30 points (2026-10-18_trend_scalp): side-of-open + path, 1-min momentum, and the confirmed entries with fixed 10–30 pt exits | all FAIL — the best open-side form (30 / 30, VWAP pullback) fails controls 2023–26 (2023–24 −1.44 pts/session); momentum forms fail the halves; the confirmed entries lose with fixed small targets (best −0.47 pts/session) — the edge needs ATR-sized exits |
| Trend-day pullback: 09:50 cascade side, then 5-min pullbacks to VWAP / EMA 20, ≤ 3/day (2026-10-19_trend_day_pullback) | FAIL controls (+1.98 OOS, direction 73 %), 2023–26 −6.44; as an add to F 2010–18 down and pts/DD 0.0121 → 0.0096; Vest $50 pass 63 % → 34 % (faster, 14 vs 24 days) — the trend day is already in F's first entry |
| Globex EMA follow: EMA 9/20/50 stack (5 or 15 min), pullback to 5-min EMA 20, 18:05–09:20, ≤ 3/day (2026-10-19_globex_ema_follow) | FAIL at SCREEN — all 8 variants lose, walk-forward −0.57 pts/trade on 2,066, gross +0.43 = coin flip; EMA following is closed overnight too |
| Globex EMA follow with no target, trail after +1R, ≤ 3 or 1 per night (follow-up, 2026-10-19_globex_ema_follow) | FAIL at SCREEN (−0.26 / −0.43 pts/trade); Vest $150/trade first-pullback version passes 25 % at $158 per pass vs F 63 % at $64 — passes by variance |
| Full risk ($145–150) + full target (3.33 / 4.33R = the $500 goal) on the book entries and the Globex EMA follow (2026-10-19_full_risk_full_tp, description only) | target hit on 4.5–5.1 % of trades; Vest pass 19.5–24.9 % at $160–205 per pass vs the book at $50 / 2R 64 % at $62 |
| Full risk ($145) only on the strongest cascade days: path / gap / ES / VIX filters (2026-10-19_full_risk_best_days) | selected path + gap agree (31.4 % on 2010–18 starts) → 25.6 % on 2019–26 vs 24.8 % unfiltered; every filter 21–26 % — size, not day choice, sets the full-risk pass rate |
| 1-min bull / bear flags after a straight 15–30 min pole, NY 09:45–15:00 and Globex (2026-10-19_flag_1m) | NY FAIL controls by a hair (+2.44, 668 trades) — works 2019–24 only (2010–18 −0.02, 2025–26 −5.59 on 95); Globex FAIL screen (≤ 0) |
| "Follow the session move": 1 h in, > 0.25 ATR from the session open, go that way (2026-10-19, description in full_risk_best_days) | Asia −4.15, London −8.93, NY 10:30 +6.16 / 09:50 +4.37 FAIL controls; only the full cascade with re-checks passes |
| News days vs the opening move (description, 2026-10-19; scratchpad news/): ORB15 / first 30 min / premarket move, then the rest of RTH, by none / CPI / PPI / NFP / FOMC / big-tech earnings, 2010–18 vs 2019–26 | no day type follows the opening move reliably: continue rates 29–67 % on 17–64 days per cell, no cell |t| > 2 in both periods; NFP first 30 min reverts in both (40 % / 29 %) — the CPI/NFP fade is already a closed door; earnings days continue 2019–26 only (62–67 %, t 1.9) |
| Book without expiry weeks; cascade on big-tech earnings days (2026-10-19_expiry_earnings) | expiry skip: no difference (rule not met by 0.01); earnings: size-up rule met by the letter and ES agrees, but t 0.60 and a 2nd unit lowers pts/DD and the Vest pass → not adopted |
| Gamma exposure for ranges (2026-10-19, description; scratchpad sess/gex_out.txt): no GEX data here (needs options open interest by strike); proxy = prior VIX level and VIX/VIX3M term structure vs NQ RTH range, trend / chop share and 10:30 follow-through | high VIX / backwardation ("dealers short gamma") → range 0.71–0.89 ATR vs 0.56–0.61, slightly fewer chop days, follow-through ~51 % vs 43–50 %; low VIX does NOT bring more chop — effects small, range width is the only clear one |
| Multi-day context on the cascade and gap retest: yesterday a trend day same / opposite / none, 5-day move same / opposite / small (2026-10-19_multiday_context) | cascade: no cell; retest after any trend day SIZE-UP by the letter (+2.86 / +24.61 opp, +1.64 / +22.26 same) but ES does not replicate in 2019–24 and 2025–26 splits → nothing adopted |
| London fade: at 04:00, > k ATR from the 03:00 price, trade against it, flat 09:25 (2026-10-19_london_fade) | FAIL controls (+0.76 on 665, random direction 79 %), 2010–18 −0.36, 2019–22 −4.29, 2023–26 +7.09 (2024 alone), ES −276; holdout +2.34 on 113 — coin flip, closed |
| Prop-legal gap-down weakness long: stops 0.5–1.5 ATR, sized to its own stop on Vest (2026-10-19_weakness_long_prop) | 1.5 ATR keeps the edge (+23.29 / +24.44 / +77.52 vs no stop +23.66 / +32.33 / +77.54); F + W(1.5) +0.77 / +16.48 / +16.82 pts/session; Vest $40 74.6 → 75.3 %, $50 66.0 → 66.6 % → ADOPTED for Vest (12.6 trades/yr) |
| Exits on the book cascade (vwap / open / stall / runners / trails) and the Asia open drive as a Vest add (follow-ups in 2026-10-19_weakness_long_prop) | no exit beats the base on the book cascade (vwap +5.04 vs +5.55); Asia drive lowers the Vest pass at $50 (66.6 → 57.4 %) — neither used |
| Vest adds on top of F + weakness long: weakness long on non-gap-down days, 10:00 momentum, 1.5× VIX-fall cascade shorts (2026-10-19_vest_adds) | none adopted — each lowers the $40 pass rate (75.3 → 72.8–73.6 %); M is the speed option (23 vs 31 days) |
| Vest FUNDED $5k (assumed eval rules, claim at a threshold, 2019+; 2026-10-19_vest_funded) | F + weakness long beats every add; best value per $40 fee: $20–30 risk, claim at $500–1,000 (≈ $2,400 per fee, but 53–86 days to pass); $40 claim ≥ $2,000 $2,098; $50+ risk dies in weeks |
| Funded plan stress tests (2026-10-19_vest_funded): 2010–18 starts and 3–4 pt costs | fixed 2 pts 2010–18: ≈ $0 per fee (era-dependent); 2019+ at 3–4 pts: +$1,216…+$2,017 per fee; 2010–18 with cost scaled to ATR (today’s ratio): +$547…+$873 — the old-era weakness is mostly the cost share of a 48-pt ATR |
| Full port (first trade at $120 / $145) in eval and funded (2026-10-19_vest_funded) | full port funded = accounts die in 3–17 days (+$229 per fee at best, $145 ≤ $0); full port eval → $30 funded: +$1,222 per fee vs $30 throughout +$2,386 — but per funded account nearly equal ($90 vs $47 fees) and funded ~44 trade days sooner |
| Funded claim buffer and room-based sizing (2026-10-19_vest_funded) | claim at $1,000 down to a $250 buffer: $3,365 vs $2,837 per account, breaches 67 → 35 % (both costs) → ADOPTED; room-based sizing in funded or eval: no gain (no-floor version was an artefact) |
| Vest portfolio of 10 (full-port evals refill slots; 82 twelve-month windows 2019–25; 2026-10-19_vest_funded) | $30 / claim $1,000: +$6.2k/yr, 1 in 3 windows no payout; **$50 / claim $500 keep $250: +$13.4k / +$12.3k per yr (2 / 3 pts), 4–13 of 82 windows losing, worst −$2.4k, cash dip ≤ $4.5k** |
| Vest portfolio eval policy (2026-10-19_vest_funded) | full-port first trade at $120, refill all free slots at once: +$13.9k / +$12.7k per yr, 5 / 10 of 82 windows losing; $145 eval or an all-setups eval earn less |
| Vest operation plan on 2010–18 (out of period; 2026-10-19_vest_funded) | cost scaled to today’s ATR: +$10.7k / +$10.1k per yr, 5 of 89 windows losing, every start year positive → holds; at fixed 2010-era costs it fails (−$3.8k at 3 pts) — the plan lives or dies on cost per trade |
| Vest operation plan cost sweep (2026-10-19_vest_funded) | ≈ +$15k / +$11k per yr at 2 pts round trip, +$8k / +$5k at 10, break-even ≈ 15–20 pts (≈ 2.5–3 bp per side at NQ 30,000); Vest’s NQ fee is not public — read it from the platform |
| High-cost plan choice: all setups vs first trade + W vs cascade only, $50 / $75, claim $500 / $750 at 10–20 pts (2026-10-19_vest_funded) | all setups at $50 / claim $500 best at every cost (+$6.6k / +$3.2k / +$0.6k per yr at 10 / 15 / 20 pts); trading less does not help; ≥ 15 pts: not worth running |
| Weekly + daily behaviour; the book by weekday and week-so-far (2026-10-19_weekly_daily_profile) | weeks: high on Friday / low on Monday most often, no weekly direction carry (Mon→week 42–51 %); book best on Fridays (all 3 setups SIZE-UP 2010–24), Friday 1.5× lifts the Vest operation +$2.2–2.8k/yr in both eras → adopted, but Fridays 2025–26 −16.8 pts/trade |
| Entry timing on 1m / 5m / 15m bars (break of prior bar; pullback-then-break) for the 09:50 cascade direction (2026-10-19_entry_timeframes) | none beats the market entry per session in either era (base +0.25 / +3.06; best brk 15m +0.10 / +2.57); waiting costs more of the move than it saves — keep market entries |
| Pre-session levels (description, 2026-10-19; scratchpad sess/levels_*.txt): Asia vs prior NY H/L, London vs Asia H/L, NY vs overnight and prior NY H/L, Globex 18:00–09:30 vs prior NY H/L — inside / one side / both, and what follows the FIRST break | one side only 56–72 % of sessions, both 2–41 % (London 38–41 %); after the first break the session closes beyond it 49–52 %, back inside 30–48 %, through the other side 1–19 % — the first break is a coin flip for direction in every session and era |
| Session model: boosted trees + ridge on 28 pre-session features (prior sessions, levels, path, VIX, calendar, ES) → Asia / London / NY direction, move, trend; walk-forward by year 2014–24 (2026-10-19_session_model, research/programs/session_model.cpp) | nothing passes — direction hit rate below "always up" in 5 of 6 cells, corr 0.01–0.04; Asia "edge" = the upward drift; NY trend separates only 2019–24. Direction is not forecastable at the open; only the move after the open carries it |
| Intraday models (2026-10-19_intraday_models, research/programs/intraday_models.cpp): A) NY range at 09:30; B) direction from 09:50 / 10:30 to 15:55 | range IS forecastable (corr +0.42–0.52; predicted fifths 0.74 → 1.40 ATR; London range, open vs yesterday’s high, Asia range lead) but book R does not depend on it → no size rule; direction model / "move + model agrees" filter fails (2014–18 worse at 09:50, ridge disagrees at 10:30) |
| Trend vs chop day from the morning state (description, 2026-10-19; scratchpad sess/morning_odds.txt): straightness + distance at 10:00 / 10:30 vs the day's close | before the open: not forecastable (session model). At 10:30, straight + moved ≥ 0.25 ATR (≈ 1 day in 6): trend day 57–58 % (all days 44–47 %), chop 16 %, closes on that side 84–87 %; choppy (< 0.30): trend 38–40 %, closes on that side 55–61 %. Part of "closes on that side" is mechanical (price is already far from the open) |
| Book worst days vs predicted range; ES accounts beside NQ (2026-10-19_tails_and_es) | wildest predicted fifth has 1.5–1.6× the ≤ −2R days but earns at/above average → no skip; 5 NQ + 5 ES: smoother 2019–25 (worst −$1.4k vs −$3.6k) but 59–74 % of the money and more losing windows 2010–18 → ES not added. NQ atr.csv = edge_lab ATR lagged a day; results unchanged on the exact ATR |
| Behaviour part 3 + order flow (2026-10-19_behavior_part3, research/programs/behavior3.cpp): streaks, pullback depth, gap fills, PDH/PDL sweeps, round numbers, afternoon, RVOL, aggressor delta / divergence / absorption / book imbalance (2023+) | no streaks (equity-curve filter fails); trend-day pullbacks avg 0.49 ATR but wider stops (0.5 / 0.7) halve the Vest operation → 0.35 stays; small gaps (0.1–0.25 ATR) fill ~75 % in ~10 min; ~83 % of PDH/PDL breaks come back within 30 min, a 30-min hold closes beyond 68–80 %; round numbers, afternoon, RVOL, book imbalance: nothing; delta mostly agrees with price — "skip when delta disagrees" is a hypothesis (55 trades) |
| Gap retest on gaps ≥ 0.25 ATR; PDH/PDL break-and-hold 30 min; small-gap fade toward the prior close (2026-10-19_gaps_and_holds) | retest ≥ 0.25: 2019–24 lower → no; break-and-hold FAIL controls; gap fade (stop 2× gap) passed edge_lab but 2010–18 −0.48, holdout 2025+ −7.91 on 70, and opposite to the retest every shared day → no |
| Six attacks (2026-10-19_attack_behaviour): overnight holds on Vest (allowed, funding fees), targets by predicted range, evals on wild days, fade first PDH/PDL break, Mon/Tue short skip, anti-streak sizing | **ADOPTED: overnight weakness hold O3** (weak close + VIX ≥ 20, close → next close, 1.5 ATR stop): +5.0 / +43.6 / +69.1 pts/trade, Vest operation up in all four cells even with ≈ 6 pts funding; everything else fails (PDH/PDL fade loses in every era); anti-streak = more size, not a streak effect — founder’s call |
| Consistency: scale-out 0.25–1R and with / without the weakness long (2026-10-18_consistency) | none qualifies — scale-out lifts green days to 58–66 % and cuts losing runs to 3–4, but positive 10-session windows stay 56–63 % and the best 1 % of days carry 43–88 % of the profit (current book: half its 2019–26 profit is from its best 20 days) |
| Book management (2026-10-18_book_management): break-even at +0.5 / +1R, flat at 12:00 / 13:30, daily-path filter on the cascade | nothing adopted — break-even and early exits lower points and raise drawdown; the daily-path filter helps 2010–18 (+1.14 vs +0.41) but loses 2 pts/session in 2019–24 |
| Order book as a cascade filter (2026-10-18_cascade_orderbook; resting-size imbalance bk(qimb,20) ± aggressor delta, 2023-03 → 2026-03) | pick (delta + book agree) FAILS confirmation (walk-forward 50 trades +1.58, 1.12 × unfiltered); hypothesis only: cascades into size stacked against them earn far less in both periods (+4.2 / +5.0 vs +8.8 / +21.1) |
| Cause & effect (tight overnight / narrow prior day) and effort vs result (rvol at the check) on the cascade (2026-10-18_cause_effect_effort) | no consistent effect — the coiled build-ups and the effort levels are not better in both periods; one cell (prior-day range in the middle third) meets the size-up rule by the letter but contradicts the theory, is 1 of 9 and under-earns in 2025–26 — not adopted |
| Weekly path × direction and turn-of-month flows on the cascade (2026-10-18_weekly_and_tom) | by the written rule: SKIP a cascade WITH a straight week (−1.97 / −7.28; 2025–26 +32.5 on 31) and SIZE UP cascade longs at the turn of the month (+1.77 / +13.43 vs +0.79 / +8.03 for all longs) — both to the forward record first |
| Checks of those two rules (2026-10-18_weekly_tom_checks): ES and the other NQ trend entries | neither replicates on both — the skip holds on the NQ retest / 10:00 rule but not ES; the turn-of-month longs hold on ES but not the other NQ entries → forward record only; in the book together +1.4 pts/session 2019–24 with a higher drawdown |
| Path and direction on the weakness long (2026-10-18_weakness_path): confirm above the open / + straight path, or exit at 10:00 on a straight-down morning | nothing adopted — every variant earns less in both periods; straight-down weakness mornings recover by the close (held +1,103 vs cut +532, 2010–18) |
| The book stop × target grid (2026-10-18_book_stop_target; 0.25 / 0.35 / 0.5 ATR × 1.5–4R) | current 0.35 ATR / 2R stays — the rule pick (0.35 / 4R) is lower in 2019–24; the 0.25–0.35 × 2–4R block is a plateau (+7.6 … +9.7 pts/session 2019–24); 0.5 ATR is worse with ~40 % more drawdown |
| BOS entries 09:35–11:30, 2–3 a day, ± day direction / straight path (2026-10-18_bos_morning) | FAIL — all 24 variants < 0 in 2010–18 (best −0.56 pts/session, direction + path); 2019–24 best +1.5 vs the book +9.2 |
| ES confirmation and the prior VIX change on the cascade (2026-10-18_cascade_es_vix) | ADOPTED size rule: cascade SHORTS after VIX fell (NQ +1.58 / +12.81 vs all shorts +0.57 / +9.85; replicated on ES in all three periods); ES confirmation and the long cells give nothing |
| Checks of the VIX short rule (2026-10-18_vix_short_checks) | consistent on gap-retest shorts (+17.8 vs +1.3, 2019–24), mixed on 10:00 shorts and with the 5-day VIX change; in the book 2× adds +19 % points but +35 % drawdown — return per drawdown slightly lower |
| More than one trade a day (2026-10-18_multi_trade_day) | sequential FAIL; overlapping F ADOPTED: +15.05 pts/session 2019–24 (vs +6.66), DD 1,247, Vest $50 62.9 % pass in 24 days. Sequential: — a 2nd setup fits on only 0.08 sessions (the 1st trade is still open); +0.7 pts/session 2019–24 but lower 2010–18; Vest eval with two-stop sizing passes less often and slower |
| More setups side by side (2026-10-18_side_by_side_adds) | nothing adopted — F+M +18.73 pts/session 2019–24 but 2010–18 +0.11 < +0.15; adding the weakness long with a 0.5 ATR stop pushes drawdown over the 1.3× cap; every add lowers the Vest pass rate at $50 |
| Late cascade checks and late retests (2026-10-18_late_entries) | nothing adopted — the 10:30 / 11:00 / 11:30 checks and the afternoon retests all add points and cut drawdown; F stays as is |
| Combining many strategies (2026-10-18_combine_many) | 13 candidates; 5 eligible; greedy adds the gap-down weakness long (no stop) and the Asia open drive → +0.81 / +16.96 / +18.02 pts/session, DD 1,215; NY momentum ideas correlate +0.45–0.54 with F (same days); on Vest the stop-less long breaks the daily limit, F stays there |
| Validation of every price-action test (2026-10-18_price_action_validation) | VALIDATED — unit tests 598/598, pipeline 76/76 (detection limit ~5 planted pts), 55/55 specs reproduce exactly, no flipped spec passes, no spec failed only on cost; the high-gross fails all fail at CONTROLS (the day, not the pattern) |
| NQ leads ES / quarterly-rebalance close (2026-10-18_follow_and_quad_close) | nq_es_follow FAIL at CONTROLS; quad_close NOT TESTABLE — the NQ.c.0 cache has no RTH bars on quarterly expiry Fridays (calendar roll keeps the expired contract to Sunday) |
| NY sizing book (description, same file): 2 units when gap and momentum agree, 1 gap-only, half after a straight fortnight | beats flat size in every period: 2010–18 +1,300 vs +792, 2019–24 +7,079 vs +4,811, 2025–26 +2,432 vs +919 (groups from earlier findings on those years — consistent, not clean) |

## Known limits
- 1-minute OHLC only (no queue position); cost is a fixed real round trip per contract in every year.
- The continuous contract is not back-adjusted (see `rollwin`). NQ has no data 2026-03-20 … 2026-09-18.
- Overnight variables use RTH ATR. Multi-day holds are not modelled (Tradeify: flat by 16:45 ET).
