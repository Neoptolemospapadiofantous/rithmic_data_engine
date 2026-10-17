# Changelog

All notable changes to rithmic_engine are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Dates are in ISO-8601 order (newest first).

---

## 2026-10-17 (later) — momentum family, VWAP pullback with plan agreement, alerts, second-drive backup, validation
- Trend engine mode `mom_dir` (1-min momentum direction + 5-min entry `mom_entry` any / ema / vwap / dip, `mom_window`, `mom_eff`,
  `mom_last_entry`, `mom_need_plan` = the plan's cascade / gap-retest direction tracked inside the mode, done-after-a-loss via the
  closed trade's P&L) and PaperBroker `be_r` (break-even at be_r × the trade's stop; paper only — executor refuses it, leaderboard
  marks it not live-runnable). Tests in test_trend_strategy (13 mom_dir checks) and test_paper_broker.
- Paper ids `mom_dir_30_any`, `mom_dir_30_ema20`, `mom_dir_45_vwap`, `mom_dir_45_vwap_plan`, `mom_dir_45_dip`, `mom_dir_45_ema9_tight`;
  backfill (label `backfill`, 09-17 → 10-16) and every-year engine replays (label `mom_hist`, NQ bars 2010-07 → 2026-03).
- Plan page: momentum boxes (`live.mom` 30-min + EMA-20, `live.mom45` 45-min + VWAP + plan agreement), momentum windows include the
  hour before 09:30; alerts `mom-HH:MM` and `momv-HH:MM`.
- `scripts/local_backup.sh` + `local-backup.timer`: nightly copy to the second drive (pg dumps 14 there / 3 on the main disk — the
  main disk was 98 % full —, git bundles, owned market data). The Oracle off-box copy has been failing since ≥ 2026-09-27.
- Validation: `scripts/research_repro.sh` (20 / 20 research programs reproduce), `scripts/edge_lab_validate.sh` (60 / 60),
  `scripts/plan_parity.sh` (12 / 13 momentum entries flagged by the page; the miss is the post-data-gap ATR on 2026-09-22).
- Research ledger for the day: research/README.md "Ledger — 2026-10-17"; programs + outputs in research/programs/.

## 2026-10-17 — daily plan for hand trading, payout sprint, regime + EMA research, dry gap-retest unit
- Daily plan (`scripts/daily_plan.sh` + `scripts/daily_plan_live.jq`, dashboard /plan): "Right now" panel (which setup triggered,
  order flow, allowed / not allowed), EMA permission box (2-min EMA-9 / EMA-21, day direction, VWAP + open side, ≤ 13:55),
  "Payout sprint" block (`live.sprint`: the day's first signal, 0.5R target, the plan's own outcome). `daily-plan.timer` now
  runs every minute 09:00–13:59 ET (+14:01). New `scripts/plan_alerts.sh`: Telegram (grid-notify) morning brief, a warning one
  minute before each cascade check, gap retest armed, TRADE NOW with MNQ / stop / target — one per day per key.
- Research (all pre-registered under `research/preregistered/2026-10-17_*`): regime optimisation (9,408 rules, all sessions —
  FAIL; only "trending by 10:30 in the gap direction" carries, i.e. the cascade); fleet entries vs exits (exits lose the move;
  signals add nothing over the day's direction); re-entries (only after a winner); EMA permission, stops (125), entries × trails
  (96), opening regime, EMA sweep (2,880 incl. trend + momentum filters) — the EMA pullback is ≈ breakeven out of sample;
  live ORB rules −1.2 pts/trade; mtf_london_v5__sg negative everywhere; one-week payout sprint (Tradeify Lightning best:
  payout ≤ 7 / 20 trading days ≈ 6.7 % / 25 % on 25K) and manual execution (1 min late or 5 pts worse kills the sprint edge).
- Paper cascade: the 12:00 follow-any-move check removed from every `so_cascade_*` id (`drive_late_count` 4 → 3,
  `drive_late_last_min_atr` 0 → 0.3) and from the dry `tradeify_cascade` executor — it lost money (2026-10-17 tests).
  The ids keep their names; their rows before 2026-10-17 include the 12:00 trades.
- New dry executor `tradeify_gor` (gap retest, `config/tradeify_gor_config.json`, label `tradeify_gor_dry`, enabled).
- `nq-executor-local@tradeify` (the live ORB) DISABLED as well as stopped (founder 2026-10-17 "do all").
- Ops: the held BBO WAL (`data/backups/ticks.wal.bbo.pending-20261016`, 6.2 M rows) was replayed in the weekend close; every
  row was already present (merged, nothing new) — NQ / ES quotes and `book_1m` are complete 10-12 → 10-16.

## 2026-10-16 (later) — round 3: gap edge is not one setting; limit-entry paper twin; account mix and worst case
- `research/preregistered/2026-10-16_round3.md`. Gap-agree beats the plain cascade at every gap threshold 0.05–0.3 ATR
  (+7.5 … +9.1 vs +4.8). Overnight-range-break filter and the weak-day gap splits fail.
- `OrbConfig::entry_limit_ticks` / `entry_limit_secs` (PaperBroker only): a limit entry N ticks better than the last
  trade, filled at the limit only when a later tick trades through it, cancelled after the timeout (no trade). The
  executor refuses the key. Paper twin `so_cascade_2r_lim1` (working fleet variant). Tests in `test_paper_broker`.
- Account mix: a second account on the same trades doubles cash, not frequency; Flex + Daily gives the most paid months
  (4.5/yr). At 8 MNQ the cascade's worst day (−$2,882) exceeds the $2,000 trailing drawdown — the fast evaluation
  fails often by design (~5.7 evals a year per Flex chain).

## 2026-10-16 (later) — gap-agree confirmed on ES; VWAP exit on top; options expiry and 2× sizing do nothing
- `research/preregistered/2026-10-16_confirm_gap.md`. `es_cascade_gap_agree` PASSES (+1.97 vs +1.10 ES pts/trade, all
  eras positive): the gap filter holds on a second market. The VWAP thesis exit on gap-agree trades passes (+10.05 vs
  +8.59, drawdown halved). Options-expiry fade and skipping opex days fail. Doubling size on gap-agree days does not
  bring the first Flex 50K payout forward (16.0 → 17.8 weeks, more failed evals); trading only those days is slower
  still (~30 weeks).
- Paper candidates `so_cascade_2r_gapagree_vwap` (NQ) and `es_cascade_2r_gapagree` (ES fleet); deployed by the same
  after-close job.

## 2026-10-16 (later) — six new angles (pre-registered): two cascade context filters go to paper
- `research/preregistered/2026-10-16_new_angles.md`, controls at 99.2 %. ES agreement, relative volume, the afternoon
  drive, the 14:00 exhaustion fade and "yesterday closed at its extreme" all FAIL.
- **Gap agrees** PASSES: +8.59 pts/trade vs the cascade's +4.80; frozen holdouts +16.35 (2019–22) and +14.32 (2023–26),
  both above the plain cascade. The trades it removes still make money, so it is a candidate, not a change to the core.
- **Yesterday NOT at its extreme** (the complement of a failed test, opposite of the prediction) passes at +7.49,
  holdouts +9.60 / +14.68 — forward test only.
- `TrendConfig::drive_gap_agree` (gap ≥ x · day ATR in the trade's direction; no prior day → no trade) and
  `drive_prev_extreme` (skip a long when yesterday's IBS ≥ x, a short when ≤ 1 − x). Both apply to the main decision and
  every late check; a blocked check leaves the later checks armed. Paper only. Tests in `test_trend_strategy`.
- Paper ids `so_cascade_2r_gapagree` (0.1) and `so_cascade_2r_notext` (0.7); working-fleet candidates; deployed and
  backfilled by the 2026-10-16 after-close job.

## 2026-10-16 (later) — the cascade on big-tech earnings mornings (pre-registered): no edge; CPI untestable

`research/preregistered/2026-10-16_earnings_mornings.md`. 456 earnings releases (SEC EDGAR 8-K item 2.02: NVDA, AAPL,
MSFT, AMZN, GOOGL, META, AVGO, 2010 on) → 344 reaction sessions in `data/edge_lab/cache/events.csv`; edge_lab day fact
`earn` (`load_events`). Cascade on those mornings +7.04 pts/trade on 217 trades but random direction beaten only 74 %
(t 1.2), years swing −706 … +874 pts → no sizing candidate. CPI release dates could not be fetched (BLS / ALFRED refuse
scripted access). Every idea testable on owned or free data is now tested.

## 2026-10-16 (later) — cascade refinements (pre-registered): none beats the plain cascade

`research/preregistered/2026-10-16_cascade_refinements.md`. edge_lab's managed exits gain **add a unit** (`add_r`: buy one
more at that R in favour, both stops to the entry, the added unit pays its own cost) and a **trailing stop** (`trail_r`:
after +1R the stop follows the best price by that many R); tests (548 checks). Results vs the base +4.80 pts/trade:
re-entry (≤ 2 trades/day) +3.62, adding at +1R +4.62, trailing after +1R +2.56, first check at 09:45 / 09:55 / 10:00
+3.57 / +4.42 / +3.98 (the edge is not tied to one minute; 09:50 stays best), 2 units on VIX < 20 days better only in
2023–26. The fleet keeps the plain cascade.

## 2026-10-16 (later) — the cascade in the first 2 hours of every session (pre-registered): New York only

`research/preregistered/2026-10-16_cascade_all_sessions.md` (founder: "test the cascade on all sessions first 2
hours"). Same rule at the Globex (18:00), Tokyo (20:00) and London (03:00) opens, exit at the end of the 2 hours or held
to the next session, and the NY cascade cut at 11:30. Only `cascade_asia_2h` passes the gates (+4.3 pts/trade, 18/yr)
and it is not validated (whole-period best variant +0.4, random entry times did better, frozen holdout negative /
insignificant). Tokyo and London fail; the NY cascade cut at 11:30 makes +2.2 vs +4.8 held to 15:55. Fleet unchanged.
Also from the session analysis of the 2021–26 replays (no code): of ~4,700 fleet strategies in the NY first 2 hours and
~550 in London / Asia, none is significant in both 2021–23 and 2024–26; the best both-period positives are index beta
(`beta_day*`), faded trend rules (`trend_gap_go_20`, `globex_keltner_20_60m`) or noise.

## 2026-10-16 (later) — chart / candle patterns and the condition matrix (pre-registered): nothing validated

`research/preregistered/2026-10-16_patterns_and_conditions.md`. 18 pattern tests (engulfing, pin bar, inside / outside
bar, 3-bar reversal, double bottom/top, structure break, flag, MTF engulfing — standalone and in the cascade's direction,
controls 99.7 %): two pass the gates (`pat_3bar` +2.5, `pat_double_casc` +3.2) but neither had an edge in its development
years (frozen holdout); the rest fail. Condition matrix (breakout / reversal / trend / pullback × none, VIX ≥ 20, VIX < 20,
VIX/VIX3M ≥ 1, VVIX ≥ 100, 50-day trend, no FOMC; controls 99.8 % + both holdouts positive): no combination validated —
at this strict bar the plain cascade's random-day control (96 %) fails too, while its frozen holdouts stay positive (t
2.1–2.5). Unvalidated but notable: the cascade on VIX < 20 days and on VVIX ≥ 100 days.

## 2026-10-16 (later) — pipeline and data validation; two look-ahead holes and a stale ATR fixed

`research/validation/2026-10-16_pipeline_validation.md`. Data integrity clean (NQ/ES: every full day has its 09:30 bar
across DST, ~0.1 missing RTH minutes/day, no duplicates or bad bars, no roll jumps). **Look-ahead:** `lag()` now refuses a
negative lag; the pair price `xs` and the meta model's ES input use the latest bar that has started, never the next one
(tests added; re-runs unchanged). **Null markets:** 20 passing / near-miss ideas on two synthetic NQ histories with every
real structure destroyed → 0 of 40 pass (the cascade nets ≈ −cost there). **Parity** edge_lab vs paper engine on
the recorded sessions: same days, directions and entry times — and one real mismatch: **`PaperDb::session_atr14_before`
(paper engine + executor regime ATR) returned yesterday's stored ATR14, one day stale vs research**; now the mean of the 14
sessions before today (verified equal to edge_lab's ATR). Changes the stop size of every ATR-sized paper/dry strategy
from tonight's deploy; the golden snapshot is re-frozen with its diff kept for review.

## 2026-10-16 (later) — the prop plan against reality: Monte Carlo and measured live costs
**Fastest payouts:** Select **Flex** 50K with **6–8 MNQ in the evaluation and 2 MNQ funded** — median first payout
≈ 16 weeks at the corrected 1.5 pts/MNQ (real 2019–26 sequence: 82 trading days, paid within a year from every start),
~1 in 3 within 3 months, ~3 evals bought (Daily 50K ≈ 23–25 weeks; 25K accounts are not faster).

`research/2026-10-16_prop_plan_reality.md`. **Corrected the same day:** a first reading took `entry_slippage_ticks`
(UNSIGNED |fill − signal|) as a cost and reported 2.4 pts per MNQ. New `scripts/execution_costs.sh` computes SIGNED costs
from the stored prices: MNQ entries ≈ 1 tick better than the signal (median 0), stop exits ≈ 3 ticks worse, commission 0.91
→ ≈ **1.3 pts per MNQ round trip** (the research's 1.5 is conservative); `latency_logger.hpp`'s comment now says the
field is unsigned. The order path was not changed: entries are not where execution loses money. Original reading:
Live trades (45, 09-21 … 10-09): entries "slip 4.6 ticks" (unsigned), submit → fill median 1.2 s.
At that cost the cascade nets +4.38 pts/trade (random-day control 94 %) and the weak-day long +14.0 (passes). Monte
Carlo of year 1 (10,000 years from real 2019–26 months, Select Daily 50K, monthly withdrawal): cascade + VWAP/stall at
4 MNQ — first eval passes 45 %, any payout 73 %, first payout ~day 101, 3.1 months paid, $2,871 mean (worst 10 %: $0),
4.7 evals bought; at 2 MNQ 64 % / 58 % / $1,540 / 2.0 evals. Year 1 is far weaker than the continuous replay.

## 2026-10-16 (later) — free macro / options data as day filters (pre-registered): none adopted

`scripts/edge_lab.sh macro` downloads free FRED (10-/2-year yields, broad dollar) and CBOE (VVIX, SKEW) daily files and
merges them (curl + awk) into `data/edge_lab/cache/macro.csv`; edge_lab day facts `vvix skew y10 y2 y10c usdc` (last
observation dated before the day; test added, 543 checks). `research/preregistered/2026-10-16_free_macro_filters.md`:
cascade on VVIX ≥ 100 days +9.35 pts/trade vs +2.51 on the rest (fails controls; noted as a sizing hypothesis),
cascade after big yield moves +5.56 vs +4.91, weak-day after falling yields +29.8 (passes) vs +27.5 on the rest, weak-day
after a dollar jump +16.3 vs +22.4. Adoption needed the removed trades to lose money — none did.

## 2026-10-16 (later) — mega grid of entries / stops / targets / exits (pre-registered): no fleet change

Founder: "test as many variants as possible" (`research/preregistered/2026-10-16_mega_grid.md`). edge_lab gains
**break-even** (`be_r`: stop to entry once price has gone that many R in favour) and **partial exits** (`partial_r`:
half out at that R, the rest's stop to entry) — `simulate_managed`, 5 tests (539 checks) — and writes every variant's
in-sample result to `reports/<id>.variants.csv`. Cascade 2,800 variants (thresholds × stop 0.15–0.5 ATR × target
none/1–5R × break-even × partial): walk-forward +4.08 (worse than the 54-variant grid's +4.80); frozen-holdout pick beats
the current setting in 2019–22 but not in 2023–26 → kept. **All 2,800 variants positive in both holdouts.** Weak-day
3 × 384 (IBS × VIX × stop × target × break-even × exit 12:00/14:00/15:55): only 15:55 survives; its pick (VIX ≥ 25 only)
is better per trade but makes less in total — the fleet's existing +1 MNQ at VIX ≥ 25 already captures it.

## 2026-10-16 (later) — VWAP / EMA re-test and a frozen-setting holdout of every winner

- **VWAP / EMA, untested versions only** (`research/preregistered/2026-10-16_vwap_ema.md`, 5 tests at 99 %): VWAP
  hold on trend days −0.2, VWAP reclaim −0.5, EMA pullback with the 60-minute trend +2.1 (controls), cascade with a
  60-minute EMA filter +3.7 (worse than plain, not adopted). `ema_cross_vwap` (5-minute EMA9/21 cross in the day's
  direction, price beyond VWAP and the open) PASSES the gates at +6.79 — but the walk-forward stood aside in 109 of 184
  months, its whole-period best variant is +1.81 (t 1.6), and the frozen holdout shows no edge before 2019. Not in the
  fleet.
- **Frozen-setting holdout** (`research/preregistered/2026-10-16_frozen_holdout.md`; founder: "find winning runs and
  test that in data it has never seen before"): each winner's setting chosen on 2010–18 (or 2010–22) only, tested once
  on 2019–22 (or 2023–26). The cascade holds best (+6.9 / +10.0, t ≈ 2.4); weak-day +21 / +67 (t ≈ 1.8–1.9); strong
  open +6.0 / +13.0; ES cascade +1.3 / +1.2. Added to `config/working_fleet.json` evidence. Truly unseen data is only the
  forward paper test.

## 2026-10-16 (later) — owned-data ideas, pre-registered: nothing passes

Founder: "test all. I cant have more expenses" (`research/preregistered/2026-10-16_owned_data_ideas.md`, 9 tests,
controls at 99.5 % of 400 seeds). FAIL: trend-follower MA-cross trigger (18:00 +7.4 pts/trade but direction 75 %, loses
2010–18; 09:30 −1.1), failed-cascade reverse (+5.5, negative 2023–26), NQ–ES divergence (−2.4), Bitcoin weekend → NQ
Monday (09:30 +8.1 on 86 trades, negative 2023–26; Sunday 18:00 −3.1), the cascade on BTC / ETH at 10 bp cost (−2.9 /
−3.1 bp), and volatility-regime sizing in the payout sim (26 % of months paid vs 60 % for a fixed 4 MNQ). Not tested
(no free data): other markets, earnings days, CPI / auction days.
- **VIX term structure** (`research/preregistered/2026-10-16_vix_term_structure.md`; founder: "more mechanical"):
  edge_lab day fact `vts` = VIX / VIX3M (prior close, owned CBOE files). Cascade in backwardation −1.75 pts/trade (144),
  in contango +5.13 (fails random-day control 97 %), weak-day with the term-structure gate +25.6 on 222 trades (fails
  direction 94 %, 2024 −912). No filter adopted.
- edge_lab: day facts `sma100`, `sma200`, `xma` (net 50/100/200-day cross of yesterday's close); spec key `pair` with
  `xs` (the pair's move from its open in its own ATR, same minute) and `xr` (the pair's return since this market's prior
  RTH close); markets without a micro on our prop firm skip the prop gate (reported n/a); `scripts/edge_lab.sh export
  <X>LOG` = <X>USDT on a log scale (1 pt = 1 bp). Tests 534 checks.

## 2026-10-16 (later) — the working fleet: one list, written rules, two forward-test candidates

Founder: "run test and lets come up with our fleet of working strategies".
- **`config/working_fleet.json`** — every strategy that passed edge_lab (core: `so_cascade_2r`, `anyweak_day__vix20`,
  `es_cascade_2r`), the variants compared with them (`so_cascade_2r_vwap`, `_vwap_stall`, `so_cascade_3r_s30`,
  `es_cascade_k25_3r`) and two forward-test candidates, each with its rule, evidence and a keep / drop / promote /
  retire rule written before forward data exists (forward from 2026-10-19). **`scripts/working_fleet.sh`** scores it
  from `paper_trades` (`backfill` argument = the recorded-session replays, for orientation only).
- **New trend modes (paper only)** `asia_sweep` (fade a 5-minute sweep of the 20:00–03:00 range whose aggressors are
  trapped) and `round_break` (first 5-minute close beyond the 100-point band around the 09:30 open with flow behind it);
  keys `lf_delta_min`, `lf_stop_atr`, `round_step`; fail closed without aggressor data; 7 tests in
  `test_trend_strategy`. Paper ids `lf_asia_sweep`, `lf_round_break` at edge_lab's best fixed setting (a 0.05, stop
  0.25 ATR, 2R) — near-misses of the pre-registered box tests, forward-tested, never live on that alone. Deployed and
  backfilled from 2026-09-17 18:00 by tonight's after-close job.
- **Weak-day long, pre-registered fixes** (`research/preregistered/2026-10-16_weak_day_fixes.md`): exits and the meta
  model both FAIL — every thesis exit throws the full-day recovery away (+1.4…+2.3 vs +13.2). Kept as is, small size.
  edge_lab: no-stop ideas now use 0.25 × ATR as R in the meta model and the stall exit.

## 2026-10-16 (later) — search for strategies uncorrelated with the cascade; edge_lab months + correlation; prop uses the right micro

Founder: "find 2 more uncorrelated strategies" (target: ≥ 60 % positive months, low monthly correlation with
`so_cascade`, so a book of accounts pays most months).
- edge_lab reports now carry **Months**: share of positive months (all, since 2019), longest run without one, and the
  monthly correlation + "this or it positive" share against `corr_with` (default `so_cascade`, `weak_day_long`;
  `reports/<id>.monthly.csv`). Information, not a gate.
- **Bug fixed:** the prop gate valued every market as MNQ ($2/pt, 1.5 pts). It now trades the symbol's micro (MNQ $2 /
  1.5 pts, MES $5 / 0.6 pts; other symbols refused). Only ES ideas changed.
- 9 new ideas (all with a why): `es_cascade` **PASS** (+1.10 ES pts/trade, PF 1.20, 7/7 robust, all eras positive; MES
  break-even fee $1,495 at 2 MES, $1.9k/yr at 6 MES) — but monthly correlation with the NQ cascade 0.57, so half a new
  strategy. Dead: `overnight_drift` (dir 94 %; being long in cash hours makes more, +5.6 vs +3.1 — it is index beta, not an
  overnight premium), `overnight_asia`, `data_drift`, `quiet_pm_break`, `on_overshoot_fade`, `range_day_fade`,
  `highvix_vwap_fade` (−5.3), `weekend_gap_fade` (96 trades, controls), `postclose_drift`.
- **Paper (ES fleet, label `es`, MES $5/pt):** `es_cascade_2r` (mirror of NQ `so_cascade_2r`, all knobs ATR-relative) and
  `es_cascade_k25_3r` (edge_lab's in-sample best on ES: k 0.25, stop 0.25 ATR, 3R). Registered and backfilled
  (2026-09-18 … 10-16, label `backfill`) by the same after-close job as the NQ VWAP ids.
- **ORB 5 / ORB 15** (founder: "check for orb 5 and orb 15"; `orb5`, `orb15`: first 1-minute close beyond the range,
  stop = other side of the range or k × ATR, 1.5–3R, optional narrow-range filter, 36 variants each): both FAIL at
  controls — ORB 5 +1.52 pts/trade (random direction beaten in 84 %), ORB 15 +2.48 (88 %); both lose in 2010–18
  (−1.25 / −1.84) and win since 2019; positive in 42 % / 56 % of months since 2019; monthly corr 0.6 with the cascade (the
  same open flow, a worse trigger). The narrow-range filter and the range stop add nothing.
- **ORB 5 / 15 with order flow** (`orb5_of`, `orb15_of`: break taken only when RTH aggressor delta since the open
  agrees by ≥ f × volume; 2023-03 on, 6-month training): ORB 5 +5.49 pts/trade (433 trades), ORB 15 +4.09 — both FAIL at
  controls (random time / random day beaten in only 44–70 %; ORB 5 beats random direction 96 %). Same window without the
  flow condition: ORB 5 −0.95, ORB 15 +2.51, so the flow filter lifts ORB 5 by ~6 pts/trade — but 2024 carries every
  version (ORB 5 flow: 2023 −175, 2024 +1,939, 2025 −512, 2026 +1,125 pts) and the history is 2.5 years. The paper ids
  `ofo_ny5*` (since 10-14) agree it is not settled: recorded-session replay flow-agree 11 trades +$369 vs no filter 17
  +$779 vs flow-disagree 6 +$409. Monthly corr 0.55 with the cascade. Not an edge yet; the paper ids keep running.
- **ORB + order flow, round 2 — pre-registered** (`research/preregistered/2026-10-16_orb_order_flow.md`, founder: "test
  all. remember we dont want to overfit"): 8 tests written before any run, controls at 99 % for the family, ≤ 8 variants,
  development data to 2026-03-19, collector sessions after as a holdout. **Nothing passes** (table in the file): range-
  window flow, far-side liquidity pulled (positive every year 2023–26 but controls 58–96 %, and no live depth feed),
  absorption at the range edge (−2.3), a flow-flip exit (helps ORB on ~20 trades, nothing on the cascade), flow/book
  inputs to the meta model (no gain). New edge_lab pieces: `bk(field, n)` (order-book minutes from `mbo_1m`, exported as
  `NQ.book.csv`), `dwin` / `vwin` (aggressor delta / volume in a fixed window), exit rule `flow`, spec key `to`,
  `meta --book`; 9 new tests (523 checks).
- **Liquidity boxes × order-flow entries — pre-registered** (`research/preregistered/2026-10-16_liquidity_boxes.md`;
  founder: "a box between liquidities or orb or levels … entries with orderflow"): 15-minute range, overnight
  high/low, yesterday's high/low × sweep-and-reclaim fade / break-and-hold follow, flow 2023–26 at 99 % controls, plus
  price-only references 2010–26. All 12 fail (flow: −6.1 … +3.6 pts/trade; price-only sweeps lose on 16 years).
- **Every other liquidity box × order flow — pre-registered** (`research/preregistered/2026-10-16_liquidity_boxes_all.md`,
  founder: "test everything"): equal highs/lows, Asia and London ranges, yesterday's value area, naked POC, VWAP ± 2 sd,
  last week, last month, 100-point bands, consolidation × sweep / break, flow 2023–26 at 99.75 % (20 tests), plus
  price-only references 2010–26. **Nothing passes.** Closest: 100-point-band break with flow (+12.4, direction control
  99 % < 99.75) and Asia-range sweep with flow (+10.4, 96 %), but their whole-period best variants make only ~+5.5
  (t ≈ 1.3). New edge_lab pieces: `whi`/`wlo`, `eqhi`/`eqlo`, `floor`/`ceil`, day facts `ppoc pvah pval pwh pwl pmh pml
  npoca npocb` (RTH volume profile, last week/month, naked POCs); tests (530 checks).
- **Bug fixed: `hh(tf, n)` / `ll(tf, n)` always returned NaN** (the loop in `Market::series` never ran). Only today's
  `cnbox` specs used them (re-run); the StarNet desk's copy of edge_lab was told to re-copy and re-run its hh/ll ideas.
- The VWAP thesis exit does **not** carry to ES (every exit rule fails on `es_cascade`) — a caution on the NQ result; its
  paper ids decide.
- Book sim (Select Daily 50K, 2019–26, one withdrawal a month): NQ cascade 4 MNQ alone 60 % of months paid; + weak-day
  1 MNQ 70 %; + ES cascade 20 MES 78 % ($12k/yr, 8.6 evals/yr — the ES account burns most of them). Sign-only, cascade +
  weak-day + ES cascade have a positive month 91 % of the time.
- Trap recorded in research/README.md: `px` is the decision bar's open, so `px > hi_since(…)` is never true — use a
  completed bar (`cl(5,1) > lag(5, hi_since(9,30))`).

## 2026-10-16 (09:00 ET) — edge_lab meta (take/skip) and thesis exits on the cascade

Founder: copy what profitable discretionary traders do, programmatically — option 1 (skip the bad signals) and 2 (exit
when the reason for the trade is gone). New `src/research/meta.hpp`, `edge_lab meta|exits SPEC`, `scripts/edge_lab.sh
meta|exits`, gates in `research/README.md`, tests in `test_edge_lab` (514 checks: planted rule learned, no future
labels reach a year's model, base exit = the edge_lab path, VWAP exit leaves a reversing trade early).

Results on `so_cascade` (NQ, 1.0 pt real cost, walk-forward trades):
- **Thesis exit — PASS.** Close when a 5-minute bar closes back through VWAP (hard stop kept): +5.58 vs +4.80
  pts/trade, PF 1.36 vs 1.22, max drawdown 911 vs 1,244 pts, beats the path-blind random-timing twin in 100 % of 200
  seeds (twin +8,150 vs +12,951 pts). With the 60-minute stall rule: +5.82, PF 1.41. 2010–18 is slightly worse
  (+0.77 vs +1.04); the gain is 2019–26. Prop (Select Daily 50K, 4 MNQ): 32–40 evals over 14 years instead of 72,
  $2.7–3.2k paid/yr per account vs $2.4k. Walk-forward over all 8 rules (the honest multiple-testing number): +5.25
  vs +5.00/trade. Open-cross, stall alone, open runner, 15-minute trail: fail.
- **Meta-label — PASS, but borderline.** Trees take 59 % of signals: +8.30 vs +5.70 pts/trade (2014–26), random skip
  beaten in 96.2 % of seeds, shuffled labels in exactly 95 %, ridge cross-check barely better (+5.84). Prediction
  quintiles are not monotonic (Q4 +10.7, Q5 +4.6). Sensitivity (not tuning): 7 of 8 model settings improve
  (+5.4…+9.6 vs +6.35 on 2015+), 5 bins makes it worse. Leans on overnight range, trend vs SMA50, overnight position,
  yesterday's IBS. Treat as a candidate for forward paper, not an edge.
- **Paper fleet:** TrendConfig `drive_exit_vwap` (a completed 5-minute close back through the RTH VWAP closes the
  drive trade) and `drive_exit_stall_min` / `drive_exit_stall_r` (no ≥ r × stop in favour after N minutes → close);
  both off by default, 5 tests in `test_trend_strategy`. Ids `so_cascade_2r_vwap` and `so_cascade_2r_vwap_stall`
  (60 min, 0.5 R) beside `so_cascade_2r`. Deployed after the close by a one-shot user timer (build/ paper_engine →
  restart paper-engine-local → backfill the two ids 2026-09-18 … 10-16 under `backfill` → golden re-freeze only if
  nothing but the new ids changed → make hermes; log `data/logs/cascade_vwap_deploy.log`).
- **Monthly payouts (sim, Select Daily 50K, 2019–26):** withdrawing the maximum as soon as allowed leaves the balance
  at the +$2,100 line, so a losing week stops the next months; ONE withdrawal per calendar month pays in 49–60 % of
  months (cascade + VWAP/stall, 2–4 MNQ, 0.4–0.8 evals/yr, $5–7.7k/yr) vs 18–31 %. More copies of the same strategy add
  nothing (same months); the weak-day long on a second account adds a little (61 %). Ceiling: the cascade is positive
  in 67 % of months; cascade or weak-day positive in 85 % (monthly corr −0.05).
- **Control bug caught before trusting it:** the first random-timing twin re-timed only the trades the exit rule had
  closed — trades selected on their future (they later went back through VWAP) — and "beat" every rule by 2×. The
  twin is now a path-blind time exit over all trades with the rule's exit rate and holding times. The exit fill now
  also counts in the trade's MAE (prop breach test).

## 2026-10-16 (morning) — edge_lab: one pipeline every strategy idea goes through; the dead list re-tested at real cost

### Added
- **`edge_lab`** (`src/research/{bars,expr,edge_lab}.hpp`, `edge_lab_main.cpp`, `scripts/edge_lab.sh`, `research/README.md`,
  tests `test_edge_lab`, in hermes). An idea is one JSON file in `research/ideas/` (rule language over 1-minute NQ/ES bars
  2010-07 → now, a mandatory "why"). Gates: screen at the REAL cost (NQ 1.0 / ES 0.35 pt) → walk-forward (12-month
  trailing pick) → controls (200 seeds: random direction, random time with the rule's own direction, random day in the
  same month) → robustness (≥ 5/7 incl. eras, cost × 1.5, entry 1 bar late, ES, plateau; latest era must be positive) →
  prop fit (MNQ 1.5 pt through `select_daily_50k` eval → funded → re-buy). Report per idea in `data/edge_lab/reports/`,
  results in Postgres `research_ideas` / `research_runs` (`migrations/20261016_research_ideas.sql`). ~0.5 s per idea.
- Day facts `mtd` (month-to-date) and `prehol` (holiday follows) for calendar-flow ideas.

### Research (29 ideas, all in `research/ideas/`, `scripts/edge_lab.sh list`)
- **PASS (3):** `weak_day_long` (weak close → buy the open, hold to 15:55; +13.2 pts/trade net OOS, PF 1.36, beats
  random day 100 %, all eras +), `so_cascade` (+4.8, PF 1.22, 11/16 yrs), `strong_open` (+4.2, PF 1.17; a subset of the
  cascade). All three are tradeable only at today's real cost; 2010–18 is ≈ +1 pt/trade.
- **Skipping FOMC days does NOT help the cascade** here (+4.64 vs +4.80 pts/trade; day control 93 % vs 96 %) — the
  2026-10-15 "ADOPT" came from a 2.41-pt in-sample run. Keep `skip_fomc_day` off until forward data says otherwise.
- Last-hour momentum passes everything except the latest era (2023–26 negative) → FAIL. Near misses: month-end
  rebalancing (direction control 86 %), narrow-range breakout (93 %, 13/16 yrs).
- Dead at real cost: EMA cross / 60m-pullback, VWAP fade / follow, ORB, Asia / London open drives, gap fade / failed gap,
  midday trend, late SO alone, turn of month, Monday fade, pre-FOMC drift, pre-holiday, Friday de-risk, Tuesday
  turnaround, overnight-trend open, half-hour→last-half-hour. Vol-target de-lever short loses −15.9 pts/trade = the
  weak-day long from the other side.
- Control design notes (found while building): a random-time control that keeps the real trade's direction leaks the
  future (it beat the cascade 2:1); a random-day control drawn from the whole year scores the walk-forward's month
  choice. Both fixed; tests pin them.

---

## 2026-10-16 (night) — FOMC skip for the trend engine, the cascade as a dry-run 50K instance, flow-trade research

### Added
- `skip_fomc_day` (TrendConfig, off by default): no entries on an FOMC decision day. Hosts now pass today's FOMC
  flag as the second argument of `set_event_day(event, fomc)` from the `calendar` table (kind `fomc`, 137 rows
  2010-01 … 2026-12) — paper_main (start, day rollover, warm-up roll) and the executor (start, daily reset).
  Evidence: the every-day cascade on decision days PF 0.54 at real cost, t −3.8 (2010–26). 4 tests in
  `test_trend_strategy` (FOMC day blocks, other event days and normal days trade, off = unchanged).
- Paper ids `so_cascade_2r_nofomc`, `so_cascade_3r_s30_nofomc` (fleet 6,635). No backfill: 2026-09-18 … 10-15
  holds no FOMC day, so they equal their bases there; the next decision days are 2026-10-28 and 12-09.
- `config/tradeify_cascade_config.json` → `nq-executor-local@tradeify_cascade` (dry_run, label
  `tradeify_cascade_dry`, tag CASCADE): the cascade with the FOMC skip, sized as a Select 50K evaluation (4 MNQ,
  −$1,000 daily, $2,000 trail). Founder: "trade what works". Real-cost evidence: positive only since 2019.

### Research (scratchpad `rF_flows/`, no repo code)
- Five new flow hypotheses with priors written first (turn of month, mid-month payroll, leveraged-ETF last hour,
  week after quarterly expiry, VIX-expiry Wednesday), NQ + ES 2010–26, random-date controls: none positive in all
  eras after costs, none beats its control on both markets. Quarterly-expiry Fridays are untestable (NQ.c.0 ends
  09:29 on those days). Nothing to adopt.

### Fixed
- **NQ quotes had not reached Postgres since 2026-10-12 21:59 (Cyprus).** One collector BBO batch held two rows with
  the same (symbol, exchange, ts_event); `ON CONFLICT DO UPDATE` rejects such a statement whole ("cannot affect row a
  second time"), and the BBO WAL re-sent that batch plus everything after it on every flush — 6.2 M rows / 348 MB held,
  `bbo` / `book_1m` frozen (order-book gates, book overlays and the chart ran on no quotes). `TickDB::write_bbo` now
  merges same-key rows in the batch first (present side wins, absent side keeps — the ON CONFLICT rule); test
  `write_bbo_same_key_in_batch`. Found by the cost agent (book_1m had no NQ rows after 10-12).
- `scripts/collector_watchdog.sh`: `START_GRACE_S` (300 s) after the unit last became active. The collector's start
  (`SELECT COUNT(*) FROM ticks` over 123 M rows, schema statements, WAL replay) takes > 2 min on a cold cache, and the
  2-minute watchdog restarted it in a loop — 01:24–01:40 ET 10-16 lost ~17 min of Asia ticks.
- The held quotes are kept at `data/backups/ticks.wal.bbo.pending-20261016` (6.2 M rows, 10-12 → 10-16) — NOT yet
  loaded: the collector replays a WAL as one statement at start-up, which blocks live ticks for minutes. Load it in
  the weekend close (put it back as `ticks.wal.bbo`, restart the collector).

### Ops
- 2026-10-16 00:50 ET: a dashboard `SELECT 'live' …` sat in ClientWrite for 8 h holding `paper_trades`; the paper
  engine's `ALTER TABLE … ADD COLUMN IF NOT EXISTS` queued behind it and every paper-trade INSERT, the 02:40
  pg_dump and both restarts queued behind that (same pattern as 2026-10-14). Terminated with
  `pg_terminate_backend`.

## 2026-10-16 — every strategy family swept with walk-forward selection; the box theory fails; a three-strategy fear book

### Research (bar-level re-implementations of the fleet's families on NQ 1m 2010-07 … 2026-10, 2.41 pts/trade, $ per MNQ;
### `sweep.py` / `refine.py` in the session scratchpad — not fleet replays, so the live engines can differ in detail)
- **Box theory (founder: "ORB is set — top we sell, bottom we buy"): no.** 36 variants (5/15/30-min box, stop ¼–1 box, target
  middle / far side / hold, ± trail), limit fills at the edge: best PF 0.87, 3/17 years positive, gross edge ≈ 0 before costs
  (+0.04 box per trade). The mirror (ORB breakout, 600 variants) is PF 1.00 at best — the opening range alone carries no edge
  in either direction; the live ORB's results come from its other rules, not the box.
- **Walk-forward over 24 families × session windows (3,933 variants):** each year trade the variant that was best over all prior
  years. Only four cells earn anything out of sample — `weak_day` (prior close weak + VIX ≥ 20 → buy the open, 9–10/13 wf years
  positive), `so_cascade`, `supertrend` 60m RTH and `range_break` with order flow (2023+ only). Donchian, Keltner, EMA pullback,
  VWAP trend/fade, Bollinger fade, RSI-2, IB break, prior-day levels, failed breakouts, NR7, EMA ribbon, thrust fade, ORB retest,
  fib pullback, volume burst, delta divergence: negative after costs in EVERY window (NY open / 10–12 / PM / Asia / London), no
  parameter choice would have helped (hindsight > 0, blind and walk-forward < 0).
- **Refinement (1,840 variants):** weak_day is robust across ibs 0.25–0.4 × VIX 20–25 (PF 1.5–2.2, $1.2–1.3k/yr); entering at
  09:45 beats 09:30 by ~$15/trade; VIX ≥ 28 is PF 3.3 but trades only 8 of 17 years. **The mirror does not work** (strong close →
  short: PF 0.5–0.8 in 2023–26) — it is buy-the-fear, not mean reversion. A VIX rise ≥ 8 % the day before is an alternative
  trigger (PF 1.9, 13 tr/yr). Strong open: one check at 09:50, k 0.25–0.3 × ATR, hold to close, PF 1.2–1.3; **taking it only when
  it agrees with the weak-day bias (no shorts on fear days) lifts PF to 1.44, 8/8 wf years positive.** Supertrend(14, 3–4) on
  60/120m bars in RTH, exit on the flip, NO stop: PF 1.2–1.3, $470–740/yr, 10/17 years (any stop hurts it). Gap fade ≥ 0.75 ATR:
  PF 1.2, era-unstable. Order-flow ORB (`ofo_ny5`): PF 1.30 in-sample 2023–26, 2023 negative — unchanged from 10-14.
- **Trend-day pullback study (`study.py`, same scratchpad): 19 trend-day definitions (move ≥ 0.2/0.3/0.4 ATR at 09:50/10:30/
  11:00; efficiency ≥ 0.6/0.75; range ≥ 0.5/0.7 ATR; one side of VWAP) × 8 pullback definitions (0.1/0.2/0.3 ATR off the running
  extreme, 38/50/62 % of the leg, VWAP touch, 5m EMA20 touch) + market-at-detection × limit/resume × stop 0.25/0.125 ATR × 2R/hold/
  back-to-extreme × 1/3 trades = 3,876 variants, plus the same grid twice with the trend-day DIRECTION randomised per day.**
  Verdict: dead. Best real cell by walk-forward +0.35 R/trade (move 0.4 @09:50, 0.1-ATR limit, hold; 163 trades) against a best
  randomised cell of +0.27 R (same cell shape, 287 trades) — the grid's maximum is inside what no information produces. Every top
  real cell has NEGATIVE neighbours (mean of its 12 sub-variants −0.06 to −0.10 R) and is negative in 2010–18. Per regime (2019–26)
  the mean over all pullback variants is ≈ 0 or below while the mean of market-at-detection variants is +0.15 R for `move 0.3
  @09:50`; the pullback "best" beats the market "best" only because it has 8× more cells to pick from. In-sample 2019–26 the real
  grid is shifted right of the control (mean −0.03 vs −0.07 R, 31 % vs 20 % of variants positive): the 09:50 direction carries
  information, pullback mechanics mostly destroy it. Later detection (10:30/11:00, efficiency, range, VWAP-side) is weaker than
  09:50 in every form. Visual summary published as the "NQ Strategy Evidence" artifact.
- **Regime scan (`regimes.py` / `regimes2.py`): 24 pre-09:50 features — opening / first-5-min / overnight / prior-day volume,
  prior-day range, gap, overnight range and move, VIX level and change, realised vol 5/20, ATR14/ATR50, SMA20/50 position,
  09:30–35 range, prior-day efficiency and IBS, ES agreement, opening aggressor share, open outside prior range, weekday,
  turn of month — each cut in thirds and scored by the 09:50 momentum trade's R on signal days (2019–26), against a 300-shuffle
  permutation ceiling.** Two beat it: prior-day efficiency LOW (a balance day yesterday: +0.66 / +0.43 R in 2019–22 / 2023–26 vs
  +0.05 / +0.23 after a trend day; same best bucket in all three eras; p = 0.017) and the first 3 trading days of the month
  (+0.55 / +0.59 vs +0.20 / +0.07; p = 0.047) — about one of 24 would pass by chance, so the efficiency finding is a candidate
  for a paper variant, not a rule. Volume in every form, VIX level, realised vol, ES, weekday: p ≥ 0.28. Low opening volume halves
  the number of signal days (14 % vs 44 %) and does not improve them beyond noise. No regime makes small-move days worth
  following (every-day follow ≤ +0.17 R in its best bucket, +0.04 overall).
- **General settings + significance (`robust.py`, `zscores.py`):** each setting scored by the mean PF of its one-step
  neighbours, cells pooled over sub-variants. Weak day: the whole ibs 0.2–0.4 × VIX 20–28 region is PF 1.4–2.6 (100 % of 184
  variants PF > 1); general setting **ibs ≤ 0.3, VIX ≥ 20, buy 09:45, sell 15:55** (t 2.5 all years, 2.4 in 2019–26, yearly t 2.1,
  year-bootstrap P(≤0) 0.0 %, PF 1.56, 13/16 yrs). Strong open: k 0.3–0.5 × stop 0.2–0.3 all PF > 1.2 at 09:50; the PF peak
  (k 0.5) has 162 trades in 16 years, too thin; general setting **k 0.3, stop 0.3 ATR, hold to 15:55, no shorts on fear days**
  (t 3.5 in 2019–26, yearly t 2.4, bootstrap 0.2 %, PF 1.44, 10/17 yrs; over all years t 1.5 because 2010–18 is −1.3 at today's
  cost). Supertrend: 60m×4 / 120m×3 no stop, t 2.3 / 1.9 per trade but yearly t 1.2 / 1.5 (money in a few years, bootstrap 9 / 4 %).
  Turn of month t 1.9 recent, bootstrap 3 %. Cascade (t −2.9 all, 2010–18 −6.5) and gap fade (t 0.7) do not clear. All are
  post-selection survivors of thousands of variants: only the weak day and the 2019–26 strong open stay convincing after a
  selection haircut.
- **The book (weak_day + SO-agree + supertrend60):** $44k/MNQ over 16 years, 10/17 years positive — but $42k of it is 2020–26,
  yearly P&L correlations 0.4–0.7, and 2010–17 is flat to negative for all three. They are one bet on volatility regimes, not
  three independent edges; size them as one.

### Ops
- The 10-14 Postgres lock pile-up recurred 2026-10-15 ~23:50 Cyprus: a dashboard live-trade export stuck in ClientWrite, the
  paper_engine `ALTER TABLE paper_trades ADD COLUMN mae_pts` queued behind it, every later paper_trades INSERT / SELECT blocked
  (fleet not recording for 35+ min at the time of writing). Fix is `pg_terminate_backend(<ClientWrite pid>)`; the session could
  not run it (sandbox). The paper_engine should not issue DDL at startup while a reader can hold the table — move the migration
  to `migrations/`.

## 2026-10-15 (afternoon) — every-day momentum cascade on paper; direction, VWAP-band, prop-sizing and RL research

### Added
- `drive_late_last_min_atr` (`trend_strategy.hpp`, default −1 = off): replaces `drive_late_min_atr` on the LAST late
  check; 0 = follow any non-zero move. Paper id `so_cascade_2r` = `so_k25_2r_late` + a 4th check at 12:00 that follows
  the day's move → one trade on ~99% of days; `so_cascade_3r_s30` = the same with stop 0.3 ATR and 3R. 5 checks in `test_trend_strategy`. Off by default (golden unchanged).

### Research (2010-07 … 2026-10 NQ 1m, today's cost/ATR; scripts in the session scratchpad)
- **Cost caveat:** all figures below charge today's cost as a share of ATR in every year. At the real 2.41 pt per
  trade the cascade is PF 1.07 over 2010–26, negative in 2010–18 (cost was 6 % of ATR then vs 1 % now), +$8–10/MNQ per
  trade in 2019–26 — the edge only beats real costs since ~2019.
- Cascade (09:50 k.25 → 10:30/11:00/11:30 k.3 → 12:00 any): 249 tr/yr, PF 1.16, +0.022 ATR (~$13/MNQ), all eras,
  13/17 years; without the 12:00 step 169/yr PF 1.22, 15/17. The strong-open threshold was partly chosen in-sample.
- Prop sizing: 25k ($1k DD) fits 1–2 MNQ (one NQ stop = $1,450); 1 NQ with 15–50 pt stops: targets give PF < 1, hold-to-
  close has PF 1.2 but 11–33 % wins → 60–90 evals/yr. 50k Select Daily at 4 MNQ ≈ 11 payouts / $7.6k per account-year,
  break-even eval fee ~$500. Copies multiply money, not timing (a payout in ~1 month of 3); mixing strategies with a
  direction lock spreads timing slightly but cuts money ~⅔. Without a lock ~30 % of days would have opposite positions
  across accounts (Tradeify FTA §6.7).
- Direction signals (2023–26 order flow): VWAP side, VWAP ±SD bands, aggressor delta, book imbalance, 50-day trend and
  votes — 42 tests, none beyond chance. VWAP SD-band fade over 2010–26 on 1m (72 cells) and 5m (120 cells): 2/72 and
  0/120 positive in all eras; exits at VWAP always lose.
- Cascade robustness: survives +3 pt extra slippage per trade (PF 1.08), gone at +5; 3R / hold-to-close and a 0.3 ATR
  stop are a bit better (PF 1.16–1.19, 16/17 yrs at stop 0.3); thresholds chosen on 2010–18 hold on 2019–26 (all 45
  cells positive out of sample, chosen cell +0.027 ATR); ES same rules PF 1.06 (≈0 in 2010–18); weak when VIX < 15.
- RL: tabular Q-learning for daily size on a 50k (train 2010–18, test 2019–26): +$2.5k/yr vs fixed 4 MNQ +$7.3k; the
  learned policy is noise — fixed sizing stays.

## 2026-10-15 — late strong open + looser day-weakness in the paper fleet (research r1)

### Added
- `opening_drive` late re-checks (`trend_strategy.hpp`): `drive_late_count / drive_late_minutes / drive_late_step /
  drive_late_min_atr` re-check |price − 09:30 open| ≥ x × ATR at 10:30 / 11:00 / 11:30 only on days the main decision gave
  no direction (before any score filter), one late trade per day; `drive_late_only` makes the main decision a skip test.
  Off by default (golden replay unchanged). 9 checks in `test_trend_strategy`.
- Paper fleet (5 ids): `so_k25_2r` (strong open 09:50, k 0.25 day ATR, stop 0.25 ATR, 2R, hold to 15:55),
  `so_k25_2r_late` (+ late checks at k 0.30), `so_late_k30_2r` (late trades only), `wkday_ibs30_vix20` and
  `wkday_ibs50_vix20` (prior IBS ≤ 0.3 / 0.5 and VIX ≥ 20 → buy 09:30, sell 15:55).

### Research
- Late SO: +92 trades/yr on non-SO days, +0.10R net, all three eras, weekly P&L ρ ≈ 0 with the base; picked from ~18
  time × threshold cells — paper forward test decides. Like the base SO it is net negative in 2010–18 at that era's cost
  share. IBS ≤ 0.5: +10 trades/yr at the same edge. ES adds no independent trades (ρ 0.94); weakness with VIX < 20 = coin
  flip; SO filters (EMA vs VWAP, VIX, prior day, volume, overnight range, ES confirm, weekday) — none added; closing-auction
  proxies — nothing for NQ (ES month-end 15:50→16:00 = hypothesis only).

## 2026-10-14 — Order-flow ORB on NY 5m + Asia 15m in the paper fleet; a 13-hour DB lock pile-up cleared

Founder: "any orb on any session 5m or 15m … possibly order flow confirmation", then "yes do both and re-run the payout math".
- **Fix: `session_stats.atr14_pts` ran 8 % low (founder: "yes fix it", after a validation agent found it).** The ATR /
  volume / prior-close baselines in `refresh_session_stats()` counted any stored row as a session, including a 1-bar
  stray print on Sunday 2026-09-27 (range 0) and the collector's 13-bar partial first day — NQ atr14 for 10-14 read 265.8
  instead of 289.7. Both the paper engine and the executor read it (`session_atr14_before`, e.g. `rb_min_range_atr` on
  `ofo_ny5_wide`). Baselines now take only rows with ≥ 60 RTH bars (half days, ~210, still count);
  `refresh_session_stats(25)` recomputed the collector era (172 rows); the minute timer re-applies the file cleanly.
  The Databento history had three more such rows (NQ+ES 2018-06-15 1 bar, 2020-06-30 41 bars, ES 2023-06-16 1 bar);
  atr14 / vol_vs_20d / range_vs_atr / gap of the sessions in the 30 days after each were recomputed with the same rule.
- **Research (scratchpad `sessions2.py` / `of_focus.py`, 726 configs, Asia/London/NY × 5m/15m × break/fade/retest ×
  regimes):** nothing passes all eras 2011–2026 without order flow. With it (2023-03+, the only years with aggressor
  volume): first 1m close beyond the range, taken only when the range window's net aggressor volume Σ(2·bv−v)/Σv agrees
  with the break, stop at the other side, hold to the session end — **NY 5m PF 1.27, +11.4 pts, 130/yr; Asia 15m PF 1.24,
  +3.6 pts, 118/yr**; disagreeing breaks PF 0.97 / 0.79. London loses in every variant. The filter was chosen on these
  same years (in-sample); a random-direction control passed 0 times.
- **TrendConfig `range_break` gains `rb_of_filter` (+1 agree / −1 disagree / 0 off), `rb_first_only`, `rb_stop_range`
  and `rb_last_entry`.** `rb_stop_range` hands the range's other side to the paper broker (`TrendStrategy::take_stop_hint()`
  → `PaperBroker::set_stop_hint()`, only tightens `sl_points`) — **paper only**: the executor refuses a config carrying it
  and the leaderboard marks it not live_runnable.
- **Fleet:** `ofo_ny5`, `ofo_asia15` + controls `_dis` (order flow disagrees) and `_all` (no filter), family `of_orb`,
  registered under `tradeify`. Replay 2026-09-21..10-13 (label `ofo_check`): the C++ trades match the research rule
  11/11 (NY) and 13/13 (Asia) on entry minute and direction; P&L differs by the paper slippage. Paper engine restarted
  06:22 ET, warm-up clean, no collector queue drops. Tests: test_trend_strategy (+6), hermes 22/0/0.
- **Payout math (scratchpad `pay_of.py`, 2023-03..2026-10 only):** best use is the order-flow ORB in the EVAL (book 2 +
  OF 2 MNQ: pass median 29 days vs 44 for the book alone) and the book alone once FUNDED (1 MNQ) — 5 sequential $80
  accounts: net +$7.8k / 12 months, profit in 86 % of starts, first payout by month 3 in 27 % (book alone 17 %), months
  paid 5.1/12. Adding the ORB to the funded account LOWERS the result (wide stops vs the $2K trail). Month 1 stays ~1 %.
- **VIX-fall doubling (same day, founder: "yes add them"):** 162 regimes × 8,935 fleet strategies (8.3 M trades 2023+,
  pick 2023-24 / judge 2025-26, shuffled-label baseline) — no regime makes the fleet profitable (median PF 0.42 → ≤ 0.57,
  winners no more frequent than chance). On the order-flow ORB, only "prior VIX close fell ≥ 3 %" held in both periods on
  both sessions (NY PF 1.79 / 2.11, Asia 2.07 / 1.64; ~29 % of sessions). New TrendConfig `vix_chg1_max` (VixDay `chg1`,
  passed by paper + executor `set_vix`); fleet `ofo_ny5_vfall` / `ofo_asia15_vfall` = a second copy on those days, i.e.
  2× size together with the base id. Replay 09-21..10-13 picks the expected 7/7 days. Payout sim (eval book 2 + ORB 2,
  ORB 2× after VIX falls; funded book 1): 5 sequential accounts — profit in 96 % of starts, first payout by month 3 in
  34 % (27 % without), months paid 5.6/12; buying 1/month → a payout in 56 % of months 3-14, avg $1,730/mo. Month 1 still ~0 %.
- **Fixed — stale VIX regime (found by the validation review, same day):** `VixRegime` was loaded once at startup and
  `before()` returned the last row however old, so from day 2 a long-running paper_engine/executor gated on a stale VIX
  (all `hold_vix_*` / `vix_chg1_max` variants). Now `reload_if_changed()` runs at every `apply_vix` (paper + executor)
  and a close more than 5 calendar days old → unknown (gated variants stay out). The `vix_fetch.sh` cron ran at 16:45
  BOX time (Cyprus = 09:45 ET), before the close it needed: moved to `30 0,2 * * 2-6` (00:30 / 02:30 Cyprus = 17:30 /
  19:30 ET, after CBOE publishes; crontab backup in the session scratchpad). Tests: VixRegime chg1 / weekend / stale.
- **Independent validation (5 agents, same day) — corrections:** NY order-flow ORB reproduces (PF 1.27) but the CI
  is [0.99, 1.62], the OF filter itself is not significant (permutation p 0.12) and the top 5 days are 66 % of the
  profit → fragile. Asia order-flow ORB: the research dates dropped 32 losing trades (quarterly-expiry Fridays,
  holidays); on every weekday it is PF 1.17, 1.06 at 4 pts cost, < 1 without its top 5 days → treat as an artefact.
  VIX-fall doubling: NY consistent (p 0.047 uncorrected, not after multiple testing), Asia not shown. Book: no
  look-ahead; strategy 2 (midday) 93 % of its 2023-26 profit is 2025 → fragile; strategy 3 is lumpy (one day = +2,121
  pts); strategy 1 robust but modest. Payout sims reproduce exactly but ignore the 90/10 split, the $250 minimum and
  2025's weight: realistic first payout month 3-6, 30-55 % of months paid buying 1/month, $500-1,700/month.
- **Wide-range copy (`rb_min_range_atr`, same day):** research round 3 (honest protocol): flow fade/absorption outside the
  opening range — no edge (best 2023-24 config PF 1.13 → 0.84 in 2025-26); ORB + 10:00 move "PF 2.51" was look-ahead
  (decided at 10:00 it is 1.17-1.32); ORB on weakness days — no regime beats random day sets except "VIX fell ≥3 %"
  (PF 1.90, p 0.038 / 0.074); wide 09:30-09:35 ranges (≥ 0.21 × prior ATR14, the 2023-24 median) PF 1.43 (1.38 / 1.49) vs
  1.02 tight. New TrendConfig `rb_min_range_atr` (unknown ATR = no trade), fleet id `ofo_ny5_wide`; tests +3, hermes 22/0/0.
  **Out-of-sample 2010-2022 (plain ORB, no order flow exists then):** the NQ 5m ORB itself loses before 2019 (PF 0.67 /
  0.89 / 1.02 / 1.15 for 2010-14 / 15-18 / 19-22 / 23-26); "VIX fell ≥3 %" adds nothing before 2019 (0.61, 0.84; random
  p 0.76 / 0.61) and helps only 2019+ (1.21 p 0.08, 1.53 p 0.03); wide range adds nothing. These are recent-era effects.
  GC / CL have order flow only since 2026-09-28 (11 sessions) — not testable yet.
- **Incident found while testing:** the dashboard backend (`python -m ui.app`) left a `SELECT` stuck in ClientWrite for
  13.5 h (it stopped reading its socket); a research replay's `ALTER TABLE paper_trades` queued behind it, and every
  paper-fleet `INSERT INTO paper_trades` plus the 02:40 `pg_dump` queued behind the ALTER (~12 h of blocked trade writes).
  Cleared with `pg_terminate_backend` on the stuck read; the queued writes and the backup then completed. Root cause
  (backend reads not bounded) is still open — see the "Dashboard backend wedges" note.

## 2026-10-12 — Select Daily 50K config for the combo book; executor skips CME holidays / early closes

Founder: buying a Tradeify Select Daily 50K, "we just need to make sure it works".
- **Engine = research:** combo3 replayed through the C++ engine on NQ 1m bars 2010-07..2026-03 (label `combo_verify`,
  id `combo3_verify`): 709 trades vs 706 research days, avg $40.5 PF 1.55 vs $38.8 PF 1.49, median same-day diff $2;
  the day mismatches are mostly month-start days (the replay starts each month cold).
- **Fixed — holiday sessions:** 18 replay entries were CME holidays / early closes (Labor Day, Thanksgiving, Christmas
  Eve, July 4, MLK, Presidents', Juneteenth, Memorial Day); the 15:55 exit found the market shut and the position rode to
  the 18:00 reopen. `OrbDB::calendar_close_et_min()` + `market_close_et_min()` / `market_closes_before_flatten()`
  (orb_config.hpp): a trend engine whose flatten time is at/after that day's close takes no entries (holiday or early
  close from the `calendar` table, re-applied after every rollover and after the warm-up). Calendar rows cover
  2026-11-26 … 2027-09 — extend them yearly.
- **New `combo_stack` (TrendConfig, default true):** false = combo3 enters at the config qty whatever the signal count.
- **`config/tradeify_select50_config.json`** (dry_run, account_id placeholder): any combo3 signal → 2 MNQ at 09:30,
  sell 15:55, stop 36 pts/contract (= payout_sim's −$150 day stop incl. costs), $2K trail, −$1K DLL, consistency cap OFF
  (the 0.3 cap would freeze the book after its first big day). Sim (P5): eval pass 51 %, 12 % blown/60d, ~2.8
  payouts/month. Unit `nq-executor-local@tradeify_select50` runs it dry. Tests: test_trend_strategy (+10), hermes 20/0/0.

## 2026-10-12 — payout_sim: firm rules re-read from the firms' own pages, + Lucid Flex and MFF Rapid EOD

`src/paper/payout_sim.hpp` plans(): Apex split into `apex_eod_50k` / `apex_intraday_50k` ($2K trail, not $2.5K;
payouts need 5 days ≥ $250/$200 and a $52,600 balance), `topstep_50k` payout share 1.0 → 0.45 (≤ 50 % of the balance,
90/10), new `lucid_flex_50k` and `mff_rapid_eod_50k`. Unstated terms are marked ASSUMED in the comments. Result
(ledger P5): on the C1 combo book the daily-payout plans (MFF Rapid, Tradeify Select Daily) pay best; 5-day-cycle
plans pay slowly on ~30 signal days a year. `test_payout_sim` 28/28.

## 2026-10-12 — executor warm-up now rolls sessions, so a restarted hold engine knows the prior day

**Fixed:** the pg-feed warm-up replayed ticks into the strategy, but the session rollover is
driven by the wall clock (`today_date_str()`), so no `reset_session()` happened during the replay.
Hold mode only learns `prev_*` / `prev2_lo_` in `reset_session()`, so a restarted `tradeify_day`
/ `tradeify_combo*` had no prior day until it had run live through a whole session and an 18:00 ET
rollover (two of them for combo3). Every restart, Monday start or crash cost the next signal
silently. Now the warm-up rolls the session whenever a replayed tick's trading date changes
(`src/execution/executor_main.cpp`, pg_feed_loop). The three dry configs warm up 6000 min (back to
Thursday's open from a Monday restart). Verified live: three replayed rollovers, the reset line no
longer says "no prior day yet", and combo3 found Thursday 10-08's wide down day (range 578 pts vs
1.5 × ATR 445) as Friday's signal in the replay. `make hermes-fast` 20/0/0.

## 2026-10-12 — the combination book (ledger C1) runs dry on NQ and ES

Founder: "there must be a strategy or a combination of strategies that make money", then "run everything in the
matrix", then "yes" to forward-testing the result.

### Added
- `hold_combo3` (TrendConfig, hold mode, long only, default off): one unit per daily signal that is on at the prior
  close — weak close (IBS ≤ `combo_ibs` or close below the session-before low) with VIX ≥ `combo_vix_min`; big down
  day (body ≤ −1 ATR); wide down day (range ≥ 1.5 ATR, closed down). Entry at `hold_entry_hhmm` when ≥ 1; the count
  goes to `last_units()` and the executor caps it with `qty_max`. New `prev2_lo_` (the session before the prior one).
  5 tests in `test_trend_strategy`.
- `config/tradeify_combo_config.json` (MNQ, NQ ticks) and `config/tradeify_combo_es_config.json` (MES, ES ticks):
  `dry_run: true`, 09:30 → 15:55, 1–3 units, no stop; units `nq-executor-local@tradeify_combo{,_es}` enabled,
  labels `tradeify_combo_dry` / `tradeify_combo_es_dry`.

### Research (ledger C1, M1–M4)
- C1: the three signals, chosen on 2010–18 only and scored on 2019–26 (NQ + ES stacked, 1 micro per signal, cap 3):
  451 days, $158/day, PF 1.70, 8/8 years, max DD −$6.3k. Payout sim (test 2020–26): Select Flex 50K at 3 MNQ EV
  ≈ $2,200/account, eval pass 45 %, ~59 % blown within 60 days — it pays across several accounts, not one.
- M1–M4 (93 daily variants: 4 market phases, effort vs result, cause & effect, waves): 2 held out of sample, both
  repeat what C1 already trades; adding them by train-only selection lowered the 2019–26 result (PF 1.72 → 1.60).

## 2026-10-11 (late night) — validation pass: stale replay binary, consistency-cap freeze, broker refusal logs

Founder: "validate everything". S1 (day-session weakness long) reproduced from raw `bars_1m` in SQL and by a paper_engine MBO
replay of the fleet ids over Mar–Apr 2025 (ledger S14): prior-day values identical, trades match day for day, P&L within fills.
Two defects surfaced: (1) `build-hist/paper_engine` was built before today's hold filters and silently ignored
`hold_prev_ibs_max`/`hold_weak_any` (traded every day) — rebuilt; the 2,088 `nq_bars` rows it had written for the hold ids
(2021-06…11) were deleted and re-replayed with the current binary (529 rows). (2) The per-strategy `RiskManager` consistency
cap (paper fleet: 100 %, halt persists until a manual clear) froze a paper strategy for the rest of a replay after one day that
out-earned all its prior profit — a hold-mode profile. `consistency_cap_pct <= 0` now means OFF (`risk_manager.hpp`), the
paper fleet sets 0 (`paper_config.hpp`; the live executors keep their 30 % Tradeify cap), `PaperBroker` logs the two formerly
silent entry refusals (halt reason, cooldown), and `TrendStrategy::hold_daily_ok` logs the prior-day hi/lo/close/IBS/ATR it
decided on. Test `consistency_cap_zero_is_off` (test_risk_manager 44/44). All four paper fleets restarted on the fixed
binary with CME closed. Three new intraday ideas measured and closed (S11 NQ/ES pairs, S12 FOMC-day, S13 gap fade).
Hermes 22/22.

## 2026-10-11 (night) — intraday-only search closed; research ledger S1–S10

Founder: prop firms forbid overnight holds, so the weakness × VIX book is research-only. Measured every intraday door with the
data on the box (`docs/intraday_search_2026-10-11.md`, `scripts/sql/research_tests_2026-10-11c.sql`): the day-session leg of
the weakness edge survives (buy 09:30 after a weak close when VIX ≥ 20, sell 15:55: $53/MNQ, PF 1.60, 14/16 years, 33/yr) and
nothing else does — timing/dip/stop mechanics, 5,853 fleet strategies on the regime days, at-open conditions, the last hour,
a weekly-consistency search over 9,295 strategies, crypto hour/session sweeps and a walk-forward order-flow model are all at
or below cost out of sample. Payout cadence of every intraday sleeve on Select Daily 50K: 1–6% of weeks. No code change;
**Correction (P3):** the conviction-stack payout numbers of the evening (P1: 53% of 2-week windows, 28%/yr blown) are invalid —
the export carried one row per unit and `payout_sim` tested each unit's MAE separately; as one position the stack breaches a
$2,000 trail ~12×/yr. Venue sim (P2: FTMO Swing / Phidias Premium) finds no venue where the book pays weekly (FTMO 100k at
minimum size ≈ $700/month). `tradeify_book` keeps running dry for the record; the intraday sleeve runs dry as `nq-executor-local@tradeify_day`
(`config/tradeify_day_config.json` + `.env.tradeify_day`, label `tradeify_day_dry`, tag `WEAK_DAY`, 1 MNQ +1 at VIX ≥ 25, qty_max 2).

## 2026-10-11 — conviction sizing in the executor (the payout-sim book, dry-run)

The weakness×VIX book was simulated as a 1–4 unit stack and the executor could only size from `qty`. Now the strategy asks:
`TrendConfig` gains `stack_wc_units / stack_vix_units / stack_both_units` (+ `stack_wc_ibs` 0.3, `stack_vix_min / stack_vix_rise_min /
stack_vix_pct_min`, OR-gated). In `hold` mode `TrendStrategy::stack_units(dir)` runs right before each entry emit (dip and clock paths):
`1 + wc + vix + both`, logged `[TREND hold] conviction stack: N unit(s) (weak close …, VIX fear …)`, read back via `last_units()` (0 =
not configured). `executor_main` passes it to `OrderManager::set_entry_units()` before `on_signal`; the OM takes it over
`RegimeState::qty_for()`, caps with the new `OrbConfig::qty_max` (0 = off), floors at 1 and consumes it once. The two "fill qty exceeds
expected position size" warnings compare against `max(cfg.qty, position.qty)` so a 4-lot fill on a 4-unit entry is not flagged as
unowned. Tests: `test_order_manager` (`entry_units_from_strategy_capped_by_qty_max`), `test_trend_strategy` (4 / 2 / 2 / 1 / off).
`config/tradeify_book_config.json` now runs the stack (base = any weakness, `qty 1`, `qty_max 4`, strategy tag `WEAK_STACK`), still
`dry_run: true` on `nq-executor-local@tradeify_book`. Hermes 22/22. Paper fleets are untouched — PaperBroker sizes from the fleet's
`qty`, so the stack is live/dry-executor only (the per-unit P&L of the paper twins is what payout_sim stacked).

## 2026-10-11 — "do all" (second batch): firms, crypto, exits, earnings nights

- **Firm comparison in `payout_sim`** (MFF Rapid 50K from its own checkout page; Apex / Topstep from third-party summaries —
  approximations): for the 4-unit book, 250-day accounts, test 2020–26 — Tradeify Select Daily 53 % of 2-week windows,
  1.8 payouts/month, blown 28 %/yr; MFF Rapid 43 %, 1.5/month, blown 53 % (real-time trail, no DLL, daily payouts above a
  $2,100 buffer, min $500); Apex eval the easiest (95 % pass in a median 31 days, $2,500 trail) but payouts need 5
  qualifying days → 19 % of windows; Topstep 100 % blown ($1K DLL + 5 winning days); Growth worse than all. **Tradeify Select
  Daily stays the plan**; Apex is the cheapest eval to pass if an account is only needed as a pass. (Lucid: site blocked,
  third-party only: Flex = no DLL, no funded consistency, 5-day cycles.)
- **Conditional exits**: hold-to-close beats every early take / cut / target / trail on the gated book (research_tests X1).
- **Earnings nights**: +$38 vs +$48 per MNQ — mild, keep trading them (X2).
- **Crypto**: BTC "weak & own-fear" PF 1.46 but fading, TSMOM long bull-era only, ETH weaker, shorts negative — a crypto-prop
  sleeve at most (X3).
- **Multi-account model**: with 92 % pass / 28 %/yr blown / ~$1.6K per account-month, N funded accounts scale income linearly
  and make resets a budgeted cost; cadence per account does not change (same days pay).
- Not done (needs live fills): 18:00 entry execution study; not possible (paid data): options positioning / auction imbalance.

## 2026-10-11 — the switch (founder: "we wanna switch strategies to the ones that will work" → stop ORB now)

- **Live ORB stopped** (`nq-executor-local@tradeify`, +$337 / 30 trades since 09-21; the Growth 25K has ~$90 of room and
  cannot take any strategy). **MTF-Asia dry run disabled** (−$489 / 37). **Live-rota timers disabled** (`live-rota-*.timer`;
  the `sig_*` strategies have no proven edge). The account is idle — Tradeify wants ≥ 1 trade per week to stay active.
- **The book runs as its own dry-run executor instance**: `config/tradeify_book_config.json` (+ `.env.tradeify_book`, pg
  feed), unit `nq-executor-local@tradeify_book`, label `tradeify_book_dry`: any-weakness + VIX gate, buy 18:00, sell 15:55,
  1 MNQ, no orders. Proves the live path (VIX file, calendar, hold filters, EOD) for 20 sessions next to the paper fleet.
  The rsi2 unit arms after 10 RTH closes; the others after 1–2 sessions.
- Still to build before a funded account: conviction sizing in the executor (strategy-requested quantity, so the 4-unit
  stack runs as one instance), and Tradeify's answer on 18:00 entries (parked). Target account: a NEW 50K Select
  (Select Daily); never the 25K.

## 2026-10-11 — VIX regime: the conditioner that changes the size of the edge (founder: "do what we can for now")

### Research (free data: CBOE VIX/VIX3M/VIX9D/VVIX histories, yfinance ETF closes, FOMC dates from federalreserve.gov)
- **Buying weakness at the daily horizon is worth ≈ 0 when VIX < 15 and 3–8× its average when VIX ≥ 20, rose > 10 % in
  5 days, or sits in the top tercile of its year** — monotonic, in every year 2010–2026, NQ and ES (per MNQ, one-session
  18:00 → 15:55 hold: `ibs<0.3` VIX<15 → +$0.4, 20–30 → +$32, >30 → +$81; `rsi2<20 & VIX≥20` 221 trades, +$105, PF 2.09,
  14/16 years, ES PF 1.87; `ANY weakness & (VIX≥18 | rose>10% | pct≥0.66)` 681 trades, +$45, PF 1.51, t 3.4, worst year
  −$257). Backwardation (VIX/VIX3M > 1) and SPY in a drawdown (< 60-day high, below its 20-day mean) say the same thing;
  the unconditional beta hold is NEGATIVE in deep backwardation. Pre-FOMC drift (enter the evening before the decision,
  exit 15:55 on the day): +$41/MNQ, PF 1.55, 12/17 years, 8/year — small; the session AFTER the decision is −$42.
- **Payout simulator, conviction-sized book** (`anyweak` + `anyweak_vixany` + `wc30` + `wc30_vixany` stacked = 1–4 MNQ
  by signal strength and regime), Select Daily 50K, members chosen on 2010–19 and scored on 2020–26, 250-day accounts:
  eval pass 92 % / fail 8 % (median 58 days), **1.8 payouts/month, 53 % of 2-week windows paid, $1,590/month**, blown 28 %
  per year (7 % before the first payout). Flex: 23 % of windows. This is the first construction that meets the founder's
  payout goal in more than half the windows.

### Width test (founder: "there has to be") — 15 futures + 18 ETFs, daily, 2009–2026
- The weakness-long edge is an **equity-index phenomenon only** (NQ, QQQ, IWM, SPY, ES; YM/RTY marginal); commodities,
  rates, FX and grains are negative on it; strength-shorts, time-series momentum and own-volatility gates show nothing after
  costs. A first run produced t 7–9 "short edges" on bonds/grains/silver — a sign bug (cost ADDED on shorts) caught by the
  unconditional-baseline check, plus roll-gap drift in unadjusted continuous futures (ZN −3.4 ATR/yr of close→open gaps).
  So there is no second uncorrelated daily book in price action; the only diversification is NQ + ES (correlated).
  Ledger: `research_tests` M1.

### Stack-size curve (founder: "ignore [the Tradeify email] for now" → sizing), Select Daily, 250-day accounts, test 2020–26
- 50K: 1 unit (any weakness, 1 MNQ) eval pass 41 % / fail 59 %, blown 89 %/yr; +gate unit 79 % / 21 %, blown 56 %; +weak-close
  unit 90 % / 10 %, blown 32 %, 1.6 payouts/month, 47 % of 2-week windows; **4 units 92 % / 8 %, blown 28 %, 1.8/month, 53 %,
  $1,590/month**. Bigger conviction stacks are SAFER here: the eval is a race between +$3,000 and the −$2,000 trail, and the
  extra units fire only on the strongest days, so edge per day outruns the drawdown. 25K accounts do not work for this book
  (the $1,000 trail is too tight even at 1 MNQ: blown 57–100 %). Day-only fallback (2 units on VIX ≥ 20 days, 09:30 → 15:55):
  eval 47 % / 22 %, 1.2 payouts/month, 40 % of windows, blown 36 %, $430/month.

### VIX × intraday (founder: "can we combine vix with an intraday strategy")
- Split by the prior close's VIX: the intraday families go from losing to *less* losing (fleet 2024–26: ORB PF 0.36 → 0.81,
  trend 0.33 → 0.56, mtf ≤ 0.54, lv_conf flat); the 16-year ATR-ORB and a 16-year Keltner(20, 2 ATR) breakout have no
  positive VIX bucket. **Gap-and-go** is the one rule that turns mildly positive under the fear gate (bars 2013–26: +6.8
  pts/trade, PF 1.31, 8/14 years; off the gate PF 0.85); the tick replay's `trend_gap_go_20` is PF 3.2 on VIX ≥ 20 days,
  6/6 years 2021–26. The best intraday use of the regime is the **day-only weakness hold** (09:30 → 15:55 after a weak close,
  VIX ≥ 20): +$50–70/MNQ, PF 1.5–1.8, 11–13/16 years — about two-thirds of the overnight-session form, the fallback if
  Tradeify refuses 18:00 entries. Tick-replay "VIX ≥ 20 winners" like `keltner_ride_20_15m` (PF 2.15, 2021–26) do not
  survive the 16-year bar test: 2022 artefacts.
- Engine: a **generic VIX entry gate for every mode** (`vix_min / vix_rise_min / vix_pct_min / vix_any`, checked in
  `can_enter`; `hold_vix_*` keeps the hold-mode semantics via the same `vix_gate_ok`). Fleet (NQ): `trend_gap_go_{20,20__tr25,40}__{vix20,vixany}`,
  day-only holds `anyweak_day__{vix20,vixany}`, `rsi2_20_day__vix20`, `wc_ibs30_day__vix20` (+ the four `es_` day holds).
  Test: `vix_min on donchian`.

### Added
- `scripts/vix_fetch.sh` (CBOE VIX + VIX3M histories → `data/regime/`, fail-open; cron 16:45 ET weekdays — the crontab is
  not version-controlled, this line is its record; backup `backups/crontab.bak-vix-fetch-20261011`) and
  `src/paper/vix_regime.hpp` (`VixRegime::load/before`: level, VIX3M, 5-day change, trailing-252 percentile; the row for a
  trading day is the last close strictly before it).
- `hold` mode gates: `hold_vix_min`, `hold_vix_rise_min`, `hold_vix_pct_min`, `hold_vix_any` (OR), and `hold_weak_any`
  (the IBS / streak / RSI(2) / fear-body filters combine with OR = "any sign of weakness"); `TrendStrategy::set_vix`.
  paper_main and nq_executor load the regime at start and every rollover (unknown → gated holds stay out, logged).
  `set_event_next_day` is now also fed in the executor. 7 new checks in `test_trend_strategy`.
- **Calendar**: 137 historical FOMC decision dates (2010–2026) loaded into the `calendar` table, so `hold_pre_event` and
  `news_break` event-only variants see them in replays.
- **Fleet** (NQ + ES): `anyweak_h1`, `anyweak_h1__vixany`, `anyweak_h1__vix20`, `wc_ibs30_h1__vixany`,
  `wc_ibs30_h1__notcalm`, `rsi2_20_h1__vix20`, `rsi2_20_h1__vixany`, `fear18_h1__vixrise`, `fomc_drift_h1` — all
  one-session holds (Tradeify-legal pending the 18:00-entry confirmation). Engine replay 09-18..10-09 reproduces the
  research's days (`anyweak` 09-25/10-01/10-05/10-07/10-09, `wc30_notcalm` 10-01/10-05/10-07, every VIX-gated variant out
  in a 14–16 VIX regime). It also showed `hold_pre_event` counting NFP eves (10-01 entry before the 10-02 NFP) — new
  `hold_pre_event_fomc_only` (+ `calendar_fomc_day` in both DB classes, `set_event_next_day(any, fomc)`) keeps
  `fomc_drift_h1` on FOMC decision days only, as researched.

## 2026-10-11 — "do all": cadence research for the payout goal (founder: "is there anything we can explore … do all")

### Simulator (`payout_sim`, 28 checks)
- **Steady-state cadence**: `gap_med / gap_le10_pct / gap_le20_pct / payouts_per_month_after_first` and `win10_{1,2,3}acct_pct`
  — the share of non-overlapping 10-trading-day windows (from the first payout) holding at least one payout, for one
  account and for 2 / 3 accounts started 10 days apart on the same strategy.
- **Contract-aware portfolios**: members × size never exceeds the plan's contract limit (every member can be in a
  position at once).
- **Two-stage evaluation** (`run_eval(…, boost, boost_until)`): 2× size until half the target, 1× after;
  `eval2x_*` columns.

### Findings (lv_conf family, Select Daily 50K, test = later segments of 2022–2026)
- **Cadence ceiling ≈ one payout in every other 2-week window**: `win10` 47–51 % for every candidate that earns
  ≥ $150/month with ≤ 10 % blown (`trend_ema_pb_9_21_5m__lv_conf` 1 NQ 50 %, `trend_ema_pb_20_50_5m__lv_conf` 1 NQ 47 %
  at 1.5 payouts/month). **Staggered accounts do not raise it** (2 and 3 accounts: identical) — the same strategy pays
  on the same profitable days, so the gaps are market gaps, not account gaps. Different strategies per account would be
  needed, and they share the same market days too.
- **Two-stage eval**: helps `ema_pb_20_50` (pass 37 % → 56 % within 120 days, 0 % fail, median 71 days), hurts
  `ema_pb_9_21` (26 % → 10 %). Sizing the whole eval to 3–4 NQ fails 34–53 % — never.
- **Passive entries kill lv_conf**: limit at signal − 1 tick fills 78–85 % of the time, but the unfilled 15–20 % carry more
  than all the profit (market PF 1.43 → passive PF 0.35, −$15k vs +$12k on 4,222 trades). These are momentum entries;
  they must be taken at market. Research item closed.

### Fleet
- `__lv_conf` variants added to every ORB/trend base of the ES fleet (+24, `config/paper_fleet_es.json`, engine
  restarted) and of the overnight grid (+816, `config/night_fleet.json`, 1,764 ids — replay-only via `night-eval.timer`;
  roughly doubles its nightly replay).

## 2026-10-11 — Tradeify's real rules, the Select Daily plan and an evaluation model in the payout simulator (founder: "we wanna pass with the current tradeify rules")

### Rules read from help.tradeify.co (in a real browser — the help centre 403s plain fetches)
- **All positions must be flat by 16:45 ET every day; no holding overnight.** A trading day runs 18:00 → 17:00 ET, so the
  one-session holds (18:00 → 15:55) sit inside a trading day — to be confirmed with Tradeify support before going live —
  while **every multi-session variant (`*_h2`, `*_h3`, `*_xs`, `ds*_xup`, `fear18_*`, `dc20_h5`) is illegal on Tradeify: paper
  research only.** Trailing drawdown recalculates at EOD but is **enforced live against net liquidation** (an open
  position's MAE can breach it). Select evaluation: $1,500 / $3,000 target, $1,000 / $2,000 EOD trailing, no daily loss,
  **40 % consistency, minimum 3 days**, 1 mini on 25K, 4 on 50K, no time limit. Funded Select Flex: 5 winning days
  (≥ $100 / $150), 50 % of profit capped $1,250 / $2,500 (post 2026-09-01), no DLL. **Select Daily: payouts any day once
  the balance is above a buffer ($1,100 / $2,100), 2× the cycle's profit capped $600 / $1,250, minimum $250, DLL
  $500 / $1,000 — no winning-day count.** Every payout after the first needs a positive cycle (all plans).
- `src/paper/payout_sim.hpp`: `Plan` gains `buffer / payout_mult / min_payout` (Select Daily), `max_minis` (the plan's
  contract limit, 10 micros = 1 mini) and the evaluation parameters; new `run_eval()` (target, consistency, min days,
  live breach at the MAE); plans updated to the official numbers, `select_daily_25k/50k` added. `payout_sim_main`:
  sizes 10 MNQ / 2 NQ, sizes over the contract limit skipped, eval pass / fail / median days in the CSV and report.
  `test_payout_sim`: 26 checks.

### What the simulator says (chosen on the past, scored on the future)
- **A payout every two weeks is not reachable with anything in the fleet** under Flex or Growth (5 winning days per
  cycle: ≤ 4 % within 10 days) nor under Select Daily (best ≈ 6 % within 10 days, 9 % within 20).
- **Safest path**: the `__lv_conf` family (an entry that coincides with a price level — keltner, donchian, ema-pullback,
  ORB-10, ONH/ONL bases; PF 1.4–2.2 over 5 replayed years) on **Select Daily 50K at 1–2 NQ**: eval pass 25–37 % within
  120 trading days with 0 % failed (no time limit, so the rest is "not yet"), then $300–600 / month alive, 0 % blown.
  **Steady state after the first payout** (new `gap_*` / `payouts_per_month_after_first` columns): 1.0–1.5 payouts a
  month, median gap 1–6 trading days, 75–93 % of gaps ≤ 10 days — payouts cluster after good stretches, so "about every
  2–3 weeks on average". The front end is the slow part (eval + buffer ≈ 3–4 months). Its 2025-only window reads 3–4×
  better — that is the strong year, not the base rate. Its 2024 level months are not replayed yet (`data/history/priority.txt`
  now puts `nqx 2024-05 … 2025-12` first).
- **Faster, riskier**: the legal one-session hold portfolio (`wc_ibs30_h1 + wc_ibs30_h1_sl15 + wc_ibs20_h1`) at 1 MNQ on
  Select Daily 50K: eval pass 77 % / fail 20 % (median 88 days), then 17 % paid within 20 days, $840 / month, 10 % blown
  before the first payout; at 3 MNQ 47 % paid within 20 days but 41 % blown first.
- Growth: every hold variant ≤ 0 EV; `lv_conf` 8 % paid within 20 days. Flex: ≤ 6 % within 20 days for everything.

## 2026-10-11 — daily-context holds: the sweep that survived, wired into the fleet (founder: "try all and add to fleet")

### Research (`scratch` SQL/pandas on `bars_1m` + `session_stats`, NQ and ES 2010-07..2026-10, entry at the 18:00 reopen
after the signal session, exit at a 15:55 close, cost 2.41 pts/MNQ, one position at a time)
- Seven families, ~70 variants: weak close (IBS), RSI(2), down-day streaks, fear days, post-FOMC/NFP drift and fade,
  NQ/ES 5-day spread reversion, daily Donchian/SMA trend following, unconditional beta. Bar to survive: t ≥ 2.5, PF ≥ 1.2,
  both halves (≤2018 / ≥2019) positive in ATR units, ES t > 1. **21 survive, all long mean reversion after weakness plus one
  trend variant**; every short, every event rule and the spread fail. Strongest: `ibs<0.1 exit-on-strength` 304 trades,
  PF 3.12, 15/17 years, +$47.9k at 1 MNQ, worst year −$406; `ibs<0.1 hold3` PF 2.05 (ES 2.22); `ibs<0.2 exit-on-strength`
  501 trades PF 2.03 worst year −$226; `down_streak≥2 exit-up-close` PF 1.71; `fear hold2/3` PF 1.9; `rsi2<20 hold1`
  PF 1.61; `donchian20 hold5` PF 1.72. The full table is in the session scratchpad (`sweep.csv`) and summarised in TODO N11.
- Intraday, the same day: 69-indicator walk-forward LightGBM on 15m bars 2013–2026 (IC +0.02..0.04 OOS, 3–4× below
  cost; passive limit fills recover almost nothing — PF 1.02 → 1.03), day-type prediction at 09:45 (range IC 0.51 every
  year, ORB follow-through IC 0.17 every year) that no breakout or trend family converts into profit (ATR-scaled ORB loses
  on predicted high-range days, PF 0.8–0.9; the mid-range quintile is a +1–8 pts tilt, 9/14 years), EMA-pullback-in-trend
  structure at ~0 gross on 5m. The intraday search is closed on evidence; the edge in this data is daily.

### Added
- **`hold` mode daily-context filters + multi-session exits** (`src/execution/trend_strategy.hpp`, all off by default):
  `hold_prev_ibs_max` (prior RTH close position in its range), `hold_down_streak` (k lower RTH closes), `hold_rsi2_max`
  (Wilder RSI(2) of RTH closes), `hold_donchian` (prior close beyond the n closes before it), `hold_above_sma` (n-day SMA
  of RTH closes); `hold_days` (sessions to hold, 1 = unchanged), `hold_exit_ibs_min` (exit at the window end of a session
  that closed ≥ x of its range), `hold_exit_up_close` (exit once a session closes above the prior close). The exit is decided
  ONCE per session at the first window-end minute; the engine keeps a 260-deep RTH close history and counts sessions held
  across the 18:00 reset. 16 new checks in `test_trend_strategy`.
- **Paper only beyond one session**: `nq_executor` refuses a `hold` config with `hold_days > 1` or `hold_exit_*`
  (`--check-config` fails), and `strategy_leaderboard()` marks such ids not `live_runnable`. Multi-session variants switch
  the paper broker's own EOD off (`eod_flatten_hour 24`) — the strategy owns the exit.
- **18 NQ + 18 ES paper strategies** (`wc_ibs{10,20,30}_{h1,h3,xs}`, `wc_ibs10_h2`, `ds{2,3}_xup`, `fear18_{h2,h3,xs}`,
  `rsi2_20_h1`, `rsi2_20_sma50_xr`, `dc20_h5`, ES twins `es_*`), labels `tradeify` / `es`. Golden re-frozen (0 added: they
  enter at 18:00, outside the golden window). Engine replay over 2026-09-18..10-09 (label `wc_check`, log only — the ids
  were registered afterwards) reproduces the research's signal days exactly: IBS<0.1 → 10-01; IBS<0.2 → 10-01, 10-07;
  IBS<0.3 → 10-01, 10-05, 10-07; streak≥2 → 09-25, 10-09; no fear day; and the exits behave (h2 out after 2 sessions, h3
  after 3, `_xs` out the same session it closes ≥ 0.6 of its range, `ds2_xup` out on the first up close).
  One deviation found and fixed: `hold_rsi2_max` fired on 09-29 from a 5-close history — the filter now needs 10 RTH
  closes (α ½ has converged by then). Warm-up note: the close history only starts when the engine starts, so `rsi2_*`
  needs 10 sessions, `dc20_h5` 21 and `rsi2_20_sma50_xr` 50 before they can fire; the one-session filters fire from day 2.

## 2026-10-11 — fear-day hold filters + forward test of the one edge that survived

### Added
- **`hold` mode fear-day filters** (`src/execution/trend_strategy.hpp`, all off by default, in prior-day ATR14 units):
  `hold_prev_body_max` (prior RTH close − open ≤ value), `hold_prev_range_min`, `hold_prev_down`, and a dip entry
  `hold_dip_atr` / `hold_dip_until` (first bar from the entry time to the cutoff whose low reaches open − k × ATR).
  The engine now keeps the prior RTH open (`prev_open_`). Tests: 6 new checks in `test_trend_strategy`.
- **14 paper strategies** forward-testing it: `fear_bigdown_{open,dip25,dip50,open__sl1atr}` and
  `fear_bigrangedn_{open,dip25,dip50}` in `config/paper_fleet.json` (label `tradeify`), the same with `es_` in
  `config/paper_fleet_es.json` (label `es`). Research basis: `research_tests` R9/R10 — buying after a ≥ 1 ATR down
  day (or a ≥ 1.5 ATR wide down day) held on NQ and ES in both 2010–18 and 2019–26. About 15–20 signals a year.
  Engine replay over 2026-09-18..10-09 (label `fear_bf`) matches the research's signal days.

### Research (no code): what did not hold
- Weekly volume profile value-area edges with/without absorption (YouTube order-flow method), late-day reversals,
  intraday "drop from the high" buys, the confluence trend pullback (EMA/VWAP stack, impulse, pullback, trigger),
  and a walk-forward logistic / gradient-boosting model over 14 daily features — all logged in `research_tests`
  (R8, R10–R12) with numbers.

## 2026-10-11 — payout simulator, and three corrections to the history research

### Added
- payout_sim: daily risk rules (`payout::Rules`), train/test split (rules chosen on the first 60% of days), EV per account
  with a reset cost, greedy portfolios; regime `trend_b` (close vs 50-session average, `session_trend`) in `hist_trades_regime`.
- `build/walk_forward` + `scripts/walk_forward.sh` (`src/paper/walk_forward.hpp`, `tests/paper/test_walk_forward.cpp`): walk-forward parameter
  selection per strategy family × session window (asia, london, ny_open, ny_mid, ny_pm) with 3 / 6 / 12-month training,
  against blind and hindsight. Output `data/walk_forward/`.
- `build/payout_sim` + `scripts/payout_sim.sh` (`src/paper/payout_sim.hpp`, `tests/paper/test_payout_sim.cpp`, 13 checks): every
  history strategy, alone and per regime known at the open, through Tradeify Select Flex / Growth 25K / 50K rules at 1/2/3/5 MNQ
  and 1/2 NQ — payout within 10 / 20 trading days, blown before a payout, $ per month. Breaches tested at each trade's MAE;
  Growth trails intraday incl. open profit (MFE first, the conservative order); soft daily loss cuts the day. Output `data/payout/`.
- `scripts/sql/history_done.sql` + `scripts/history_done_sync.sh`: table `history_done` (mirror of the queue's done markers),
  views `hist_trades` (finished months only) and `hist_trades_regime` (+ prop trading day and the regime known at entry).
- `data/history/priority.txt`: `history_queue.sh` reads it first every loop (reorder without a restart). Now: 3 bars-vs-ticks
  checks, then NQ 1m bars 2023-02 → 2010-07 interleaved with `nqx` (level variants) on the finished MBO months.

### Fixed
- **Bar replay filled stops a bar's range past the stop**: `--dbn-bars` turned each 1m bar into 4 prints (O, L/H, H/L, C), so a
  stop between two of them filled at the next one. First bars-vs-ticks check (NQ 2026-01): bar stops −20.4 pts vs −11.2 on
  ticks, breakevens −6.2 vs +0.2, only 56 of 4,479 strategies matched. `hist::bar_path` now walks every 0.25 between the four
  points (≤ 40 prints per leg). `scripts/bars_vs_ticks.sh` + table `history_bar_reliability` decide which strategies' 2010–2023
  bar results walk_forward.sh / payout_sim.sh may use.
- **Half-replayed months were counted as complete** (`history_report.sh` read `paper_trades`): 5 running months entered the
  rankings. It now reads `hist_trades`. `beta_day` fell from +$6,316 to +$1,761.
- **Units: the paper fleet trades MNQ** (`point_value` 2). Every "per contract" history figure is per MNQ; the report says so,
  and the simulator's NQ sizes are 10× gross (its first version labelled MNQ results as NQ).
- **Regime look-ahead**: a 03:20 London trade was tagged with that day's 09:30 gap. `hist_trades_regime` uses the latest
  session whose open (gap) / close (day type) is before the entry; `tcf5_ldn · gap_big` disappears with it.

## 2026-10-11 — price levels in the paper fleet: 1,728 level variants

Founder: "lets implement levels into our strategies" — every level family, every use ("try them all").

- **`src/paper/paper_levels.hpp` LevelState**: one per paper_engine (host-owned like RegimeState), built from
  1-minute bars — seeded from the 45 days of `bars_1m` before the start, then fed every tick (a minute folds in
  when the next starts; a re-fed minute counts once). Families: prior RTH high/low/close + today's open (`day`),
  overnight high/low (`on`), opening range 5/15/30 (`or`), RTH VWAP ±1/2σ + prior closing VWAP (`vwap`), prior
  day POC/VAH/VAL (`vp`), naked POCs (`npoc`), prior week/month high/low (`wk`, `mo`), round numbers scaled to
  price (`round`: NQ 50, BTC 500). The RTH session closes into "prior day" at 16:00 ET, so overnight
  strategies see today's session as the prior day.
- **PaperBroker uses** (OrbConfig `lv_*`, distances × ATR — session_stats ATR14, else the mean RTH range the
  LevelState has seen, so the first days after a data gap still have one): `lv_room_atr` gate (next level
  ahead too close → `lv_room`), `lv_near_atr` gate (no level close → `lv_far`), `lv_target` (resting limit at
  the next level ≥ `lv_tgt_min_atr`·ATR ahead → exit `level_target`), `lv_stop` (stop beyond the nearest level
  behind, + buffer; only ever TIGHTENS `sl_points`). `lv_set` picks the families.
- **Calibration** (scratch tool over NQ 2025-10…2026-02 1m bars): with all families a level sits 0.031 ATR
  ahead at the median, so the gates use the key levels (day,on,vp,npoc,wk,mo): room p25 0.051, nearest p25
  0.026 ATR. Overlays (`scripts/fleet_add_variants.sh`, `ONLY=lv_`): `__lv_room` (0.05, key), `__lv_conf`
  (0.025, key), `__lv_tgt` (0.1, all), `__lv_stop` (min 0.01 + buf 0.005, all) on the 61 ORB + 371 trend
  bases. Golden day: lv_room skips ~30 % of entries, lv_conf ~75 %, lv_stop changes 48/901 trades.
- **Paper only**: `nq_executor` refuses a config with lv_* keys; `strategy_leaderboard()` marks `lv_*`
  overlays not live_runnable. History: the lv variants ride with `nqx` over every NQ month (the `nq` job
  replays the main fleet minus them, per-month config files so concurrent jobs never share one).
- Golden re-frozen: 0 removed rows, +2,993 rows, all `__lv_*`. Tests: 7 new (test_paper_broker 49/49).
  `make hermes` 20/20. Not yet: MTF (bracket broker) bases, the crypto mirrors, a live implementation.
- **A1 profiling** (gprof, `build-prof/`): replays are CPU-bound (262 s CPU / 270 s wall, ~0 system time —
  not Postgres). MBO day: 96 % per-tick (TrendStrategy::on_tick 41 %, PaperBroker::on_tick 25 %, dispatch
  20 %, ORB 10 %); ~half of all CPU sits in libc — every broker and trend strategy converts each tick to ET
  with `gmtime_r` (~185 M calls a bar-week) and compares mode/session strings per tick. → A2/A3.

## 2026-10-11 — history on all the data: night grid + winners on MBO, NQ back to 2010 from 1m bars

Founder: "we need to trade on all the data we have", then "find the best strategies".

- **Bar feed** `src/history/bar_feed.hpp`, `paper_engine --dbn-bars FILE…`: the years with only 1-minute
  bars (NQ 2010-07 … 2023-02) become 4 synthetic prints per bar — open, low/high (low first when the bar
  closed up), close; volume split over the prints, aggressor = print ≥ previous. An approximation: a stop
  and a target inside one bar resolve by that path. Label `nq_bars`, never mixed with tick history.
  `test_dbn` gains the path, sizes, window and roll-overlap cases (101 checks). Smoke week 2015-03:
  33,287 trades, 3,474 strategies, 2 min.
- **history_month.sh**: market `nqx` (night_fleet + staged winners/newmodes, 1,745 ids, merged into
  `data/history/configs/nq_extra.json`, label nq_hist) and `nqbars` (binary `build-hist/`). `nq` and `nqx`
  share a label, so each now deletes only its own ids before a replay (a redo of one never wipes the other).
- **history_queue.sh**: jobs `nqx` × every NQ month and `nqbars` 2023-02 … 2010-07; 6 slots while CME is
  closed (Fri 17:00 → Sun 18:00 ET, `--parallel-closed`), 2 otherwise. Fixed a race: a job just launched
  had not spawned its engine yet, so two slots filled 5 s apart and 3 replays ran on `--parallel 2`.
- **First ranking** (9 complete MBO months, 3,933 strategies ≥ 60 trades): 0 pass the robust gate
  (≥75 % months positive, PF ≥ 1.3, both halves positive); 41 are net positive; ~85 % lose before costs.
  Only 15-minute trend strategies are positive in both halves (`keltner_ride_20_15m__inv`,
  `roc_20_15m__inv`, `trend_supertrend_10_2_15m*`, `trend_gap_go_20`). `beta_on*` tops the raw ranking
  on 2025's overnight drift and loses all of 2026 — not an edge.
- `TODO.md`: the research pipeline (speed → correctness → walk-forward scoring → more strategies).

## 2026-10-10 — tick fan-out: every trade and quote to the dashboard in under a millisecond

Founder: "has to be tick", then "100–400 ms is too slow", then "do the same for NQ". New shared header
`src/tick_fanout.hpp` is a TCP server bound to 127.0.0.1 only. Each client gets one JSON line per trade
(`k:"t"`), quote (`k:"q"`) and closed mbo_1m minute (`k:"m"`). Each line carries a counter `n`, so a
consumer can see a dropped line.

- **crypto_collector**: every aggTrade, every bookTicker (not the 100 ms DB throttle) and every mbo_1m
  minute goes to port 8197 (`CRYPTO_FANOUT_PORT` or `--fanout-port`; 0 turns it off). The summary line
  gains `fanout clients= lines= dropped=`.
- **rithmic_engine (tick collector)**: every accepted trade and BBO goes to port 8198
  (`RITHMIC_FANOUT_PORT`). The 60 s status line gains `fanout … clients= lines= dropped=`. The trade `seq`
  copies db.cpp's rule (rank among identical (symbol, ts, price, size) rows).

The feed never waits on the fan-out:
- `publish()` does nothing when no client is connected.
- Otherwise it `try_lock`s a queue that only the fan-out uses and wakes the fan-out thread through a
  non-blocking pipe. If the lock is busy or the queue is full, the line is dropped and counted.
- A client more than 8 MB behind is disconnected. Only that client loses anything.
- The DB write path, batching and rows did not change.
- The Rithmic fan-out thread never logs, so it shares no lock with the Logger either.

The dashboard's `/ws/market` (bot `ui/routers/market_stream.py`) relays these lines to /live. It reads
Postgres too: once a second to pick up anything the fan-out dropped, and every 100 ms when the fan-out
is down. Tests: 7 new cases in `tests/crypto/test_crypto_collector.cpp`, covering line format, queue and
buffer caps, Rithmic seq rank, in-order delivery over loopback, and a slow client being dropped while
`publish` returns at once.

Measured on BTCUSDT over 60 s:
- Binance trade time to event time: ~150 ms. This happens inside Binance (aggTrade aggregates).
- Event time to the collector receiving it: ~124 ms. This is network and Binance push.
- Collector to fan-out client: 0.1 ms.
- Fan-out to browser socket: 0.3 ms p50, 0.5 ms p95.
- Trade count over the WS matched `ticks` exactly.

The NQ path was checked with unit tests and a fake fan-out only. CME is closed until Sun 18:00 ET.

## 2026-10-10 — NQ history from the owned Databento data: bars since 2010, MBO replays, order flow

Founder: "add all the mbo NQ data we have into the chart and strategies page", then "adjust all strategies on
the mbo data of 3 years and current data … for crypto we do the same". The data (Databento GLBX.MDP3, DBN v1
+ zstd) stays read-only in `/home/theone/Desktop/bot/data/raw`.

- **Decoder.** `src/history/dbn.hpp` is a header-only, streaming DBN reader (v1–v3 metadata, MBO, trades,
  OHLCV). Records are walked by their own length byte. `tests/history/test_dbn.cpp` uses synthetic v1/v2 buffers,
  plain and zstd, and checks the first bar of a real file. Spot checks matched the `databento` Python package
  record for record (MBO, trades, OHLCV, ES).
- **Bars.** `dbn_tool bars` loaded `bars_1m` NQ 5,192,922 and ES 5,438,283 rows, both back to 2010-07, with
  `ON CONFLICT DO NOTHING` (the collector's rows from 2026-09-19 win). Hours rebuilt from those bars equal the
  `ohlcv-1h` file exactly. `buy_volume` and `ticks` are 0 where only OHLCV exists. `dbn_tool mbo-1m` fills them in
  for MBO months. `session_stats` now covers 3,970 NQ and 3,993 ES sessions. Its baselines look back at most 30
  days (ATR), 45 days (volume) and 10 days (prior close), so the first collector day after the 2026-03-20 …
  09-18 gap starts fresh, as before. `PaperDb::session_atr14_before` has the same 10-day bound. Golden replay and
  hermes are unchanged.
- **MBO replay.** `paper_engine --dbn-mbo FILE…` reads MBO directly; nothing goes into `ticks`. Trades come from
  `T` records, with the aggressor taken from the side field (B = buy, at the ask: 127k of 144k prints at the
  touch, the rest sweeps through it, 3 on the wrong side). Top of book is rebuilt per instrument (add, cancel,
  modify, clear; zero unknown order ids over a month). At most one quote goes out per trade or per idle second,
  and only at event ends, so the quote rate is about one per trade (the raw top changes about 14 times per
  trade). A book waits for its snapshot, and the continuous-contract roll switches books. Check on 2026-02-24
  RTH: 400 one-minute bars rebuilt from the replay ticks against the OHLCV file, 396 identical and 4 off by one
  1-lot print at a minute edge, total volume equal. Daily trade counts, volume, high and low for 2026-02 equal
  the Databento trades file. `paper_engine --binance-agg FILE…` replays Binance aggTrades zips with the same row
  semantics as `crypto_backfill.sh` (no quotes).
- **Order flow.** `mbo_1m` (venue `GLBX`, symbol `NQ`, the crypto collector's table and columns) and `book_1m`
  are filled per minute, about 60 s per month.
- **Jobs.** `scripts/history_month.sh {nq|btc|eth} YYYY-MM` runs one idempotent month. NQ months use MBO
  through 2026-03 and the collector's Postgres ticks from 2026-09, under label `nq_hist`; crypto uses
  `crypto_hist`. `scripts/nq_history_replay.sh FROM TO` loops over months. `scripts/binance_history_download.sh`
  downloads data and verifies each zip against Binance's CHECKSUM. It stops if free space would drop below 40 GB.
  `scripts/history_queue.sh run` runs at most 2 replays and SIGSTOPs them in weekday RTH and in Asia. It writes
  `data/history/queue_status.json`. `scripts/history_report.sh` writes `data/history/report.md`: ranking, every
  year, overlay study and the walk-forward `adjust` proposal. Nothing changes a config.
- **Crypto cost model (coordinator audit).** The fixed `commission_rt` ($8.28 BTC) and `slippage_ticks` (41)
  were set at today's price, so 2020 history would have paid fees 3–16× too high. `PaperBroker` and
  `PaperBracketBroker` gain optional relative costs, set from the fleet keys `commission_pct` (round trip, as a
  fraction of notional, half per fill at its price) and `slippage_bps`. Both default to 0, which keeps
  `commission_rt` / `slippage_ticks`; NQ and the golden replay are unchanged (test `relative_costs_pct_and_bps`).
  `crypto_fleet.sh build` now emits `commission_pct` 0.001, `slippage_bps` (BTC 0.49, ETH 0.60), `knob_ref_price`
  and `knob_ratio`. Ids and params are unchanged and re-registered. `crypto_fleet.sh month-config CODE YYYY-MM`
  rescales the point knobs to a month's mean close, so history replays keep them constant in % of price. The
  forward units `paper-engine-local-crypto-{btc,eth}` were restarted on the regenerated configs and the new
  binary.
- **Caveats.** Each month replays cold. NQ.c.0 stays on the expiring contract until the roll Sunday, so about a
  week per quarter is thin (2023-03-16: 97k prints against about 440k on a normal day). There is no NQ data
  2026-03-04 … 03-05 or 2026-03-20 … 09-18.

## 2026-10-10 — every strategy replayed on crypto (BTC and ETH perpetuals)

Founder: "test all strategies for crypto", with history plus MBO going forward. `scripts/crypto_backfill.sh`
loads Binance USDⓈ-M daily aggTrades from data.binance.vision (free, no key) into `ticks` in the collector's
row format, source `binance_hist`. It is idempotent per symbol and UTC day and refuses while CME is open,
because a bulk COPY competes with the NQ collector's writer. Loaded: 2026-09-21..10-09, BTCUSDT and ETHUSDT,
38 coin-days, about 0.4 GB each. `BARS_ONLY=1` loads 1-minute klines straight into `bars_1m` (about 60 KB a
day). That was done for 08-25..09-20 so `session_stats` has an ATR14 from the first replayed day. Binance
stopped publishing futures bookTicker files in 2024, so replayed days have no quotes and book-gated variants
are judged on the forward feed only.

`scripts/crypto_fleet.sh build` mirrors all 4,845 `config/paper_fleet.json` strategies to
`config/paper_fleet_crypto_{btc,eth}.json` (ids `btc_*`/`eth_*`, label `crypto`). One contract is 0.1 coin.
Point knobs are scaled by the coin's mean 09:30–16:00 ET range ratio to NQ over shared sessions (BTC 5.45,
ETH 0.20) and snapped to the tick, as `fleet_expand.sh markets` does. Costs: 0.10 % of notional round trip
(Binance taker) and NQ's 0.75-pt slippage scaled by the same ratio. `register` / `replay FROM TO` (label
`crypto_hist`, refuses in weekday RTH and Asia) / `report` (writes `data/crypto/report.md`, gross and net).

## 2026-10-10 — the live executor leaked one Postgres connection per broker reconnect

Rithmic was down for the weekend, and `nq-executor-local@tradeify` retried the order plant about 110 times.
Each session opened an audit-log connection, but only the normal end of a session closed it. A failed
ORDER_PLANT connect leaves the session early (`co_return`), so every cycle leaked one. After 13 hours it held
88 connections, Postgres refused new connections to every process on the box (`remaining connection slots are
reserved`), and `make hermes` failed its database steps. An RAII guard in the session now closes it on every
exit path. The unit was restarted flat on the fixed binary at 05:30 ET with the founder's OK: box-wide
connections went from 99 to 8 and stay flat across reconnects. `make hermes` 18/18.

## 2026-10-10 — crypto_collector: Binance futures ticks/bbo and Bitstamp market-by-order

New standalone binary `crypto_collector` (`src/crypto/`), so crypto strategies can be tested on history and
forward. It streams Binance USDⓈ-M futures BTCUSDT/ETHUSDT trades into `ticks` and top of book into `bbo`
(exchange `BINANCE`, source `binance_fut`) in the NQ collector's row format: size in 0.001 lots, side `B`/`A`,
`seq` = aggregate trade id % 2e9 so trades in one millisecond keep exchange order. The parent backfill
(`binance_hist`) uses the same rules. Book quotes are thinned to the last one per symbol per 100 ms.
Binance has split its websocket routes: `aggTrade` is only served on `/market`, `bookTicker` on `/public`.
A single unrouted `/stream` URL gets quotes and no trades, so the collector opens two sockets.

It also records Bitstamp's BTC/ETH market-by-order stream (`live_orders_*`). Every message, and every REST
snapshot, is kept forever in `data/mbo/bitstamp/<pair>/<UTC date>.jsonl.zst`. An in-memory order book per
pair writes one `mbo_1m` row a minute (`migrations/20261010_mbo_1m.sql` documents each column). Bitstamp has
no sequence numbers, so resync is by time: after a REST snapshot, events at or before its microtimestamp are
skipped, and the book is re-snapshotted every 30 minutes.

The collector uses one Postgres connection with asynchronous commit and batched inserts. Its queue is bounded
and drops its own rows, so it cannot hold up the NQ collector's writer. It reconnects to Postgres and to each
websocket by itself. Unit file `deploy/crypto-collector-local.service` (not installed yet). Tests:
`test_crypto_collector` (11 cases, in `make hermes`).

## 2026-10-10 — paper ORB: a refused entry no longer locks the variant out for the day

The 10-09 entry below said the paper engine's release "behaves the same way". That was true for trend runners
only. When the paper broker refused an ORB signal (book / liquidity / regime gate, cooldown, cap), the ORB
strategy stayed "in position" with nothing filled. Nothing released it, so that variant sat out the rest of
the session. The release is now one helper, `paper::release_refused_entry()` (`paper_broker.hpp`), used for
both engines. ORB gets its trade count re-seeded one lower, as the executor's `settle_strategy()` does live;
trend gives the trade back itself. Test: `test_paper_broker` `orb_refused_entry_released_and_not_counted`
(fails without the re-seed).

`make hermes` never rebuilt `paper_engine`, so the 10-09 golden was frozen with the pre-fix binary. The golden
replay was testing a stale engine. `scripts/hermes.sh` now builds `paper_engine` too. Re-frozen golden:
4182 → 4555 trades, 0 removed: 132 ORB variants (today's fix) + 78 trend variants (the 10-09 trend fix,
captured for the first time). `make hermes` 18/18.

## 2026-10-09 — a refused entry no longer uses up max_daily_trades

Every engine counted a trade when it emitted the signal, and nothing gave the trade back when the
executor refused the entry before any order went out (book/liquidity/regime gate, chase, feed lag, risk).
So London early's 02:24 ET gate-blocked SHORT used 1 of its 2 trades, and the +16.5-pt 02:36 ET long was
refused at "2/2". NY RSI-2 at 11:45 and 12:00 ET (both `regime_eff_low`) was counted the same way.
Now a `not_executed:` release gives the trade back:
- `TrendStrategy::notify_trade_filled` and `MtfScalperStrategy::notify_trade_filled` do it themselves, only
  while the engine still holds the leg, so a second release cannot undercount.
- ORB (`orb_strategy.hpp`, no-touch) has its count re-seeded by the executor's `settle_strategy()`.

A release that follows an EXIT signal while the order manager is flat is now tagged `released:exit:…`, not
`not_executed:`. That keeps a trade that really happened from being given back. The paper engine's
`not_executed:entry_refused` release now behaves the same way for trend runners (paper ORB: fixed 2026-10-10, above). That added one trade to the golden day
(`sig_orb_5_15_10` LONG 09:35, +32.25 pts), which is re-frozen. Tests: `test_trend_strategy` "cap:" and
"release:" blocks, and `test_mtf_scalper` `not_executed_release_gives_trade_back`. `make hermes` 18/18.
The live unit was restarted flat at 16:28 ET.

## 2026-10-09 — live ORB on 2 MNQ

Founder: "2 MNQ for orb". `config/tradeify_config.json` qty 1 → 2 (one 15-pt stop = $63.64 against the
$187.18 trailing-drawdown room); validated with `--check-config`, the ORB unit restarted flat at 06:06 ET
(`[BROKER] exchange_net=0`, room 187.18, no `[BROKER-ROOM]` refusal). 1-MNQ copy in
`config/archived/live_history/tradeify_config.pre-mnq2-20261009-0606.json`. ORB is a fixed size; only the rota
windows size themselves from the room.

## 2026-10-09 — live rota: size each window from the drawdown room (1 NQ when it fits, else MNQ)

Founder: "check if we can do 1 NQ contract, if not do as many MNQ", with risk per trade = half the room.
`scripts/live_rota.sh` reads the newest `[BROKER-HWM]` room from either live instance's log and sizes the
window's strategy so one full stop (`sl_points` × $/pt × qty + `commission_rt`) fits in 0.5 × room: 1 NQ when it
does, otherwise the most MNQ that fit (max 10, min 1); no room reading → 1 MNQ. The generated config gets
`symbol`/`trade_contract`/`point_value`/`qty` accordingly and the Telegram hand-off line names the size. At the
2026-10-09 room ($187.18) no NQ stop fits (London early 10 pts = $201.82); sizes: London early 4 MNQ, band fade
3, VWAP / RSI-2 / breakout 2. The executor's own guard still refuses an entry whose stop exceeds the whole room.
The ORB instance stays 1 MNQ (`config/tradeify_config.json`).

## 2026-10-09 — live rota: 3 trades a day, matching the paper twins

`scripts/live_rota.sh` fell back to `max_daily_trades` 2 when a `sig_*` strategy sets none, while the paper
engine's default is 3, so the replays that picked each strategy assumed more trades than live allowed. The
fallback is now 3 (still capped at 3); strategies that set their own value keep it (`sig_ny_keltner_btp` and
`sig_ny_rsi2_btp` stay at 2). Found on 2026-10-09: London early missed a +16.5-pt paper long at 02:36 ET
because a SHORT the liquidity gate blocked at 02:24 had used up one of its 2 trades. That counting bug (a
refused signal still counts as a trade) was fixed separately the same evening, in the next entry up.

## 2026-10-09 — live rota: London windows handed back early (octal times)

`scripts/live_rota.sh` passes zero-padded HHMM times to `strategy_handoff.sh`, and bash `(( ))` read them
as octal. London early's hand-back time `0503` became 323, so the account went back to ORB at 03:23 ET
instead of 05:03. London late's `0918` is not valid octal, so that wait loop would error out and hand the
account straight back after the switch. `strategy_handoff.sh` now converts all three times with `10#`.
NY and Asia were unaffected: their times have no leading zero. After the fix the London early window was
re-armed at 03:47 ET (`systemctl --user start live-rota@ldn_e`).

## 2026-10-08 — manual signals on every session, with trail and exit alerts

Founder: "go on all sessions and wait for a signal for me to execute 1 contract … the most WR strategies
with the less drawdown … we will also trail and signal for me to exit."

- **Publisher generalised** (`src/paper/signal_publish.hpp`, `paper_main.cpp`): any trend-engine mode with
  `"publish_signals": true` publishes (was trend_confirm only; non-tcf states carry `"mode":"other"` and stage
  `watching`). The manual stop = the broker's working stop (initial, break-even, trail ratchet), tightened by
  trend_confirm's own trail. Telegram now sends, besides the entry (now "1 contract · 1R = $ NQ / $ MNQ"):
  `🔒 MOVE STOP` when the stop tightens by ≥ max(2 pts, ¼R) (and `live_signals.stop` follows it —
  `PaperDb::update_live_signal_stop`), `🟡 EXIT NOW — CLOSE` on a strategy / target / window exit, and
  `⚪ STOPPED OUT` on a stop, break-even or trail exit. Test: `signal_publisher_trail_and_exit_alerts`.
- **Signal set** (`config/paper_fleet.json`) — founder: "ORB, momentum / trend and a mean reverse strategy
  whenever the regime and conditions match", lowest drawdown "so we can pass the accounts", tight trail.
  Published as `sig_*` copies (the forward-test originals keep their settings and history):
  `sig_orb_5_15_10` (copy of the live ORB: 5-min range, 15-pt stop, 10-pt trail, 3 trades, entries to 10:30 ET),
  `sig_ny_trend_vwap_rgx` (`trend_vwap_5m__rg_expand` — only on expanding days: 23 trades / 8 sessions, 4 % losers,
  +$1,328, worst day +$5), `sig_ny_rsi2_btp` (`mr_rsi2_15m__btp`: 21 / 11, 33 % won ≥2 pts, 0 losers) and
  `sig_ny_keltner_btp` (`keltner_ride_20_15m__btp`: 17 / 10, 53 % won, 0 losers) — 09-21..10-07, backfill
  replay before 09-29, forward after — plus `sig_tky_band_fade_5m` (Asia 20:00–22:00) and
  `sig_ldn_donchian_55_5m` (London 07:00–09:15, `nt_ldl_trend_donchian_55_5m_s_g0`: 22 / 8, 0 losers). Regime
  gating did NOT help mean reversion (range/fade-gated copies: 2–10 trades, one losing) and regime-gated ORB
  copies only scratched, so neither is used. The tcf5_* set stopped publishing (max DD $2.6–7.4k at 1 NQ).
  **The low-loss records are mostly break-even scratches (median trade ~36 s)** — the stop moves to entry +1
  at +3 pts automatically, so the entry alert carries the BRACKET to set on the platform.
- **ORB engine publishes** (`SignalPublisher::on_tick_orb`, test `signal_publisher_orb_engine`).
- **Paper entry cutoff is minute-granular** (`last_entry_min` parsed by the fleet config; the paper broker's
  window used the hour only and would have stopped the ORB copy at 10:00). No existing strategy set the key.
  Test `entry_window_minute_cutoff`; parity test still passes.
- **Tight trail, decided by replay** (`config/ttrail_fleet.json`, 66 variants, label `ttrail`): every sig_*
  strategy × trail_step {2,3,4,6, current} × trail_delay_secs {0,300} + exact control. The evening deploy
  applies per strategy the variant with the highest share of trades won by ≥2 pts among those with net > 0,
  max drawdown ≤ the control's and ≥80 % of its trades (else keeps the current trail) and Telegrams the result.
- **Signals are for 1 MNQ** (founder, 10:10 ET: "switch the signals to MNQ" — the account has ~$300 of drawdown
  room, below one 18–20-pt NQ stop). Telegram entry / move-stop / exit texts take the instrument and $/pt from the
  fleet config (`OrbConfig::symbol` / `point_value`, MNQ / $2): "🟢 LONG / BUY MNQ … 1R 18.00 pts = $36 on 1 MNQ".
  Levels still come from the NQ feed (same index price). Tests assert the MNQ text.
- **Running today** (founder: "run these live"): the six sig_* strategies publish from a separate paper engine
  (`config/sig_live_fleet.json`, label `sig_live`, transient unit `paper-signals-live`, build-exp binary) — the
  main fleet was not restarted mid-session. The evening deploy stops it right before the main engine restarts.
- **Set revised 10:20 ET** (founder: "highest winning strategy with less drawdown … all sessions every day"):
  the paper ORB copy stopped publishing — on 09-21..10-07 it made PF 0.81 / −$23 on MNQ while the LIVE ORB made
  +173.9 pts with a worst trade of −2.25 (paper ORB does not reproduce live; investigate after the close).
  Added `sig_ldn_early_ema_pb_1m` (London 02:00–05:00, copy of `nt_ldn_trend_ema_pb_9_21_1m_s_g1`: 13 trades /
  5 sessions, PF 2.5, DD $25 on MNQ — thin). Coverage: Asia 20–22, London 02–05 and 07–09:15, NY RTH; no
  research yet for 18–20, 22–02 and 05–07 ET.
- **Live executor trade alerts** (`scripts/live_trade_alerts.sh`, user unit `live-trade-alerts`, enabled): the
  executor sends only incident alerts, so a read-only watcher turns its order stream into the same Telegram
  format — entry (fill on the `entry` order) with the account's BRACKET, each material `stop_loss` re-placement
  (break-even, trail), and the exit from the completed `live_trades` row (the executor writes it only at the
  close). Replayed today's three ORB trades in `--dry` mode before enabling. Starts at the newest rows.
- **Missing-window research** (founder: "add research for the missing windows tonight"): `config/gap_fleet.json`
  (1,700 `gw_*` variants, label `gap`) = the night grid's Tokyo template (224 trend/momentum/bounce bases, 44 mtf
  scalpers, 72 session ORBs, × stop sets × liquidity gate) moved to ue 16:00–16:55, ae 18:00–20:00, al1
  22:00–23:59, al2 00:00–02:00 and lm 05:00–07:00 ET (al split at midnight so no window wraps). `scripts/gap_eval.sh`
  runs `scripts/night_eval.sh` with NIGHT_CFG/NIGHT_LABEL/NIGHT_OUT (new env overrides, defaults unchanged) and
  Telegrams the lowest-drawdown strategy per window that clears ≥10 trades / ≥5 sessions / PF ≥1.5 / net >0 on
  1 MNQ. Nothing is added to the signals automatically.
- **Trade timeline** — founder: "fully show the entry, exit, full status of the trade alongside signals and
  exit". `live_signals.events` (JSONB, added by the schema list): `entry`, `breakeven`, `trail` / `stop`
  moves and `exit` (price, stop after the event, points, reason), appended by the publisher at the same
  moments as the Telegram alerts (`PaperDb::append_live_signal_event`). The dashboard shows it as a
  status strip (phase initial stop / break-even / trailing, now, open pts and $ NQ/MNQ, R, locked pts,
  best) on the Signals banner, the strategy card and under the Chart badge, plus BE / trail markers on the
  chart and a timeline column in the Signals history.
- Takes effect at the 16:08 ET paper-engine restart (deploy also builds and runs `test_paper_broker`).
  **Re-freeze golden** after it (`make golden` then `make golden-freeze`): two new ids add trades.

## 2026-10-07 — trend_confirm research grid + generic research evaluator

Founder: a dedicated up/down-trend momentum strategy with regime, bid/ask, liquidity and trend
confirmations (mode `trend_confirm`, built separately in the trend engine). Research tooling:
- `config/tcf_fleet.json`: 2,432 variants (label `tcf`). RTH 09:45–15:30 and post-ORB 10:30–15:30
  (1,024 each): tf {3,5} × entry {pullback, breakout} × ER min {0.30, 0.45} × VWAP slope {0, 0.2} ×
  delta flow {0, 0.55} × book imbalance_min {0, 0.6} × liquidity gate {off, ≥400/min & ≤25 pts/1k} ×
  trail {2.0, 3.0 ATR} × EMA exit {on, off} × min ATR {0, 4 (tf3) / 6 (tf5)}; overnight (globex anchor,
  128 each): London 02:00–09:15 gated (≥130/min & ≤70 pts/1k), the same London without the gate (to
  measure it), Asia 20:00–02:00 gated. Founder's reference move: 2026-10-07 04:30–07:30 ET, NQ
  31426 → 31249 — the report lists every trade any variant took in it.
- `scripts/research_eval.sh <config> <label> [report] [--force]`: generalises night_eval.sh — registers
  ids, replays each unreplayed session from the previous weekday 09:00 to d 16:05 under `<label>_tmp`
  (indicator warm-up) and keeps only session d's entries, [prev weekday 18:00, d 16:05) — Asia evening
  included, no duplicate warm-up rows, refuses in RTH / the Asia session,
  holds a per-label lock, refuses if `build/paper_engine` lacks a mode the grid uses, logs collector
  drops; report by every params key that varies, plus the top 25 consistent variants
  (`data/research/<label>/report.md|json`).
- `deploy/tcf-eval.{service,timer}` weekdays 16:45 ET (installed, enabled).

---

## 2026-10-07 — trend_confirm: a dedicated up/down-trend momentum mode with stacked confirmations

Founder: "a good uptrend / downtrend dedicated strategy, trend / momentum, with confirmations of the
regime, bid/ask line up, enough liquidity/volume and it's trending". New trend-engine mode
`trend_confirm` (`trend_strategy.hpp`, keys `tcf_*`); paper research only, no live config touched.
- **Trending:** own EMA stack fast > mid > slow (9/21/50) with mid rising vs 3 bars ago (mirror for
  downtrends); Kaufman efficiency ratio ≥ `tcf_er_min` over `tcf_er_bars`; session-VWAP slope over 6
  bars ≥ `tcf_slope_min` × ATR and the close on the trend side of VWAP.
- **Order flow / volume:** rolling aggressor buy share ≥ `tcf_flow_min` over `tcf_flow_bars` (≤ 1 − x
  for shorts), trigger-bar delta with the trade, trigger volume ≥ `tcf_vol_mult` × the 20-bar average.
- **Volatility:** tf ATR in points within [`tcf_min_atr`, `tcf_max_atr`] (0 = off).
- **Any session:** the VWAP anchor follows `session` (rth / globex / window) and entries follow
  `win_start`..`win_end` — London / Asia windows work exactly as NY does.
- **Entries:** `pullback` (a recent bar touched EMA-fast while the trend held, then a close beyond the
  prior bar's extreme) or `breakout` (a close beyond the prior `tcf_bo_bars` extreme).
- **Exits (strategy-managed, every tick):** stop = pullback structure ∓ 0.25 ATR capped at
  `tcf_stop_atr` × ATR; after +1R a `tcf_trail_atr` × ATR trail; optional close through EMA-mid
  (`tcf_exit_ema`) and fixed `tcf_target_r`. Reasons tcf_stop / tcf_trail / tcf_ema_exit / tcf_target.
- **Book alignment and liquidity** are not duplicated in the mode: variants combine it with the
  existing host gates (`imbalance_min`, `liq_vol_min` / `liq_impact_max`, `regime_min_eff`,
  `regime_with_move`, `rvol_min`), which run identically on paper and live.
- Every entry logs its confirmation values (ER, slope, flow, volume ratio, EMA stack, VWAP); a
  pattern bar that fails a confirmation logs which ones.
- Tests (`test_trend_strategy`, 8 new, suite 125 ok): uptrend pullback LONG at the trigger close;
  chop does not enter; selling flow blocks the long; breakout LONG; structural stop; trail after
  +1R; ATR outside [min, max] blocks; mirrored downtrend SHORT.
- **Reference move check (founder):** today's London downtrend, NQ 31426 → 31249 between 04:30 and
  07:30 ET. Scratch harness fed from `bars_1m` (1m bars with their aggressor-buy volume, no replay),
  defaults with tf 5 / session globex / window 02:00–09:15: 03:55 ET SHORT 31406.00 stopped 03:59 at
  31430.75 (−1R, −24.75 pts, a probe before the top); **05:10 ET SHORT 31386.25 held through the
  05:45–06:15 / 06:45 / 07:15 pullbacks and trailed out 07:59 at 31282.25 (+104 pts, +4.2R)**. Net +79
  pts. The pullback-short pattern itself fired on several earlier bars but was held back by ER / VWAP
  slope until the trend was established.

---

## 2026-10-07 — trend_confirm live state + manual-trading signals (dashboard feed)

Founder: "a dedicated live strategy page that handles this one and shows me the progress of a
pullback and a signal for me to take; if the strategy doesn't fully complete, don't create a signal".
Paper engine side (the page is in ~/Desktop/bot):
- `TrendStrategy::tcf_status(now_us)` — public live status of a trend_confirm strategy: stage
  (outside_window / no_trend / trend / pullback / trigger_ready / in_trade, plus `stage_flat` = the
  stage ignoring the strategy's own position flag), bias, every confirmation value + ok flag, the
  pullback (touched, bars since, touch price, trigger level/distance), EMAs/VWAP. The confirmation
  maths moved into one helper `tcf_confirm()` used by BOTH the entry decision and the status, so the
  page cannot show a check the strategy would fail (behaviour unchanged: 125 trend checks still pass).
- `src/paper/signal_publish.hpp` (new): for fleet strategies with `"publish_signals": true`,
  `live_signal_state` (one row per strategy, JSONB) is upserted at most every 5 s and at once on a
  tf-bar close / entry / exit; host gates the strategy cannot see are added (liquidity via
  RegimeState::liquidity, book imbalance via QuoteState) and a would-be trigger_ready that a host gate
  would block is shown one step back. `live_signals` gets ONE row only when the paper broker ACTUALLY
  OPENS the position (all confirmations + gates passed) — entry = broker fill, stop = strategy stop,
  1R/2R targets — closed with the broker's exit price/reason; each new row sends one grid-notify line.
  Replay/audit runs and warm-up publish nothing. PaperBroker gained last-exit accessors; PaperDb gained
  `upsert_signal_state` / `insert_live_signal` / `close_live_signal` and the two tables in its schema.
- Tests: `tcf_status` stage progression no_trend → trend → pullback → trigger_ready with no signal
  (test_trend_strategy); `signal_publisher_only_on_actual_entry` (test_paper_broker): a book-gate block
  writes state but NO signal row; a filled entry writes exactly one, closed on flatten.
- Open (not changed): in the paper engine a trend signal BLOCKED by a host gate is not released back to
  the strategy (the executor releases it since 2026-10-05) — the strategy may sit "in position" with
  nothing filled until its own exit logic or the session reset. The publisher shows the broker's truth.

---

## 2026-10-08 — signals say LONG / SHORT first

Founder: "need to know if it's long or short". Telegram now starts with "🟢 LONG / BUY NQ" or
"🔴 SHORT / SELL NQ" (signal_publish.hpp; deployed to the paper fleet after the 16:05 close — a
mid-London restart would flag every fleet session). Signals page: the banner leads with a large
coloured "▲ LONG · BUY" / "▼ SHORT · SELL", the browser notification title is "🟢 LONG — BUY NQ" /
"🔴 SHORT — SELL NQ"; chart markers read "LONG · ldn" / "SHORT · ldn" (dashboard, live now).
First London signal (02:00 ET): tcf5_ldn / _ng / _bo SHORT 31324 → trail exit 31257.50 at 03:22,
+66.5 pts (+2.24R); tcf5_ldn_book refused by its book gate (imbalance).

---

## 2026-10-08 — signal rows record the entry decision's own confirmations

First live trend_confirm signal (tcf5_asia, 00:00 ET, SHORT 31350.25 → stop 31379, −1.03R) was
recorded with `vol_ok: false, vol_ratio 0.00` although the engine's ENTRY line shows vol 1.44x: the
publisher read `tcf_status()` at the FILL, one tick into a fresh, empty forming bar. Now mode_tcf
saves the decision's values at entry (`TrendStrategy::tcf_entry_snapshot()`, stage "signal") and the
publisher writes those to `live_signals.checks`. Test `entry snapshot keeps the trigger bar's
confirmations`. Row 1 corrected from the ENTRY log (`checks.corrected` notes it). Paper fleet
rebuilt + restarted 00:42 ET (before London, before 03:00 so no restart-session flag), 0 collector
drops. Record-only change: no entry/exit decision differs.

---

## 2026-10-07 — trend_confirm 5m in the paper fleet + live Signals page; refused trend signals released

- **Paper fleet +9 `tcf5_*`** (founder: "add it to the paper fleet on 5m"): trend_confirm, 5m, pullback
  defaults — London 02:00–09:15 (liquidity gate / no gate / breakout / book-gate twins), Asia
  20:00–02:00 (liquidity gate), NY 09:45–15:30 (plain / liquidity / breakout), post-ORB 10:30–15:30.
  `publish_signals: true` → the paper engine writes `live_signal_state` (≤ every 5 s + bar close /
  entry / exit) and a `live_signals` row ONLY when the broker actually opens the trade (every gate
  passed), with a fail-open grid-notify line. Fleet 4,829 → 4,838; restarted 16:35 ET, 0 collector drops.
- **Bug fixed — a refused trend signal froze the strategy for the session** (paper engine; the live
  executor got the same fix 10-05): the trend engine marks itself in position after its signal
  callback, so a gate/cooldown refusal left it "in position" with nothing filled. paper_main now
  releases it when the broker has no position and no pending entry. Golden 2026-09-22: +258 trades
  from 170 gated trend variants (now taking their next setup), 0 removed, deterministic — re-frozen.
  ORB paper variants are not covered by this release (live ORB has no host gates); parity follow-up.
- **absorption_zone grid result (azq, 13 sessions): 0 trades.** Classifier fine (most days TREND);
  only 2 distinct absorption events in 13 sessions, 0 triggers — the spec's AND of narrow zone touch
  + volume + counter-flow + held close + 3-bar trigger is too strict for NQ 5m. Not loosened (founder's call).

---

## 2026-10-07 — absorption_zone: the founder's "Absorption Strategy Spec" as a trend-engine mode

Founder: "build all scenarios" of the spec (written for a Python backtester; built in the C++ trend
engine instead — same code on paper and live, replayable on recorded ticks, house no-Python rule).
New mode `absorption_zone` (`trend_strategy.hpp`, all keys `az_*`, 5m bars):
- **Day type** at +30 / +60 min: 5 votes — open vs prior value area, opening range (2 closes outside
  vs both sides traded), VWAP slope (|Δ6 bars|/ATR ≥0.3 / <0.15), cumulative delta (with price, ≤40 %
  retrace vs ≥2 sign flips), pullbacks (no VWAP touch since the OR break vs ≥2 VWAP crosses) → Trend
  (≥3, with the OR-break direction, not back inside the OR) / Balance (≥3) / Neutral. 60-min
  reclass: Trend back inside the OR → Balance; Balance breaking out with |cum delta| ≥ 3× avg bar
  volume → Trend; then frozen. `az_daytype=false` skips the classifier.
- **Setups:** Trend → absorption at the VWAP/EMA-20 zone (exists when |vwap−ema20| ≤
  `az_zone_width`×ATR, ±0.25 ATR) in the OR-break direction; Balance → absorption at VWAP ±2 SD
  (new volume-weighted SD bands), target VWAP. Absorption = zone touch + volume ≥ `az_vol_mult`×avg
  + delta against the trade ≥ `az_delta_thresh`×volume + close held in/beyond the zone; trigger =
  close beyond the zone with delta in favour within `az_trigger_window` bars. Optional divergence,
  EMA-200 bias, VWAP-slope filter, stacked-level count (prior POC/VAH/VAL/H/L, overnight H/L, VWAPs
  anchored at prior-day high/low and the week open), min R:R, one re-entry per zone if the new
  absorption has more volume, zone dead after 2 stops, −3R daily stop.
- **Exits** checked every tick by the strategy (stop = absorption extreme ∓ 2 ticks; targets per the
  spec). One contract cannot scale out, so `az_exit` = t1 (all out at target 1) / t2 (stop to entry
  at t1, out at t2) / trail (stop to entry at t1, out on a 5m close through EMA 20). The broker's
  sl_points is a catastrophe stop only.
- Tests (`test_trend_strategy`): balance fade fires and exits at VWAP; volume near-miss and a close
  below the zone do not fire; structural stop; t2 break-even; Trend-up and Balance day-type fixtures.
- **Grid** `config/azq_fleet.json`: 2,304 variants (setups × classifier on/off × exit × volume ×
  delta × divergence × EMA-200 × slope × zone width × entry window), replay label `azq`;
  `azq_bo_dty_trail_v12_d15_divn_ey_sy_z5_w1100` = the spec defaults. Paper research only.
- Not built: news calendar buffer and NYSE TICK internals (no feeds — the spec's stubs pass), the
  Excel journal column names (journal not supplied), true ⅓-scaling (needs ≥3 contracts).

---

## 2026-10-07 — live ORB switched to 1 MNQ (founder)

10-06: the ORB signalled 3 times (09:36 SHORT first); each was refused by the broker-room guard
(room $90.38 < one 1-NQ stop $301.82) and — the 10-05 fix working — released, so it could signal
again. With $90 of room no 1-NQ trade can ever pass, so founder chose "Switch to 1 MNQ":
`config/tradeify_config.json` symbol MNQ / trade_contract MNQZ6 / point_value 2 (qty 1, commission
1.82 = the measured MNQ fee, entries ≤10:30, 3/day unchanged; signals still from NQ ticks). Backup
`config/archived/live_history/tradeify_config.pre-mnq1-*.json`. `--check-config` OK; executor
restarted 00:4x ET (flat): `Trading contract: MNQZ6`, `room=90.38`, no BROKER-ROOM refusal (one MNQ
stop ≈ $31.82). pre_rth_check / execution_audit already handle MNQ (pv 2, contract prefix, room vs
stop). Note: ~2 straight MNQ stop-outs would use the room up again.

**Order test — PASS (09:01 ET, founder: "test that we can place a trade").** New
`nq_executor --drill probe` + `scripts/order_probe.sh [account] [--force]`: stops the live unit
(NO_DEPLOY held), confirms the exchange FLAT, sends 1 BUY LIMIT 100 pts below the last price,
waits for the exchange to accept it (`open`), cancels, waits for `complete` with nothing filled,
then always restarts the live unit. Refuses inside 09:25–10:35 ET and when not flat. Live result:
MNQZ6 LIMIT 31156.00 (last 31256.00) on RTG25785042011 via `simulator` — working at the exchange
195 ms after sending, cancelled, filled 0, balance unchanged $24,280.24. First attempt failed
safely (drill mode halts the strategy, which then ignores ticks → no price): the probe now prices
off the executor's own last tick.

---

## 2026-10-05 — no live trade today: trailing-drawdown room; guard now matches Tradeify; ORB release fixed

Founder: "why did we not take any trades today" → "please fix anything that made it to not trigger"
→ "add logs and debugging to check the flow is correct and won't break anything".

- **Why no trade (correct behaviour, unchanged):** the ORB signalled LONG at 09:35:04 ET and risk
  refused it — broker room $137 < one 1-NQ stop $301.82. Tradeify's own numbers (Growth 25k,
  balance $24,280.24, trailing max drawdown **$24,189.86**) put the real room at **$90.38**: a
  1-NQ trade that stops out fails the evaluation. Only a smaller contract (1 MNQ ≈ $32 per stop)
  fits — founder's decision, live config untouched.
- **Guard was LOOSER than Tradeify** (our mark 25,143.12 vs theirs 25,189.86, −$46.74): the
  high-water mark only saw sampled tid=451 balances and missed the intraday peak of 10-02's trade 2.
  New `notif::open_equity_peak()`: while a position is open, every tick raises the mark to
  last FLAT balance + (best open move + `hwm_margin_pts`, default 2.0) × point value × qty (Tradeify's
  mark implied ~1.3 pt beyond our highest recorded print; the collector had 30-s feed gaps then).
  Persisted once flat. On 10-02 it gives a floor of 24,206 — stricter than Tradeify's, never looser.
  Stored mark corrected to 25,189.86; startup now logs `halt when balance <= 24189.86`, `room=90.38`.
  Test `open_equity_peak_is_at_least_as_strict_as_the_broker`.
- **Bug fixed — a refused ORB signal blocked the rest of the day.** `OrbStrategy::check_breakout`
  sets `in_position` AFTER the signal callback, so every "release" the executor did INSIDE the
  callback (stale feed, book/regime gate, chase limit) was overwritten, and a risk rejection had no
  release at all. All engines now queue the release and `settle_strategy()` applies it after the
  callback (the trend engine already worked this way). Strategy logic untouched. Test
  `release_inside_callback_is_overwritten_release_after_works`.
- **Logs:** every entry signal leaves an `[ENTRY-FLOW]` trail (received → skipped/refused or order
  placed → released); `[BROKER-HWM] open-position peak …` and `persisted open-position peak …`.
- **Safety:** `nq_executor` used to START A LIVE EXECUTOR on `config/orb_config.json` for any
  unknown argument (it happened once today with `--help`: an old Legends config, after the close,
  killed within seconds — no orders, no rows, no session disturbed). Now `--help` prints usage, and
  an unknown argument or a missing `--config` exits 2. Every launcher already passes `--config`.
- Restarted after the close: `nq-executor-local@tradeify`, `@tradeify_mtfasia`. hermes-fast 16/16.

---

## 2026-10-04 — liquidity gate; overnight trend variants could never enter (fixed); Asia/London grid

Founder: trade momentum / trend on Asia and London when the book is not thin; "create as many
combinations as possible to check the stats".

- **Liquidity gate** (`paper_quote.hpp` RegimeState, live + paper): `liq_vol_min` (mean contracts
  per minute over the last `liq_bars` closed minutes, default 15) and `liq_impact_max` (points of
  1-min range per 1,000 contracts — a thin book moves far on little volume). Fail-closed while
  warming up. Off by default; no live config sets it. Test `liquidity_gate_reads_volume_and_impact`.
- **Bug fixed — overnight trend variants never entered.** The paper broker's entry window comes
  from `session_open_*` / `eod_flatten_*` / `last_entry_hour` (default 09:30–15:55); a trend
  variant's own `win_start`/`win_end` never reached it, so the 300 fleet trend variants with
  windows outside RTH took **0 overnight trades in 11 sessions**. `orb_config_for` now maps an
  out-of-RTH trend window onto the broker window (explicit keys still win; RTH windows untouched,
  golden unchanged). Test `trend_window_sets_broker_window_overnight_only`. Paper only — a live
  overnight trend config must still set the session keys itself.
- **Overnight grid** `config/night_fleet.json` (948 `nt_*` variants: 28 trend-engine bases, 11 mtf
  scalpers, session ORBs × Tokyo 20:00–22:00 / London 02:00–05:00 / London-late 07:00–09:15 ×
  stops × gate off/100-80/130-70/160-65). Replay-only research under label `night` via
  `scripts/night_eval.sh` + `deploy/night-eval.{service,timer}` (weekdays 16:30 ET);
  report `data/night/report.md`. First 9 sessions: the gate cuts losses in every window/family
  (e.g. London momentum median −$603 → −$173..−$231) but no group is clearly positive; 1 of 948
  combinations passes the consistency bar (`nt_ldl_mtf_htf30_v5_g0`, 39 trades, +$1,954, PF 1.55)
  — at 948 tries that is a lead for the forward test, not a result. Bounce loses everywhere.
- Hourly NQ liquidity (8 sessions): overnight 64–83 pts per 1k contracts vs 15–20 in US hours;
  best overnight windows 20:00 Tokyo, 02:00–04:00 London open, 07:00–08:30. The BBO feed is
  throttled (~3–4 quotes/s, size = order count) — spread/top size are relative only.

---

## 2026-10-04 — second-strategy paper test for 10:30–12:00 ET (slot 2); 11 candidates in the fleet

Founder: keep the ORB as is; find the best bounce / trend / momentum strategy for the window
after it where volume and volatility still give consistent trades. Paper only — no live config
or strategy code changed.

- Why 10:30–12:00: 15-min NQ range averages 75 pts there (ORB hour 106, after 14:00 ~47) on
  ~60 % of the ORB hour's volume (bars_1m, 9 sessions 2026-09-22..10-02).
- `config/slot2_fleet.json`: 11 windowed copies (`s2_*`) of the best live-runnable variant per
  family on the forward test (trend: vwap_trend ×2, supertrend, fib_pullback; momentum:
  opening_drive ×2, mtf flags/loose; bounce: rsi2, vwap_fade, failed_breakout).
- `scripts/slot2_eval.sh` + `deploy/slot2-eval.{service,timer}` (weekdays 16:20 ET): replays
  each new session under label `slot2`, rewrites `data/slot2/report.md`. Candidates were picked
  on sessions ≤ 2026-10-02, so the verdict uses later sessions only. Pass criteria fixed up front.
- Same 11 ids added to `config/paper_fleet.json` (4,818 → 4,829) for the live forward test.
- Golden re-frozen: +14 trades on 2026-09-22, all from `s2_*` ids; 0 existing trades changed,
  determinism identical.

---

## 2026-09-30 — execution validation every day; feed drops fixed; ORB live on 1 NQ, 3 trades, first hour

Founder: "every day from now on we must validate and audit the execution process to be 100%
everything works" (after a dry-run day — `dry_run: true` from the evening before, per "test it for
now" — produced five simulated NQ winners that never reached Rithmic), then "clear todays trades and
lets execute. also lets flip back to 3 trades instead of 5. and we only trade within the first 1 hour".

**Live config** (`config/tradeify_config.json`): `dry_run false`, label `tradeify`, NQ/NQZ6, qty 1,
`max_daily_trades 3`, new `last_entry_min` → entries until **10:30 ET** (OrbConfig gained
`last_entry_min`, default 0 = the old whole-hour rule; `orb_strategy.hpp` compares hour*60+min —
`tests/execution/test_orb_strategy.cpp::minute_cutoff_blocks_at_1030_allows_1029`). The day's
`tradeify_dry` rows were deleted (founder's ask). Executor restarted live at 10:47 ET: ORDER_PLANT +
PNL_PLANT connected, exchange flat, [BROKER-HWM] room $321.76.

**Daily execution validation** (three layers, all Telegram via grid-notify):
- `scripts/pre_rth_check.sh` (09:00 ET) now also asserts: dry_run false, label not `_dry`,
  point_value matches the symbol, contract matches the symbol and is neither expired nor past its
  roll date (`contracts`), qty ≥ 1, commission_rt > 0, ORDER_PLANT login since the last start,
  drawdown room ≥ one stop; the green line prints the config summary.
- `scripts/execution_watch.sh` + `execution-watch.timer` (every 2 min, acts 09:25–16:05 ET
  weekdays): unit active, not dry, broker sessions since the last start, executor log alive, no
  standing halt, exchange stop tracking the internal stop, feed fresh. Alerts once per transition.
- `scripts/execution_audit.sh` + `execution-audit.timer` (16:10 ET): the session's log block
  (live segments only) → mode, broker sessions, route, contract, every entry signal ended in a fill
  or a logged timeout, no rejects, live_trades rows = exit fills, qty = config, broker day_pnl vs
  recorded net within $1/contract + $2, exchange stop lag ≤ 15 s, BE moves replaced, no CRITICAL,
  max_daily_trades and the entry cutoff respected, halts, feed gaps > 30 s in RTH. JSON report in
  `data/execution_audit/<date>.json`, exit 1 on any FAIL. Run on 2026-09-29 it reproduces the
  audit's findings (fee gap $73.90, two "Exchange stop rejected — software SL fallback" CRITICALs
  that nobody had noticed, 137 s feed gap).

**The feed was dropping ticks — a pre-existing defect, found by the new audit.** The collector's
writer queue (`WRITER_QUEUE_MAX` 512) overflowed and DROPPED tick batches: 2,144 on 09-29, 888 on
09-30 (mostly 15:00–18:00 EEST), producing the 30–137 s RTH feed gaps, the executor's
`pg_feed_stale` halts and, on 09-30 17:49 EEST, an 85 s lag of the newest tick behind the clock.
Cause: the paper fleet upserted all 2,271 `paper_positions` rows every 2 s, each an autocommit
transaction with `synchronous_commit=on` (~300 fsync commits/s; 76.9 M lifetime updates on a
2,271-row table) — the collector's tick INSERTs waited on WAL/IO behind it. Fixes: paper flush is
change-only (per-row signature; a resting flat row is never rewritten), batched in ONE transaction,
every 5 s, on a session with `synchronous_commit = off` (paper data only — collector and executor
sessions untouched); `WRITER_QUEUE_MAX` 512 → 8192 so a burst buffers instead of dropping. Both
paper units and the collector restarted on the new binaries (the executor was flat and past its
entry cutoff). Verification: see the execution-audit report for 2026-10-01.

## 2026-09-30 — paper fleet: start-up warm-up, honest costs, gap-through stop fills, leaderboard validity columns

Audit follow-up (paper results + leaderboard). **Warm-up on start** (`paper_main.cpp`, fleet config
`warmup_bars` = 1600): a live start replays the last 1600 completed 1m bars of recorded ticks (start
resolved against `bars_1m`, so weekends and halts cost no bars) into every strategy with the brokers in
warm-up mode — entry signals are counted and dropped, resumed positions are still managed, no
paper_signals row, no position/account flush — then re-seeds today's counters, releases any strategy
that believes it holds a suppressed entry (the executor's "release whatever you think you hold") and
re-applies start-up halts. Until today a restart re-read 5 minutes: the momentum scalper needs 1500
completed bars (`warmup_bars`) and traded nothing for a day after every restart, and 3 of the first 6
forward sessions were restart days — the whole `qualifies` set on 2026-09-29 rested on them. Every
engine start is now a row in **`paper_engine_runs`** (migrations/014, also in `ensure_schema`; history
seeded from the engine logs' "Fleet ready" lines, 52 starts). **ORB gap re-feed**: after a feed gap on
the same trading day the flat ORB strategies get the session's recorded ticks re-fed (entries
suppressed) so they regain their opening range; trend/mtf keep bar history across the reset and are
deliberately not re-fed (double-counted EMA/ATR bars). NOTE: the Sunday/holiday case remains — a
restart right after a weekend finds fewer bars than 1600 in the window and warms with what exists.
**Costs**: `commission_rt` 1.0 → **1.82** per contract round trip (Tradeify, measured on 09-28/29) and
`slippage_ticks` 1 → **3** (live ORB entries averaged 4.4 ticks on a 4-tick marketable limit) in both
fleet configs; **stop fills never better than the crossing print** (a gap through the stop fills where
it printed — 10.8 % of stop exits had filled better than the NQ print by more than a tick) in both
brokers, tests added. **Break-even stop mirrors live**: the live OrderManager now sends the BE move to
the exchange regardless of the trail_step storm filter (trade 66 on 09-29 lost −8.25 × 5 lots because
a 9-pt BE move under trail_step 15 was dropped); `PaperBroker::sync_placed_stop(force=true)` places it
the same way, the parity test covers it. **Leaderboard** (`strategy_leaderboard.sql`, appended columns
only): `restart_sessions` (sessions with an engine start 03:00 ET → slot end), `trade_set_id` /
`trade_set_size` (identical trade sets — 7 mtf `__btp` variants share one), `positive_halves`,
`scratch_rate` (49 % of exits are breakeven scratches and `wins` counts them); Sharpe/Sortino over the
period's calendar sessions with flat days as 0 and NULL under 10 sessions (a 3-day strategy read 17.0);
`qualifies` needs a real profit factor (no-loss rows have none) and `restart_sessions = 0` — on the
7-day slot ranking 8 → **0** qualify. **Data repairs**: the 26 forward rows the 2026-09-23 09:43 ET
restart produced in its 5-minute catch-up (entered 09:40:53, exited 09:40:53–09:41:03, written 09:43;
prints confirmed, but no engine was running when they happened) relabelled `invalid_resume_20260923`,
paper_daily rebuilt — 09-23 counters now equal the trades (1762 / −4001.33) where they disagreed by 8
trades before. ⚠️ **A defect in this change's first deployment wrote 1,421 phantom rows** (12:00–12:05
UTC 2026-09-30): the warm-up ended after one page because `poll_ticks` hands a LIMIT-cut same-
microsecond group to the next page and the "history exhausted" test read the short page as the end,
so the rest of 09-29 was processed live with entries enabled (583 duplicates of forward rows, 838
signals the forward run never took). Relabelled `invalid_warmup_20260930` (not deleted — same rule as
09-23), paper_daily 09-29/09-30 rebuilt (tradeify 3815 / −20731.50 and 289 / −1993.23, both equal to
the trades; es 74 / −409.00), the end test now reads the tape's time, and a restart on the fixed
binary wrote 0 such rows. A second defect on the next restart: five resumed overnight holds
(`beta_on*`, entered 09-29 15:55/18:00) were flattened at their own entry tick — the trend strategy,
seeded "in position", judged its window end against replayed ticks from BEFORE the entry. During
warm-up a runner holding a resumed position now skips every tick AND every minute-boundary window-end
check older than that entry (the replayed 09:30 ET boundary of the previous day had queued a FLATTEN_EOD
on them; the broker already ignored the ticks), and the wall-clock EOD check is off while warming up
(the strategies' state is in the replayed past). The rows joined the quarantine label (1,432 total), the
positions were restored in `paper_positions` (direction is the TEXT `LONG`/`SHORT` the loader reads)
from the rows + the engine's "Resumed" lines, and the engine resumed them. NOTE for the gate: the two
paper units share `build/paper_engine` — stop BOTH before replacing it, or `cp` fails with "Text file
busy" and the restart runs the old binary. Golden re-frozen: 2438 → 2448 trades (costs move every P&L; +10 trades from
the wider fills). Replay mode never warms up and its `trades_only` store already skips paper_daily /
positions / account (verified: every upsert guards on `trades_only_`).

## 2026-09-30 — live executor: audit fixes (BE stop, fees + VWAP, account-wide drawdown, dry-run label)

From the 2026-09-30 validity audit. All in `src/execution/`; tests in `tests/execution/`.

- **Break-even stop reaches the exchange.** `update_stop_order_locked` gained `force`; the BE move
  bypasses the trail_step storm filter (2026-09-29 trade 66: sl 8 / BE 1 / trail_step 15 — the 9-pt
  BE move was dropped, the exchange stop filled at the original level, −$82.50 instead of ≈ +$10).
  Test `be_move_smaller_than_trail_step_reaches_exchange`. ⚠️ `test_parity_paper_vs_live`
  `regime_exit_trend` now FAILS by 64 ticks: `paper_broker.hpp` still models the old suppression
  for the BE move (its `placed_stop` stays at the initial stop while live now sends BE). The paper
  side needs the same exemption — owned by the paper-fleet change in flight, not done here.
- **Fees and VWAP on live rows.** `config/tradeify_config.json` `commission_rt` 0 → 1.82 (the
  MEASURED Tradeify MNQ fee per contract round trip: 09-28 $3.64 / 2 ct-RT, 09-29 $81.90 / 45 ct-RT;
  `_commission_note` says the NQ fee is still unknown). Multi-lot entries/exits record the
  qty-weighted average of the partial fills instead of the completing partial's price (09-29 rows
  61/64/67/68/69 were off by $8). Test `multi_lot_partial_fills_record_vwap_entry_and_exit`.
- **Trailing drawdown is account-wide and broker-anchored.** `OrbDB::get_total_pnl` /
  `get_peak_equity` sum EVERY strategy and instrument on the live label (a `_dry` label seeds from
  the label it rehearses for) — the MNQ ORB instance had seeded peak 25,367 from its own gross wins
  while the account was at 24,321. New `live_account_hwm` table + `notif::broker_hwm /
  broker_drawdown_breached / broker_drawdown_room`: the tid=451 balance feeds a persisted
  high-water mark (max of starting balance, stored mark, every balance seen) and entries halt when
  balance ≤ HWM − trailing_drawdown_cap, beside the existing broker daily-loss halt. Startup logs
  `[RISK-SEED]` (synthetic, account-wide) and `[BROKER-HWM] seed=… halt when balance <= …`.
  Test `broker_trailing_drawdown_on_reported_balances`. Today: HWM 25,000 → halt at 24,000; the
  account is at 24,321.76 — $321.76 of room, one full NQ stop.
- **Dry-run rows never land in the live history.** `OrbConfig::apply_dry_run_label()`: with
  `dry_run: true` the label must end in `_dry` (appended with a WARN otherwise; `base_label()`
  strips it for the seeds). `strategy_leaderboard()` and the board already exclude `%_dry%`.
  Tests `config_dry_run_forces_dry_label`; `config_valid_file_loads` updated to the rule.
- **loss_limits drift**: `migrations/20260930_loss_limits_max_daily_trades.sql` sets the dashboard's
  `max_daily_trades` row to the config's 5 (the executor never reads that table; apply by hand).

Gate: `make hermes-fast` 15 PASS / 1 FAIL (the parity case above). Dry-run unit restarted on the new
binary: `Historical total_pnl=-537.50 (account-wide, label=tradeify)`, `peak_equity=25208.00`,
`[RISK-SEED] … room=254.50`, `[BROKER-HWM] seed=25000.00 … halt when balance <= 24000.00`.

## 2026-09-29 — live is ORB only, on 1 NQ

**Then, founder: "test it for now" — `dry_run: true` on the same config; the instance opens no Rithmic session and simulates fills on the NQ feed until the founder flips it back.**

Founder: "lets run NQ for 1 contract not MNQ and we only keep ORB". `config/tradeify_config.json`: symbol MNQ→NQ,
trade_contract MNQZ6→NQZ6, point_value 2→20, qty 5→1 (archived copy in `config/archived/live_history/`), validated
with `--check-config`, executor restarted flat after the close. `config/tradeify_handoff_config.json` archived and
`strategy-rotation.timer` disabled — nothing else can reach the account. Risk limits untouched (SL 15 pts is now
$300 a trade; the −$500 daily loss limit halts after two full stops).

## 2026-09-29 — hand-off engine removed from live

Founder: "remove FIB_PB_1M_DEEP from the live". `strategy-handoff.timer` disabled and stopped, the
`tradeify_handoff` instance confirmed stopped, `config/tradeify_handoff_config.json` set to `dry_run: true`
(live copy archived under `config/archived/handoff_history/`), validated with `--check-config`. ORB
(`tradeify`, 5 MNQ) keeps running alone; the Sunday rotation still rewrites the hand-off config but
nothing starts it. Restore path is in CLAUDE.md (Local mode).

## [Unreleased]

### Changed — live size 5 MNQ on both ORB and the hand-off (2026-09-29, founder)
- `config/tradeify_config.json` qty 2 → 5, `config/tradeify_handoff_config.json` qty 1 → 5
  (previous files in `config/archived/handoff_history/*pre-qty5-20260929*`). Both validate; the ORB
  executor was restarted pre-market (06:39 ET) and reconciled flat. Per-trade risk at the initial
  stop: ORB 15 pts × $2 × 5 = $150, hand-off 8 pts × $2 × 5 = $80; the −$500 daily loss limit
  now halts after about three ORB stops. The rotation carries qty across promotions.

### Added — volume and volatility as generic filters, two new modes, everything forward-tests (2026-09-29)
- Founder: "add these and also the beta we have. we want to have everything as filters to fully
  understand what works and what doesn't." Filters, on OrbConfig so any base takes them, live and
  paper alike: **relative-volume gate** `rvol_min` / `rvol_max` / `rvol_bars` — the last closed
  minute's tick volume against the mean of the previous N minutes, kept by `RegimeState` from the
  tick sizes (`rvol_low` / `rvol_high` / `rvol_warmup` in the signal log); **volatility-targeted
  size** `vt_risk_usd` / `vt_qty_max` — contracts = risk ÷ (prior-day ATR14 × point value),
  `RegimeState::qty_for`, used by the paper broker's entry and by `OrderManager` for the entry and
  stop orders (dry-run fill too). The session-shape and with/against-move gates from 09-28 are the
  filter form of the beta idea and already sit on 100+ bases.
- Trend modes: **atr_break** (close beyond session open or prior close ± `ab_k` × ATR, prior-day
  ATR14 from the host via `set_day_atr`, fail-closed without it) and **vprofile** (the prior RTH
  session's volume profile in 1-pt bins → POC and a `vp_va_frac` value area; `break` through
  VAH/VAL or `fade` a touch toward the POC with a strategy-owned `vp_target` flatten).
- Tests: rvol gate on a synthetic tape, sizing (off / clamp / no ATR), a sized paper entry,
  atr_break (one per side, fail-closed), vprofile (POC, value area, fade, target).
- Matrix (`config/paper_volgrid_research.json`, label `volgrid`, 16 bases × 5 filters + 6
  atr_break + 4 vprofile, six sessions): rvol ≥ 2.5 +$32.8/ct but 3 trades instead of 20;
  rvol ≥ 1.5 +$10.8/ct and −$23 net (8 better / 4 worse); quiet tape ≤ 0.8 −$10.3/ct (1/7);
  vol-targeting changes size only ($/ct identical, 2-lot version +$113 net); atr_break and
  vprofile negative or tiny. All 90 are in the fleet per the no-pruning decision.

### Fixed — the paper engine rolled the trading day AFTER the 18:00 feed-gap reset (2026-09-29)
- Found while adding `beta_on_1800` (founder: the Tradeify-compatible overnight hold — enter at the
  18:00 ET Globex reopen, out 09:30, inside one session). It entered only every other evening: at
  18:00:00 the daily 17:00–18:00 maintenance hole fires the feed-gap guard, which resets flat
  strategies and re-seeds `trades_today` from the runner's day count — still the PREVIOUS trading
  day's, because the 18:00 rollover ran only after the tick batch. The 18:00 bar closed in between
  and the previous evening's hold blocked the new one (`trades_today=1/1`). `paper_main` now rolls
  the trading day per tick, from the tick's own time, before the gap guard; the wall-clock check
  after each batch remains. Affects every strategy that trades the 18:00 reopen (`orb_t1800_globex_*`,
  the Asia/Globex ids): their day counters now belong to the right day. Golden byte-identical
  (its window is RTH). Hold mode logs why an entry was skipped.
- Fleet: `beta_on_1800`, `beta_on_1800__htf`, `beta_on_1800__wide` (stop 400 — the 150-pt stop was
  hit on three of six nights) and `es_beta_on_1800`. Six-night replay: −$34, −$4, −$302, +$183,
  −$302, −$302 for the base — noise, as expected.

### Fixed — a resumed paper position was managed by catch-up ticks older than its entry (2026-09-29)
- Found through the dashboard's fleet audit ("max_daily_trades respected: violations 7", all
  `keltner_ride_15_1m_chand*` on 2026-09-23). The engine restarted at 09:43:08 ET, resumed 14 open
  shorts entered at 09:43:00 from `paper_positions`, then replayed its 5-minute tick catch-up from
  09:38 — and stopped every one of them on a 09:38:13 print, writing trades whose exit precedes
  their entry (−$21.5 each). Both paper brokers now ignore ticks older than the resumed position's
  entry for management (`on_tick` early return); regression test `resume_ignores_ticks_before_entry`.
- Audit hardening (dashboard `fleet_audit.py`): new check "exit after entry on every trade" (fails
  on impossible rows, names them); the sibling-subset check now applies only to ENTRY-gate overlays
  (sg/imb/micro/all/inv/sz/wait) and warns instead of failing — exit-side siblings (`__btp`, `__bx`,
  `__bbe`, targets, wider trails, regime exits) leave the market earlier and legitimately take
  entries their base sits out of; model-input coverage and the per-strategy KPI check are judged on
  the FLEET label (replay grids never get context). Insights per-strategy KPIs likewise read the
  fleet label only — that alone cleared 490 of 491 "mismatches". The feature-coverage check now
  treats NaN-heavy rows as expected when there is no history to read: the first recorded RTH day
  (recording started 2026-09-21 09:32 ET; even 1-minute returns are undefined for its first
  trades) and the first minutes after a 2h+ feed hole (the Sunday Globex open). Fresh audit after
  the quarantine: 0 fail, 0 warn, 58 ok.
- The 14 impossible rows (09-23 09:43 → 09:38:13 and one 09-25 sibling, 14 strategies, −$303 in
  total) were moved with their trade_context rows to the label `invalid_resume_20260923` — out of
  the fleet, still in the database, reversible with one UPDATE. Fresh audit: 0 fail, 1 warn, 57 ok.

### Added — beta-exposure strategies in the paper fleet (2026-09-29)
- Founder asked what strategies give beta exposure, then "for now add the rest in leaderboard
  rotation". New trend-engine mode `hold`: one entry at `hold_entry_hhmm` in `hold_dir`, exit at
  the window end via `check_eod` (windows may wrap midnight), optional filters `hold_tom` (day ≥ 28
  or ≤ 3), `hold_pre_event` (tomorrow is fomc/nfp — paper_main feeds `set_event_next_day`),
  `hold_up_day`. A hold position survives the 18:00 ET `reset_session`. The paper broker's entry
  window and EOD flatten now wrap midnight when EOD < open (overnight holds); normal windows are
  unchanged (test). Seven trend tests + one broker test.
- Fleet variants (1 MNQ, stop 150, no trail): `beta_on` 15:55→09:30 (the overnight premium),
  `beta_on__htf` (4h EMA-20 filter), `beta_on_tom`, `beta_on_prefomc`, `beta_on_short` (control),
  `beta_close` 15:00→15:59 on up days, `beta_day` 09:30→15:55, `beta_day__htf`; ES mirror gets
  `es_beta_on`, `es_beta_on__htf`, `es_beta_day`. Label `beta` holds the six-session backfill —
  noise at this horizon (overnight long −$433, short +$224, day long +$649, three 150-pt stops).
- Paper only by construction: Tradeify forbids holding through 17:00 CT and the live executor's
  EOD does not wrap midnight. Known edge: a Friday entry closes at the first tick after the weekend.

### Changed — every strategy runs for ever; stats by week, month and year (2026-09-29)
- Founder: "we want all to keep running for ever and get stats per week, month, year". The 1,040
  strategies retired on 09-26 are enabled again and the 466 research ids from the 09-28 grids are
  now fleet strategies too: `config/paper_fleet.json` = 2,170 enabled, paper_strategies all
  enabled, engine restarted — 2170/2170 active at 4.5 % CPU / 40 MB. CLAUDE.md fleet rule (4) is
  superseded: nothing is pruned again.
- Dashboard: leaderboard periods gain this month / this year / 90 d / 365 d (calendar periods are
  to-date, New York). New Strategies tab **Week · month · year** on `/api/cpp/strategies/periodic`:
  one row per strategy (paper label + live tags) with all-time total, $/contract, trades, positive
  periods / periods traded, and a cell per calendar period (net · trades); bucket week (Monday) /
  month / year, search, sortable columns, server-side paging.
- Strategies page (09-29 morning): registered ids outside the fleet config showed as `research` and
  left the fleet count — moot now that all ids are in the fleet, kept for future grids.

### Added — four trend modes, an ES mirror fleet, exit-side regime, and the trail-width finding (2026-09-28)
- Founder: "do all you said". Queue and status in TODO.md ("Build queue 2026-09-28").
- **Trend modes** (`trend_strategy.hpp`, strategy files otherwise untouched): `orb_retest` (enter on
  the pullback to the broken range edge, one per side), `gap_fade` (fade a ≥ gf_min_pts gap toward
  the prior close, strategy-owned `gap_filled` flatten), `news_break` (range from `nb_hhmm` for
  `nb_range_min`, first close beyond it; `nb_event_only` reads `set_event_day`, which paper_main
  and the executor feed from `calendar` kinds fomc/nfp), `ib_break` (initial balance break with a
  strategy-owned `ib_target` flatten at ib_ext × the balance). Ten tests in test_trend_strategy.
  Grid (label `newmodes`, 28 variants): gap_fade 2 gap days / 2 winners, news_break 14:00 five
  small winners, 08:30 flat (needs `session_open_hour: 8` — the paper broker's own entry window
  starts at 09:30), ib_break PF 0.35, orb_retest flat. Four keepers forward-test.
- **ES mirror**: `config/paper_fleet_es.json` runs the top 24 NQ bases on the ES feed as MES
  ($5/pt) under label `es`, ids `es_*`, point-denominated knobs scaled by the ES/NQ ATR14 ratio
  (0.144) and the engine's hard-coded NQ base exits written explicitly at ES scale. Unit
  `paper-engine-local-es.service` (deploy/, installed, enabled). Leaderboard: data label `es`.
- **Exit-side regime**: `regime_exit_min_eff`, `trail_step_trend`, `trail_step_range`, `tp_r_range`
  select the trail step / target from the session's efficiency at exit time; `RegimeState::
  trail_step_for / take_profit_for` are the one selector both the paper broker and
  `OrderManager` call (`set_regime` on both). Parity cases: trend and range shapes, 0 ticks apart.
  Grid (label `rgexit`, 64 variants on 8 bases): every trail-25 setting beat its base 8/8. The
  control (label `tr25`, plain `trail_step: 25`, no regime) beat the base by MORE on 7 of 8 —
  orb_t1000_2nd_5 $198 → $746, fib day $394 → $703, ichimoku $289 → $696. Conclusion: the fleet's
  trails (6/10/15) are too tight; the regime split adds nothing on this sample. Eight `__tr25`
  keepers forward-test. Follow-up: trail sweep 25/35/50; trail 25 proposed for the live hand-off.
- **Combined gates** (book fade + regime, label `combo`): 1–2 trades each, all losers — the gates
  block each other. Not kept.
- Research labels now in paper_trades: fibgrid, fibgrid2, rggrid, tpgrid, newmodes, combo, rgexit,
  tr25 (+ backfill from 09-26). Research ids stay registered with enabled=false; keepers enabled.

### Added — fixed take-profit exit, and why it is not the answer (2026-09-28)
- `tp_points` / `tp_r` (target = tp_r × sl_points) on OrbConfig and paper params, off by default.
  Paper broker: a resting limit that fills AT the target on the touch (`take_profit`). Live
  `OrderManager::check_trail_and_stop`: flattens at market on the touch through the software-stop
  exit path (cancels the exchange stop first). Parity test: same tick, same reason, long and short.
- Grid (`config/paper_tp_research.json`, label `tpgrid`, 24 bases × tp_r 1/1.5/2/3, six sessions):
  every R-multiple LOSES on average — Δnet per variant −$79 (1R), −$62 (1.5R), −$48 (2R), −$25 (3R);
  win rate barely moves, profit factor falls from 2.30 to 1.35–2.25. The fleet's edge is the
  trailing runner and a target caps it. Only 3R on the classic ORB bases (orb_3_15_*, orb_5_20_15,
  orb_5_morning) came out ahead; those five forward-test as `__tp2`/`__tp3`. Not for fib/trend.

### Added — the collector records eight instruments (2026-09-28)
- Founder: "start getting data from more instruments". `RITHMIC_EXTRA_SYMBOLS` now accepts
  `SYMBOL:EXCHANGE` per entry (config.hpp `ExtraSymbol`; default = the primary exchange), so
  CBOT/NYMEX/COMEX roots ride the same session: `.env` = `ES,RTY,MNQ,MES,YM:CBOT,CL:NYMEX,GC:COMEX`.
  Verified after the restart: ticks and BBO for all eight within a minute (LAST_TRADE|BBO for the
  extras; depth stays NQ-only). `contracts` gains MES/RTY/YM on the quarterly rule; CL/GC roll
  monthly and are not modelled. `bars_1m`, `book_1m` and `session_stats` pick new symbols up by
  themselves. Disk: 93 % full, 66 GB free — expect ~2.5x today's tick volume; retention needs a
  decision within about two months.

### Added — regime gate: every strategy can trade only in a session shape (2026-09-28)
- Founder: "what about regime filtering… lets try it". Evidence first: paper P&L per contract by the
  day's closing type flips sign for almost every family (ORB +4.2 trend / −1.8 range, fib_pullback
  +8.0 / −3.1, momentum scalper +0.3 / −9.6 with all of its −$23k on range days, vwap_trend and
  failed_breakout the mirror). The day type is hindsight, so the gate reads only what is known at
  entry time.
- `paper::RegimeState` (paper_quote.hpp, beside the book gate): open/high/low/last since the 09:30 ET
  open of the current trade date plus the prior day's ATR14 from `session_stats` → `range_atr`,
  `eff` (|last−open|/range), `move_atr`. OrbConfig keys, all off by default: `regime_min/max_range_atr`,
  `regime_min/max_eff`, `regime_min/max_move_atr`, `regime_with_move` (+1 with the session move,
  −1 against), `regime_min_minutes` (15). Fail-CLOSED when an ATR reading is asked for and no
  `session_stats` row exists — an ungated fallback would silently turn a variant into its base.
- Applied in the same slot as the book gate in both paper brokers and the live executor
  (`executor_main` seeds the ATR at session start and on the 18:00 ET rollover; blocked signals log
  the readings). `PaperDb::session_atr14_before` / `OrbDB::session_atr14_before`. Strategy logic
  untouched. Six tests in `test_paper_broker` (readings, warm-up, RTH-only, date reset, fail-closed
  ATR, with/against the move, a blocked paper entry).
- **Grid** (`config/paper_regime_research.json` → label `rggrid`, 24 top bases × 5 gates, six
  sessions): no gate raised net P&L on average — they block 55–80 % of entries, mostly the 30-min
  warm-up on early ORB entries (404 `regime_warmup` blocks) — but `rg_nochase` (≤0.5 ATR from the
  open) and `rg_trend` (eff ≥0.5, with the move) were better per contract on the fib/ORB-2nd bases
  (e.g. `fib_pb_1m_deep__am_x15__rg_nochase` $32.8/ct vs $26.6, `orb_t1030_2nd_5__rg_trend` $45.1
  vs $11.1 on 5 trades). 09-21/22 had no prior ATR row, so ATR-gated variants sat out those days.
  Eight `__rg_*` keepers added to the fleet for the forward test; 112 research ids stay registered
  but disabled.
- **Golden drift, new evidence (not caused by this change)**: the pre-change and post-change
  `paper_engine` replay the golden window byte-identically (504 trades, twice each), yet the file
  frozen at 20:11 the same day holds 408. Between the two runs `bbo` autovacuumed (21:03). The first
  diverging row is an ORB-2nd entry FILL (30953.75 vs 30954.00) at 14:35:28.500174, a microsecond
  that carries several ticks with the same `seq` and different prices — details in TODO.md.
  Golden re-frozen for the fleet change (8 new rows).

### Changed — hand-off FIB_PB_1M_DEEP: exits re-tuned, window 10:00–15:55 (2026-09-28)
- Founder: "find more trades and also optimize these trades". Method per the paper-fleet research
  rules: 162 replay variants over the six recorded sessions 09-21..09-28 (`config/paper_fib_research.json`
  → label `fibgrid`, `config/paper_fib_research2.json` → label `fibgrid2`; ids `fibr_x_*` exits,
  `fibr_e_*` entries, `fibc_*` combined; registered in `paper_strategies` then set `enabled=false`
  so they never qualify for the rotation).
- **Diagnosis**: 11 of the live twin's 18 trades were break-even scratches at +$0.50 after an average
  6-pt MFE — `trail_be_trigger` 3 pulled the stop to entry+1 within seconds. Trail exits averaged
  +$82 with 50-pt MFE; stops −$17.50.
- **Exits** (base 10–12 entries, 13 trades): every combination with `trail_be_trigger` 3 ranked
  last; means over the grid — BE 12: $217, 8: $201, 5: $190, 3: $127; trail 15: $229, 10: $172,
  6: $150; sl 8: $209 vs 12: $158. Winner sl 8 / trail 15 / BE 8 / delay 0 → $349 PF 3.84 vs the
  twin's $53. Today's single slot trade: $1 live → $133 under these exits.
- **Entries**: zone 0.5–0.786 ($102 mean) beats 0.382–0.786 ($57) and 0.382–0.618 (−$30); swing 30
  beats 20/45; cap 8 adds trades, not P&L; tf 2/3 min lose. More trades come from the WINDOW:
  10:00–15:55 (no ORB conflict) 21 trades / $419 vs 10–12's 13 / $349 at cap 4; the 12:00 hour is
  the weak one, 09:30–10:00 the best but ORB's.
- **Live** (`config/tradeify_handoff_config.json`, from Tuesday 10:00): trail_step 6→15,
  trail_be_trigger 3→8, trail_delay_secs 300→0, win_end 1200→1555, flatten 15:56, cap 4, qty 1;
  `paper_source` → `fib_pb_1m_deep__day_x15`. Previous file in `config/archived/handoff_history/`.
- **Fleet**: `fib_pb_1m_deep__am_x15`, `__am_x15b`, `__day_x15`, `__day_x15_m8` added (forward
  test; paper engine restart scheduled 16:02 ET by a transient timer). Golden re-frozen for the
  fleet change (the known ORB re-entry drift still applies).
- Caveat: six sessions, ~45 % win rate, P&L carried by a few $150–190 trades; replay of the base
  differed from its forward rows (22 trades / $127 vs 18 / $280), so read the forward test before
  trusting the exact figures.

### Changed — duplicate fill deliveries no longer log CRITICAL (2026-09-28)
- Rithmic reports one fill twice (tid=352 exchange notification, then the tid=351 COMPLETE).
  The second arrives after the basket is closed, so both executor fill branches treated it as an
  unowned fill on our account and logged `CRITICAL: … order we do not own` before the router
  classified it as a duplicate — on the first live FIB_PB_1M_DEEP session every exit alarmed.
  New `notif::unowned_fill_is_duplicate()` (the router's own key rule, shared) is asked FIRST;
  a duplicate logs `tid=35x duplicate delivery of a processed fill — skipped`, everything else
  keeps the CRITICAL line and the guards/halt path unchanged. Test 18 in
  `test_trade_end_invariant` pins the helper (true for the booked exit, false for an unknown
  tag or a different cumulative qty).
- Hand-off engine FIB_PB_1M_DEEP went LIVE 2026-09-28 at 1 MNQ, 10:00–12:00 ET (founder;
  `config/tradeify_handoff_config.json` qty 2→1, dry_run false; `strategy-handoff.timer`
  enabled). The Monday dry rehearsal was retired (timers disabled, config archived).

### Fixed — every process survives a Postgres restart (2026-09-26)
A `systemctl restart postgresql` (applying the shared_buffers/work_mem tuning) found three
processes that did not heal on their own:
- **paper_engine** sat on the dead connection for 6 min (9.6k failed writes). Every query
  now goes through `PaperDb::live()`, which `PQreset()`s a dead connection in place (at most
  once per 5 s). Verified with `pg_terminate_backend`: one WARN, then
  `[PAPER-DB] reconnected to PostgreSQL`.
- **rithmic_engine (collector)**: the writer thread already reconnected before each tick/BBO
  drain, but on a quiet weekend nothing is written, so nothing reconnected — and the status
  coroutine's `db_->summary()` failure was swallowed by `catch (...) {}`, so the per-minute
  status line simply went silent for 13 min. Now: the status path logs the failure and
  reconnects `db_` (opened read-only, so the io thread never runs schema DDL);
  `flush_sentinel`/`flush_metrics` call `ensure_db_connected()` like the drains; and
  `write_sentinel_alerts` returns how many rows it consumed, so alerts are held in
  `sentinel_pending_` (cap 5000) across a dead connection instead of dropped.
  Verified live: kill → `status query failed … DB connection lost, reconnecting` →
  `PostgreSQL reconnected` → status line back 60 s later.
- **nq_executor**: `OrbDB` already reconnects before every query (and re-takes the instance
  lock), but the standalone `AuditLog` connection had no owner to reset it, so `audit_log`
  went dark until the next process restart. `AuditLog::flush()` now repairs its connection
  itself (`PQreset`, 5 s rate limit, events stay buffered). Takes effect on the executor's
  next restart.

### Changed — the evening batch (2026-09-26, founder: "1,3,4,6,7,10. do all rest")
- **Hand-off engine is `FIB_PB_1M_DEEP`** (`rotate_handoff.sh --force fib_pb_1m_deep`: config
  generated from the paper params, validated, old `MTF_FLAGS_V5` config archived to
  `config/archived/handoff_history/`, `dry_run:true` carried). Monday's rehearsal config is a
  dry-run copy (label `tradeify_dry`). Tuesday go-live unchanged: flip `dry_run`, enable the timer.
- **Fleet pruned to 635 active of 1,675**: `nr7`, `supertrend`, `roc_momentum`, `trend_day`, all
  `globex_*`, and the overlays `__micro __imb __all __sz __wait __bx __bbe` set `enabled:false`
  (rows kept, `paper_strategies.enabled` mirrored); `__pm` set removed; `__open` siblings added
  for every 1-minute trend base (30); `__inv` on the new `__am`/`__open` winners (6 — the backfill
  shows the gate blocks trend entries almost entirely: 1–2 trades each, so expect them inert).
  Backfill re-run for the new fleet (130 s, 5,512 trades / 522 strategies); `__open` held:
  donchian_10 +12.9, ema_pb +11.5, keltner +9.6, delta_div_1m_30 +8.8, ema_ribbon_1m +7.7 per contract.
- **ORB post-stop cooldown runs on the engine clock** (`orb_strategy.hpp`, founder-approved: tick
  timestamps instead of `steady_clock`). Test `notify_trade_filled_cooldown_applies_to_all_exits`
  now also asserts re-entry AFTER the cooldown.
- **Total tick order in both pg feeds** (`PaperDb::poll_ticks`, executor pg feed): `seq` is only a
  0..4 batch sub-index, so `ORDER BY ts_event, seq` left 20 % of ticks in heap order and the golden
  replay drifted after a VACUUM; now `ORDER BY ts_event, seq, price, size` (the dedup key) plus a
  page-boundary guard that never splits a same-microsecond group. Live and paper now see the
  identical tick sequence. Golden `--twice` pairs: 90 → 0 differing rows; baseline re-frozen (394).
  **Residual**: runs minutes apart still differ by 10–24 ORB re-entry rows with md5-identical inputs
  — open defect, evidence and next step in TODO.md.
- **`live_order_events`** (`OrbDB::write_order_event`, never throws, one WARN a minute at most):
  new_order_sent / new_order_failed / cancel_sent / gateway_ack / gateway_reject / rithmic_notify
  (tid=351, raw fields) / exchange_notify (tid=352). Executor restarted; first rows Monday.
- **`audit_log` trimmed**: the collector no longer writes `ticks.written` per batch; the 1,799,790
  existing rows deleted, `VACUUM FULL` → 252 MB → 160 kB (461 real events). DB 2,217 → 1,979 MB.
- **Backup**: `scripts/pg_backup.sh` + `pg-backup.timer` (02:40 nightly, `--verify`, keep 14,
  Sunday off-box copy to Oracle — best-effort, Oracle unreachable tonight). First dump 65 MB / 7 s.
- **New tables/functions**: `contracts` (+ `third_friday()`, `front_month()`), `calendar`
  (+ `upcoming_events()`), `session_stats` (+ `refresh_session_stats()`), `book_1m`
  (+ `refresh_book_1m()`), views `bars_5m/15m/1h`; `refresh_bars.sh` applies and refreshes them
  every minute (1.7 s first run incl. the 10,885-row book_1m backfill). Decisions recorded in
  TODO.md: keep `idx_ticks_ts` (collector summary), keep ES (mtf_smt reference).

### Added — 63 paper-fleet variants: time-of-day windows + tape grid (2026-09-26, founder: "add new variation of strategies")
- The 7-day leaderboard BY SLOT (`strategy_leaderboard(7, true, …)`) showed the same modes winning in
  one hour and losing in another: fib_pullback +4.0/ct PF 2.5 and rsi2 +3.7 in 10:00–12:00 but
  −9.7 / −7.0 over lunch; vwap_fade +13.6/ct PF 9.9 only after 13:30; ORB +3.8 PF 2.9 in
  09:30–10:00 and −3.6 after; almost everything negative 12:00–13:30. Most variants trade all of RTH,
  so afternoon losses bury morning edges. New sibling variants (`base_id`/`overlay` like the book
  overlays): `__am` (entries 10:00–12:00, 14 trend + 6 mtf via `session_window`), `__pm`
  (13:30–15:55, 11 mean-reversion/fade), `__open` (09:30–10:00, 10), `__am_htf` / `__am_chand` /
  `__am_ts` (the never-varied generic knobs on the four AM winners), plus a `volume_burst_*` /
  `delta_div_*` parameter grid (10). Fleet 1,597 → 1,660; paper engine restarted, 1660/1660 active.
- **Backfill replay the same evening** (`paper_engine --account-label backfill --replay-from
  '2026-09-21 09:25' --replay-to '2026-09-25 16:05'`, 590 s, 13,051 trades / 1,298 strategies):
  the recorded ticks let every strategy, new ones included, be scored on the same five sessions
  instead of waiting for the forward test. Per contract: the `__open` window (09:30–10:00) was
  positive on all 9 variants (donchian_10_1m +12.9 PF 2.8, ema_pb_9_21_1m +11.5 PF 4.2, keltner +9.6,
  roc_6 +8.0 — each above its all-day base, i.e. the window removes losses); `__am` mixed
  (ema_pb_9_21_1m +8.7 PF 3.0, ichimoku_1m_fast +11.8, volume_burst_notrend +11.2, mtf_htf30 +4.5 vs
  base −3.9, mtf_flags_v5 +2.3 vs base −0.5; delta_div and supertrend lose 10–12); `__pm` NOT
  supported (vwap_fade_1m −1.6 on 25 trades — the earlier +13.6 came from overlay rows and the
  double-counted audit day); HTF gate helps ema_pb (8.7 → 12.6) and vwap_5m, hurts fib_pb
  (3.7 → −1.5); chandelier 2 ATR and 45-min time stop never triggered on 1-minute scalps (identical
  numbers — inert knobs there); tape modes only work on the 1-minute frame (volume_burst_1m_notrend
  +11.6 PF 5.4, cl05 +10.7, delta_div_1m_wide +8.3, _12 +6.4; every 2m/3m/5m variant negative).
  12–25 trades each over 5 sessions — directions, not verdicts.
- `strategy_leaderboard()` gained `p_paper_label` (default `'tradeify'`): it had excluded only the
  `golden` label, so the dashboard's `audit` replay of 09-25 was double-counted in every strategy's
  7-day stats (and would have fed Sunday's rotation). The dashboard endpoint takes `?label=` and
  the Leaderboard tab has a **data:** selector (live paper fleet / replay: backfill / audit).
- `mtf_flags_v5__am` is the honest paper twin of the live hand-off config (which trades 10:00–12:00,
  while `mtf_flags_v5` trades 09:00–12:00 and loses the 09:30–10:00 hour) — repoint `paper_source`
  once it has a week of data (TODO.md §4).
- Golden replay: re-frozen after the change (new variants add trades). NOTE the replay is not fully
  deterministic: `OrbStrategy`'s post-stop cooldown is `steady_clock` (orb_strategy.hpp:192, a
  strategy-logic file — not touched), so `orb_*_2nd*` re-entries vary with replay speed. Measured
  with two back-to-back replays of the same window: 20 differing rows on the pre-change fleet, 90
  on the larger one (all ORB families) — pre-existing, and it grows with fleet size because the
  replay runs slower. Founder's call whether the cooldown may switch to the engine clock.

### Removed — the old Python bot's Postgres mirror tables (2026-09-26, founder: "full tidy")
- Dropped `trade_log` (175 rows), `daily_stats` (1), `orders` (24), `gate_results` (114) —
  all last written 2026-05-11 by the retired Python dual-write — plus the never-written
  `trades`, `latency_log`, `session_summary`, and the three views over `trade_log`
  (`v_daily_pnl`, `v_equity_curve`, `v_loss_limit_status`). Rows archived first to
  `data/archive/legacy_pg_20260926/*.csv`. `loss_limits` stays (live risk limits).
- `TickDB::ensure_schema()` no longer creates them; `write_gate_result`/`GateResult` removed
  (no caller). `migrations/20260926_drop_legacy_tables.sql` sorts last so a fresh
  `provision_oracle.sh` ends in the same state.
- Dashboard (`~/Desktop/bot`): the `latency_log` query in `cpp_engine.py` is gone
  (`latency` stays `[]`), the dead `pg_insert_trade/daily_stat/order` helpers are removed,
  `pg_write_gate_result` is a no-op (its four callers are the legacy backtest pipeline),
  `scripts/pipeline_status.py` deleted. NOTE: `/api/stats/*` and `/api/chart/stats` were
  unaffected — they read the legacy SQLite file, not Postgres.
- Decided the same day: retention = keep everything for now (2.2 GB, ~200–250 MB/day,
  disk 93%); partitioning/TimescaleDB = leave as is (Timescale is not installed; the
  collector's `create_hypertable` WARNs at startup are harmless).

### Fixed — stale orders after a trade (2026-09-24 re-audit, 23 trade-end scenarios)
- **Breakeven before the stop's server id mapped left NO exchange stop**: the client-id cancel
  failed, `on_cancel_failed` re-adopted the old stop and cancelled the replacement, then the
  LATE-MAP cancel by server id killed the old stop too. A failure on the client-id attempt is
  now ignored once a server-id cancel for that stop is in flight.
- **A failed cancel from the PREVIOUS trade's stop was applied to the current trade** (swapped
  out its real stop — wrong direction after a reversal). Cancelled stops carry the trade they
  belonged to; only the same trade may re-adopt.
- **Entry/exit cancels went by client id** (EOD cancel of a pending entry, pending-entry timeout,
  stuck-exit retry) — Rithmic cannot route those, the orders kept resting. They now use the
  server id, or are re-sent by server id when it maps; a retried stuck exit is guarded like a
  cancelled stop.
- Daily-trade-limit no longer exits the process (outside cycle_mode): exiting in the same tick
  as the close skipped the post-close recancel window.
- Ghost-fill halt clears on any tid=451 update showing exchange flat + consistent (it could
  block entries for the rest of the session).
- New alarm: a cancelled stop unconfirmed for 60 s → CRITICAL log + grid-notify (2026-09-21:
  nine refused cancels left stops working 41–52 min).
- `scripts/strategy_handoff.sh`: ORB → trend handoff during RTH (one executor at a time),
  waits for flat + no pending stop cancel, hands back to ORB after the session.

### Fixed — remaining audit items (2026-09-24, afternoon)
- **Shutdown drain**: SIGTERM sets `g_draining` before `g_running=false`; the ORDER/PNL plant
  readers, heartbeat and the 1 s housekeeping loop keep running (tick feed stopped, strategy
  halted) so exit fills, cancel ACKs, "Cancellation Failed", late server ids and reconciler
  unwinds are still processed. Phase-2 runs ≥ 8 s and up to 25 s while a cancel is
  unconfirmed or the exchange disagrees with us.
- **Reconnect mid-trade**: when the startup snapshot unwinds a position this process does not
  own, the previous cycle's working orders are CANCELLED by server id (were kept as
  "protective stops" protecting nothing). The live stop's server id is persisted to
  `pending_stop_cancels` as soon as it maps.
- **Unwinds are retried**: NetReconciler re-acts every 15 s while a mismatch survives an
  action, cancelling the previous unwind (by server id) first; the executor re-checks the last
  exchange net every second (tid=451 updates only arrive on account changes).
- **tid=352 fills use the cumulative `total_fill_size`** (per-event sizes made the second equal
  partial look like a duplicate).
- **Partial entry/exit is consistent while pending**: any net between 0 and the full position on
  the position's side (was: only 0 or full → a partial entry was unwound after 5 s).
- **A replaced stop that fires before its cancel lands is the exit** (was routed as unowned:
  entries halted while in the trade, then a second exit flipped the net).
  Tests: test_trade_end_invariant 26/26 (#24–26 fail on 5eba64a).

### Added
- **Live executor runs any trend-engine variant (`"engine": "trend"`).** `run_executor` /
  `flush_position` are templated on the strategy type; `main` builds `OrbStrategy` or
  `TrendStrategy` (params read by `TrendConfig::from_json_string` from the same file) and runs
  the unchanged session loop — same OrderManager, orphan guards, NetReconciler, risk and broker
  day-P&L halt. ORB-only paths (chase filter, `seed_orb_range`, ORB minute log) are compiled out
  for trend. A trend signal that leaves the order manager FLAT (entry rejected, exit while
  already flat) releases the engine after `emit()` returns, so it cannot sit "in position" all
  day. Trend adds an executor backstop flatten at `eod_flatten_hour:min`. `rs_continuation` and
  `book_imbalance` are refused (the executor feeds no ES bars / BBO). Config validation: engine
  must be orb|trend and a trend engine needs its own `strategy` tag. First config:
  `config/tradeify_trend_config.json` (trend_supertrend_7_3_1m). Tests in test_trend_strategy.
- `scripts/stack_status.sh` / `make stack-status`: one index of every trading process — units
  with pid/RSS/OOM score, STRAY executors/collectors not owned by their unit, feed age, last
  broker line, memory and Chrome inside/outside the browser cap. Exit 1 = not ready.
- `scripts/overnight_live_test.sh`: unattended drill → live ORB (18:05 ET, 2 MNQ) → live trend
  (19:00–20:00 ET, 2 MNQ) → production hand-back, each start gated on PNL plant + `[BROKER]` +
  flat exchange; any failure stops everything, sets NO_DEPLOY and alerts.
- `scripts/pre_rth_check.sh` + `deploy/pre-rth-check.{service,timer}`: weekday 09:00 ET
  readiness report to Telegram.
- `deploy/dashboard-api-local.service`: the :8080 dashboard backend as a managed unit
  (MemoryMax 4G) instead of a terminal process. `deploy/browsers.slice` + `scripts/chrome-capped`
  (installed as the google-chrome desktop entry): Chrome capped at 10 GB.

### Changed
- **Trading date rolls at 18:00 ET** (`trading_date_str`, orb_config.hpp): the executor's
  "today" — trade_date, daily P&L seed, trades_today seed, rollover reset — now matches CME and
  Tradeify's daily reset (17:00 CT). An evening session no longer inherits the closed day's P&L
  and trade count (it previously did until midnight ET). RTH rows are unchanged.
- **Instance lock is per account** (orb_db.hpp `instance_lock_key`): one executor per account
  whatever its engine/strategy tag, so an ORB and a trend executor can never trade one account
  at once.
- Local units: executor + collector `OOMScoreAdjust=100` (user services defaulted to 200, which
  made a 7 MB executor a likelier OOM victim than browser tabs), `MemoryMin`, `CPUWeight/IOWeight
  1000`; paper engine `MemoryMin=64M MemoryMax=2G`.

### Fixed
- `collector_watchdog.sh`: `08xx`/`09xx` ET times were parsed as octal (`value too great for
  base`), so the market-hours check misfired 08:00–09:59 ET. Also adds a once-per-episode
  memory-pressure alert (< 2 GB available, or swap ≥ 95% with < 4 GB available).
- **Orphaned stop → hidden position → broker liquidation (2026-09-23, tradeify, ~$600).**
  A stop cancelled by CLIENT id (server basket not yet mapped) came back
  `Cancellation Failed` (tid=351 notify_type=17), which the executor did not handle; at
  FLAT the guard was purged as "confirmed"; 12s later the still-working stop filled
  2 MNQ and the fill was logged as `unknown user_tag … ignoring (not our order)`; the
  account stayed long 2 through five restarts (the tid=451 position snapshot was only
  requested when the DB said non-flat) until Tradeify auto-liquidated it 32 minutes later.
  `src/execution/order_manager.hpp`: client-id cancels are tracked (`client_only_cancels_`)
  and never purged as confirmed; `on_stop_server_mapped()` re-sends such a cancel by server
  id the moment the id arrives; `on_cancel_failed()` re-adopts the still-live stop (at its
  real level) while in a trade and cancels the replacement, or keeps the guard when flat;
  `net_qty_consistent()` + `NetReconciler` express "does the exchange agree with us".
  `src/execution/executor_main.cpp`: fills on our account for orders we do not track are
  routed into the order manager's stale-stop guards (FLAT → unwind/ghost-halt; in a trade →
  halt entries) instead of ignored; `Cancellation Failed` is handled; the tid=451
  subscription is unconditional on every start and reconnect; every update is reconciled
  against our state and a mismatch older than `net_mismatch_grace_ms` (new config,
  default 5000) is unwound once and halts entries; the broker's own `day_pnl` is checked
  against `daily_loss_limit` and halts + flattens (`RiskManager::halt_external`).
  Six regression tests replay the sequence (`tests/execution/test_order_manager.cpp`).
  **And the position feed itself never worked**: the whole log held zero tid=451 updates,
  because `RequestPnLPositionUpdates` was sent on the ORDER plant while Rithmic serves
  position/P&L on the PNL plant. The executor now opens a third session (`PNL_PLANT`
  login → tid=400 subscribe → tid=402 forced snapshot, `proto/rithmic.proto` gains 402/403),
  reads it in `pnl_loop`, heartbeats it, and halts entries (`pnl_plant_unavailable` /
  `pnl_subscribe_rejected`) when it is missing — no position truth, no trading. Verified
  live on tradeify: snapshot net 0 with 12 buys/12 sells, balance 24,584.52, day P&L
  -605.34 → broker guard halted at once.
  **Testable and drillable.** `src/execution/notification_router.hpp` holds the routing
  policy for unowned fills, cancel outcomes, the unwind plan (extra contracts → trade them
  away; position closed externally → `OrderManager::adopt_external_close`, never re-enter)
  and the broker-P&L check; `tests/execution/test_incident_replay.cpp` (new gate binary)
  replays the incident's own prices, ids and message order through it and asserts the
  position is closed within seconds in every variant. `nq_executor --drill orphan` +
  `scripts/drill_orphan.sh <account> recover|crash` run the same shape on the REAL account
  with one contract (strategy halted): an untracked BUY 1 after the exchange confirms flat,
  then the guards and the PNL-plant reconciliation must close it (PASS/FAIL, exit code,
  grid-notify); `crash` SIGKILLs the process before the unwind and proves the restart path.


### Added
- **Eleven more signal families in the trend engine, fleet 110 → 150** (founder: "add as
  many uncorrelated strategies with different variations"; `src/execution/trend_strategy.hpp`,
  `tests/execution/test_trend_strategy.cpp` 32 checks, `config/paper_fleet.json` +40).
  Trend: `trend_day` (recognises a trend day after N minutes — net move ≥ k×ATR and ≥ 80%
  of minute closes on one side of VWAP — then buys every VWAP / fast-EMA pullback),
  `failed_breakout` (turtle soup: a Donchian break that closes back inside within N bars
  is faded — the negative of the Donchian family), `keltner_ride`, `ichimoku`
  (Tenkan/Kijun/Senkou, cloud filter), `roc_momentum` (ROC in ATR units at a fresh
  extreme), `delta_trend` (price high confirmed by a session cumulative-delta high from
  the tick aggressor flag; divergence blocks), `fib_pullback` (38–62% retrace of the last
  impulse, resumption close). Mean reversion, the least correlated to everything above:
  `vwap_fade` (≥ k×ATR from VWAP then a turn back, target VWAP), `band_fade` (Bollinger
  close outside then back inside, target the mid), `rsi2_pullback` (Connors RSI-2 on the
  trend side of the slow EMA). Two generic overlays usable by ANY mode: `htf_tf_min` /
  `htf_ema` (higher-timeframe alignment gate in `can_enter`) and `chandelier_mult`
  (flatten k×ATR off the best price since entry). Bars now carry aggressor-buy volume.
  Replay 2026-09-21 (afternoon data only): 22 of the 40 new variants fired — losers
  `mr_vwap_fade_1m` −69.5, `fail_bo_10_1m` −52.0, `delta_trend_1m` −52.0; winners
  `trend_day_ema_1m` +100.0, `trend_tod_1530_close_drive` +46.5. Dashboard: analysis text
  for every mode (mean-reversion modes labelled as such), gate/exit rules, and
  coverage-aware silence reasons.

### Added
- **Four new models in the zoo (10 → 14)** (founder: "go for it"; dashboard
  `ui/services/models/`): **#11 exit_optimizer** — re-simulates every closed trade on its
  own recorded 1-minute path under a grid of break-even trigger / trail step / delay rules
  with the brokers' exact mechanics (incl. the live working-stop rule), chooses per engine ×
  session on earlier days and judges on later days against the live rule 3/10/300;
  **#12 signal_meta** — LightGBM classifier over the signal log (every base entry taken,
  with the book at the signal) predicting P&L > 0, with base rate and a gate backtest;
  **#13 overlay_uplift** — per overlay, the contexts (engine × session × spread × bid share)
  where the paired value vs the base is positive, bootstrap CI per context, regressor when
  ≥300 rows; **#14 fill_cost** — LightGBM regressor of the recorded book cost ($) from spread,
  book state, session, engine and hold time, vs the mean-cost baseline. All refuse below
  their minimum data and report a time-ordered hold-out; none is wired into trading.

### Added
- **Collector runs 24/7 with an external watchdog** (founder: "make sure that the collector
  runs 24/7 uninterrupted"; `scripts/collector_watchdog.sh`,
  `deploy/rithmic-collector-watchdog.{service,timer}`, installed as a user timer every
  2 min). Independent of the collector's in-process link watchdog: restarts the unit if it
  is not active or if the newest NQ tick is older than 120 s while CME Globex is open
  (Sun 18:00 → Fri 17:00 ET, daily halt 17:00–18:03 excluded), alerts once via grid-notify
  and once on recovery, rotates the append-only logs daily above 50 MB (copy + truncate,
  gzip), and writes `data/validation/collector_watchdog.json`. Drilled live: a forced
  stale threshold restarted the collector and alerted; the next run reported recovery and
  ticks resumed within seconds. Fleet audit: tick-feed and feed-hole checks are
  market-hours aware (closed market → warn, not fail) and show the watchdog's last run.
  State verified: units enabled + lingering, sleep-on-AC "nothing", no unplanned reconnect
  in 36 h; disk 93% used but the DB grows ~250 MB/day against 67 GB free.

### Changed
- **Live ORB allowed 5 trades a day** (`config/tradeify_config.json` `max_daily_trades` 3 → 5;
  founder: "add more trades a day to 5 live orb"). Worst case five full stops at 2 MNQ
  = −$340 plus $40 commission, inside the −$500 daily halt. Restarted flat before the open.
- **Live executor sized to 2 MNQ** (`config/tradeify_config.json` `qty` 1 → 2; founder:
  "make it 2 sure"). Per full 15-pt stop −$68, three stops −$204 (daily halt −$500),
  Monday's +96-pt trade would be +$376. Restarted flat at 07:05 ET before the open.

### Fixed
- **Paper trailing exits were tighter than live** (`src/paper/paper_broker.hpp`,
  `src/execution/order_manager.hpp` `exchange_stop()`, parity + broker tests): the live
  executor re-places its resting exchange stop only when the in-memory level is ≥
  `trail_step` beyond the last placed stop (cancel/resubmit suppression), and its software
  backstop watches the EXCHANGE stop — so the working live stop steps up in 10-pt jumps.
  The paper broker ratcheted every tick and exited at the in-memory level. It now keeps a
  `placed_stop_` with the same rule and exits against it; `stop_price_` stays the display
  value. The parity test now compares the two WORKING stops (0 ticks apart on all paths),
  and the golden file is re-frozen for this intended change (trail exits later, larger
  give-back — that is what live does). Board wording updated.

### Added
- **Validation system — every strategy, number and comparison has an independent check**
  (founder: "create a comprehensive plan … and create it"). Four layers:
  1. *Rules*: `tests/paper/test_parity_paper_vs_live.cpp` drives the paper broker and the
     LIVE `OrderManager` with the same config and price path and asserts the stop never
     differs by more than the tick snap, break-even fires on the same tick, and the exit
     reason matches (5 paths: BE-only long/short, trail long/short, plain stop). This is
     the test that would have caught the 2026-09-23 break-even mismatch. In hermes.
  2. *Numbers*: `scripts/golden_replay.sh` + `tests/golden/2026-09-22_rth_open.csv` — a
     frozen 09:25–11:00 window (830 trades across the 1,553-strategy fleet, bid/ask shadow
     included) replayed on every full `make hermes` and diffed trade-by-trade; `--twice`
     requires byte-identical output (determinism, verified). `make golden`,
     `make golden-freeze`. Fleet audit recomputes every strategy's n, wins, total,
     expectancy, profit factor and max drawdown in plain SQL and compares with the
     insights API (`Performance KPIs` section).
  3. *Live vs replay*: dashboard `ui/routers/validate.py` — `POST /api/cpp/validate/parity`
     replays the Globex day and matches each live trade to a replay trade (same strategy,
     direction, entry within 3 s), listing live-only, replay-only, P&L and exit-reason
     differences per engine; `GET /api/cpp/validate/status`. Scheduled in `ui/app.py`:
     fleet audit every hour, parity + audit at 18:30 ET, Telegram alerts via grid-notify on
     the first failure and on recovery (`data/validation/`).
  4. *Statistics*: the Compare leaderboard adds a bootstrap 90% CI and P(mean > 0) over
     per-base paired deltas and a verdict that stays "insufficient evidence" until ≥30
     paired signals across ≥2 sessions — direction is shown, decisions are withheld.
  Strategies page → Audit tab gains a Validation panel (golden, parity, hourly audit, hermes).

### Fixed
- **Paper break-even now matches the live executor** (`src/paper/paper_broker.hpp`,
  `tests/paper/test_paper_broker.cpp`): the paper broker moved the stop to break-even only
  after the 300 s trail delay, while `order_manager.hpp` moves it the first time MFE reaches
  `trail_be_trigger`, with no delay (the trail alone waits for the delay). Paper now does
  the same; the test that encoded the old rule was replaced by two that encode the live one.
  `PaperDb` seeding queries stay unscoped when no account label is set (tests, tools).
  Board wording for break-even / trailing corrected to the code.
- **Break-even on book flip could put the stop on the wrong side of the market**: a flip
  while the trade was under water moved the stop to entry+1 above price, which the paper
  broker then "filled" at a price better than reality. The overlay now moves the stop only
  when price is already beyond the break-even level (new broker test); its measured value
  is re-derived below.

### Fixed
- **Three honesty fixes found by auditing the book layer** (`paper_quote.hpp`, both brokers,
  dashboard `compare.py` / `paper.py`): (1) a book fill could look better than reality when
  the quote was stale during a spike — 246 of 1,668 replay trades beat the reported fill by
  more than $2; the honest fill is now never better than the print that triggered it
  (0 of 1,667 after), and the SQL back-fill applies the same floor. (2) "Take profit into
  pressure" fired 65 times at ~zero profit because slippage ate a one-tick gain; it now needs
  `book_tp_min_pts` (1.0) first. (3) The replay endpoint cleared audit trades but not audit
  signals, so gate values and blocked counts for the replay label accumulated across runs
  (they doubled between two runs); the signal log is cleared per replay too. Round-2 numbers
  were re-derived after the fixes.

### Added
- **Book overlays, round 2 — six more ways to use the bid/ask, as paired siblings** (founder:
  "lets try them"; `src/paper/paper_quote.hpp`, both brokers, `OrbConfig` paper knobs). Entry:
  `imbalance_max` (INVERTED gate — only when the book leans against the move; the round-1
  numbers showed the book at a breakout is contrarian). Management/exit: `book_exit_flip`
  (flatten when the book flips against the position), `book_be_on_flip` (stop to break-even
  on the flip, no trail delay), `book_tp_imbalance` (in profit and the book stacks in favour →
  take profit). Sizing: `book_size_agree` (+N contracts when the book agrees at fill).
  Execution: `fill_wait_secs` (wait up to N s for a 1-tick spread or a microprice lean).
  Siblings `__inv __bx __bbe __btp __sz __wait` for the 105 bases with ≥2 live trades →
  fleet 923 → 1553 (1.3% CPU, 30 MB). Compare page: entry gates are scored by "what they
  removed", exit/sizing/execution overlays by paired Δ P&L, with the book-exit count per
  sibling; a "Live executor vs the book" line gives the real account's slippage beyond the
  touch at signal time. Model features `bbo_imb_open15` / `bbo_imb_persist_open15`
  (opening-book pressure and its persistence). Unit test: 55 checks.

### Added
- **The paper fleet reads the book: honest fills, entry gates, a signal log, an
  order-book family, and ~300 sibling variants to compare against** (founder: "Honest
  paper fills, Entry gates on the signal engines, New signal families, Data-quality and
  execution analytics"). `src/paper/paper_quote.hpp` (new): `Quote`/`QuoteState` — spread
  in ticks, bid-share imbalance, microprice, freshness (5 s), the gates and the honest
  market fill. Both paper brokers now take the collector's BBO stream (`PaperDb::poll_bbo`,
  one-sided rows forward-filled; the runner applies quotes in tick-time order, live and in
  replay). **Every paper trade carries its bid/ask SHADOW fill** (`entry_bbo`, `exit_bbo`,
  `pnl_bbo_usd`, `spread_entry/exit_ticks`, `fill_model`; migration 013) whatever fill
  model it ran with — buy at ask, sell at bid, stops no better than the touch — so the cost
  of the book is measured on the same trade. `OrbConfig` gains paper-only knobs (no strategy
  logic touched): `fill_model` (`last_slip` default | `bbo`), `spread_gate_ticks`,
  `spread_gate_rel` (× rolling spread), `imbalance_min`, `microprice_lead`, `base_id`,
  `overlay`. A gated signal is logged, taken or `blocked:<reason>`, with the book at that
  instant, in the new `paper_signals` table — join a sibling to its base by time to see
  exactly what a gate removed. Trend engine: `on_quote` + mode `book_imbalance` (sustained
  bid share ≥ x for N s with a tight spread → with the pressure; flat when it normalises).
  Fleet 197 → 923: 179 bases (every strategy that has traded + every non-09:30 session variant) × 4
  overlays (`__sg` spread ≤ 1.5× rolling, `__imb` bid share 0.55, `__micro`, `__all`) +
  10 `book_imb_*` variants across sessions (one on `fill_model=bbo`); 3.7% CPU, 23 MB. Bases are untouched;
  siblings carry `base_id`/`overlay` in their params. Unit test: 49 checks incl. gates,
  fills and the imbalance mode. Replay of 2026-09-22 RTH across 923: 619 trades, all with
  shadow fills — reported avg $1.60/trade vs $0.57 at the book (≈ $1.03 spread cost at a
  2.2-tick entry spread); gates logged 619 taken / 172 blocked on imbalance / 52 on
  microprice. Dashboard: **Compare page** (`/compare`, `ui/routers/compare.py`) — honest-fill
  panel (same trades, reported vs at-the-book, by session / engine / strategy, winners that
  flip to losers), overlay leaderboard (better/worse/same bases, Δ P&L, paired Δ expectancy,
  signals blocked), base-vs-siblings table, and a signal-by-signal view per base (book at
  the signal, each sibling's decision and outcome). Replay reasons are sibling-aware ("gate
  removed every signal — imbalance ×3"); board analyses list the overlay rules; the fleet
  audit gains a "Book layer" section (shadow fill on every trade, sibling ⊆ base signals,
  shadow never better than reported). Model feature `bbo_spread_rel_1h` (spread regime).

### Changed
- **Paper fleet account envelope switched off** (`config/paper_fleet.json`
  `daily_loss_limit` / `trailing_drawdown_cap` = 0, both disable their check). 197 strategies
  × 1 MNQ on one $25k envelope is not an account: on 2026-09-22 the fleet ran +3.1k by 10:13
  and gave back 2.6k, the $1k trailing cap latched at the 18:00 rollover, every strategy was
  halted and the evening session gathered nothing. The risk unit is the per-strategy
  `strategy_daily_loss_limit` (−250/day) and the per-strategy max-drawdown KPI; the account
  envelope stays available for a curated live-sized subset later. Halt cleared with the
  fleet stopped (peak reset to equity).

### Fixed
- **Live executor took no trade on 2026-09-22: its session open had been rewritten to
  17:31** (`config/tradeify_config.json`, dashboard `cpp_engine.py go_live`). The go-live
  endpoint defaulted `open_in_minutes=20` and PATCHED `session_open_hour/min` into the
  account config file; the 17:30 ET go-live on 09-21 (single-feed switch) therefore left
  `session_open=17:31` on disk, the 24/7 unit reloaded it at 17:30, the ORB window
  17:31–17:36 sat inside the Globex halt, no range ever formed, and the executor logged
  "ORB building … (no range yet)" for the whole 09-22 session while the feed was complete.
  Config restored to 09:30 (`git checkout`), executor restarted flat at 18:04 ET. go-live
  now defaults to the RTH open; `open_in_minutes` / `session_open_*` are explicit-only test
  overrides and documented as persisting in the file. The fleet audit gains a "Live
  executor" section (session open must be 09:30, dry_run, trade_route, contract, today's
  ORB/trades) so a drifted config is red on the strategies page before the next open.

### Fixed
- **End-of-window flatten was batch-granular in replay** (`src/paper/paper_main.cpp`): the
  runner asked strategies to flatten once per poll batch on the wall clock (5,000 ticks ≈
  minutes of replay time), so a strategy whose window ends at 14:00 could be stopped out at
  14:04 in a replay while live (100 ms polls) would have flattened at 14:00:00 — audits did
  not match live. The check now also runs on every minute boundary of TICK time in both
  modes; verified: `mr_vwap_fade_5m_midday` exits `signal_flatten` at 14:00:00 (−3.0) instead
  of `stop` at 14:04:58 (−25.5). Found by the fleet audit (`scratchpad/audit_fleet.py`:
  every closed trade checked against its own strategy's window, flatten time, daily cap,
  P&L arithmetic, direction sign and MAE ≤ P&L ≤ MFE — 0 failures after the fix).
- Per-strategy KPIs (dashboard `insights.py`): expectancy, max drawdown of the cumulative
  curve, average hold, trades/day and a per-session split, shown on the strategies page.
- **Strategies page as a control centre** (dashboard `strategies/page.tsx`): four tabs —
  Fleet, Replay, Audit, Insights & KPIs — with live badges (audit fail/warn, replay traded
  count, trade count); the Fleet tab gains a search box (name, engine, any param), family
  filters (ORB / MTF / Trend / Mean reversion), session filters (RTH, pre-market, London,
  Asia, evening, Globex day, derived from each strategy's own window) and status filters
  (traded today / never traded / halted); each row's drawer shows its KPIs (expectancy,
  profit factor, max drawdown, hold, trades/day, win rate, MFE capture, sessions) and a
  "view trades on chart" link (`/chart?paper=<id>` deep link); the Replay tab gains presets
  (yesterday RTH, today so far, last overnight, last 24 h) and a name filter.
- **Chart page shows the fleet** (dashboard `chart/page.tsx`, `paper.py chart_trades`,
  `chart.py /api/data/range`): defaults to the Paper Fleet source over the collector's own
  tick window (`pg_start`/`pg_end`), overlays every strategy's entries and exits with one
  stable colour per strategy and the strategy name on the marker, unions the executor's
  real fills as `LIVE <account>`, groups the selector by engine, and adds a "Strategies in
  view" legend (n, W/L, P&L, open positions) that isolates a strategy on click.
- **Fleet audit on the strategies page** (dashboard `ui/routers/fleet_audit.py`,
  `GET/POST /api/cpp/fleet-audit[/run]`): the five audit sections (processes & feed, fleet
  state, trade integrity, model input data, KPIs) as a collapsible panel with a run button —
  seconds to run, no replay, no writes; the last result is cached and shown on load.

### Fixed
- **Top-of-book (BBO) had been silently dropped since the collector was written**
  (`proto/rithmic.proto`, `src/client.cpp`, `src/collector.cpp`). The collector subscribed
  `LAST_TRADE|BBO` on every connection, Rithmic acknowledged (`rp_code=0`) and streamed
  ~150 BestBidOffer frames a minute, but the proto carried invented field numbers
  (1542xx/1543xx) so every frame parsed with no bid and no ask and was dropped before
  the counter — `bbo=0` in every status line, 0 rows in `bbo`, and nothing said why.
  Verified on the wire with a raw-frame dump + `protoc --decode_raw`: bid 100022/100030,
  ask 100025/100031, orders 159000/159002, lean (micro) price 154909, is_snapshot 110121;
  presence_bits 1 bid / 2 ask / 4 lean. The "depth" request sent update_bits 64, which
  is HIGH_BID_LOW_ASK (template 153) — not depth; it is off by default now
  (`RITHMIC_MD_DEPTH_BITS`). ORDER_BOOK (bit 4, template 156) answers with an EMPTY book
  (presence_bits 0) on the Tradeify login, i.e. aggregated L2 is not entitled; depth-by-order
  (160) never arrives. Diagnostics kept: per-template frame counts in the status line,
  `ResponseMarketDataUpdate` rp_code logging, `RITHMIC_MD_DUMP_DIR` raw dumps,
  `RITHMIC_MD_EXTRA_BITS`. Dashboard: four top-of-book features
  (`bbo_spread_ticks_1m`, `bbo_imbalance_1m`, `bbo_microprice_dev_ticks`, `bbo_updates_5m`)
  in `features.py`, NaN for trades before the fix.

### Added
- **All-sessions fleet, 150 → 197** (founder: "want strategies to trade on all markets to
  gather more data per session"). The trend engine gains a session anchor
  (`session` = `rth` | `globex` | `window`: what VWAP, cumulative delta and the "session
  open" used by opening_drive / gap / trend_day are measured from) and entry windows that
  cross midnight (`win_start` > `win_end`, e.g. 2000–0230, with the flatten at the wrapped
  end). 47 new variants: **Asia** 20:00–02:30 (Tokyo-open ORBs, 21:30 HK-open momentum,
  Donchian, VWAP/band fades, Keltner, supertrend, NR7, RSI-2, delta, two MTF windows),
  **London** 02:30–07:30 (03:00 drive, Asia-range break, 03:30 EU-cash momentum, trend day,
  the full mode set, MTF), **US pre-market** 07:00–09:25 (08:30 data-release momentum,
  drive, fades, MTF), **US evening** 18:00–23:00 (Globex reopen drive, Donchian, fades,
  Keltner, RSI-2, MTF) and four **whole-Globex-day** trend followers on 15/60-minute bars.
  Every session now feeds the dataset; insights gain a per-session bucket
  (`by_session`, ET: Asia / Asia late / London / US pre-market / RTH open / midday / close /
  US evening) on the strategies page. Caveat recorded up front: overnight paper fills at one
  tick of slippage are optimistic in a thin book — trust the fill-quality model before
  sizing anything there.

### Fixed
- **Whole fleet halted `account_trailing_dd` on a phantom peak; seed queries read audit rows**
  (`src/paper/paper_db.{hpp,cpp}`, `src/paper/paper_main.cpp`). Two residues of the first,
  un-isolated replay on 2026-09-21: (1) it had written the phantom post-gap P&L into the LIVE
  `paper_account` row (peak 26,532 vs a real day peak of 25,472), so the persistent
  trailing-drawdown halt fired at the next restart and every strategy sat idle; the row is
  rebuilt from the label's own closed trades (equity = peak = 25,457.5). (2) `sum_pnl`,
  `sum_pnl_since` and `count_trades_since` matched on `strategy_id` alone — `paper_trades`
  is keyed by strategy across labels, so the 101 audit rows were seeding live daily trade
  counts, P&L and halts at every restart. All three are now scoped by
  `PaperDb::set_account_label(fleet.account_label)`. Operational note: the fleet persists
  its in-memory account state back to `paper_account` while running — repair the row with
  the unit STOPPED, or the repair is overwritten before the restart reads it.

### Added (earlier today)
- **Trend engine — 11 configurable trend modes, 28 fleet variants (fleet 82 → 110)**
  (`src/execution/trend_strategy.hpp`, `tests/execution/test_trend_strategy.cpp`,
  `src/paper/paper_main.cpp` engine `"trend"`, `config/paper_fleet.json`). One
  header-only `TrendStrategy` driven by `TrendConfig{mode, tf_min, win_start/win_end,
  allow_longs/shorts, exit_on_flip, time_stop_min, …}` builds 1-minute → `tf_min` bars
  in ET (a bar closes when its bucket's last minute completes) and emits the same
  `OrbSignal`s the ORB does, so it shares the plain `PaperBroker` (stop / BE / trail
  from the strategy's `OrbConfig` overrides). Modes: `donchian` (N-bar channel close),
  `ema_pullback` (fast/slow EMA, touch-and-resume), `vwap_trend` (retest of a sloping
  session VWAP), `opening_drive` (first N minutes' direction ≥ k×ATR, pullback entry),
  `gap_go` (open gap vs prior RTH close, unfilled after a wait), `pdhl_breakout`
  (prior-day or overnight high/low with volume), `squeeze` (Bollinger inside Keltner
  then a break), `supertrend` (band flip; opposite flip flattens and re-enters once
  flat), `nr7` (narrowest-of-N break), `rs_continuation` (NQ leads the ES reference
  feed by ≥ bp), `tod_momentum` (continuation at a clock time). 14-check unit test in
  the hermes gate. Replay audit 2026-09-21 (afternoon data only): 12 of 28 variants
  fired, with losers recorded (`trend_donchian_10_1m` −64.5, `trend_supertrend_7_3_1m`
  −51.0, `trend_rs_es_15m` −36.5) alongside winners.
- **Feed-gap guard in the paper engine** (`src/paper/paper_main.cpp`,
  `paper_config.hpp` `feed_gap_reset_secs` = 300). A hole in the tick stream longer
  than the threshold (collector down, forced logout, box asleep) now restarts the
  session state of every orb/trend strategy that is flat and not halted (daily trade
  counters re-seeded); open positions keep their stops, halts stay latched, MTF keeps
  its bar history. Why: on 2026-09-21 the feed had no ticks 09:33–11:01 ET, and the
  first tick back fired 36 "entries" across the fleet (an ORB range built on one
  minute of ticks, Donchian channels with a 90-minute hole) — all winners by
  construction of the jump, +$1,081 of phantom P&L. Replay of the same day with the
  guard: 62 trades / +1,059.5 → 26 trades / −21.5. Logged as
  `[PAPER] WARN feed gap …`. Applies identically in replay so audits match live.

### Fixed
- **Trend runners crashed the fleet at start-up (SIGSEGV, 20 systemd restarts)**
  (`src/paper/paper_main.cpp`): the trade-close hook and the position-resume path
  branched on `strategy` (ORB) vs "else bracket broker", so a `trend` runner
  dereferenced a null `PaperBracketBroker`. Both now branch on which broker exists;
  a resumed trend position also seeds the strategy's in-position flag
  (`TrendStrategy::seed_open_position`) so the flip-exit and time-stop see the leg.
- **Stop cancels were being refused by Rithmic (rp_code=1045) — every trailing-stop
  update left the superseded stop WORKING** (`proto/rithmic.proto`,
  `src/execution/executor_main.cpp`, `tools/cancel_stops_main.cpp`). `RequestCancelOrder`
  omitted `manual_or_auto` (tag 154710, the field new orders send as
  `manual_or_auto_select`); Rithmic answered each cancel with `ResponseCancelOrder`
  `rp_code=1045` and no notification, and the executor never parsed tid 317, so the
  refusal was invisible — 2026-09-21 on Tradeify, 9 stale SELL stops sat "trigger
  pending" behind 725 silently refused re-cancels. All cancel builders now send
  `manual_or_auto=2` (AUTO); verified live: all 9 cancelled within 200 ms.
  `cancel_stops` also cancels by Rithmic's `server_basket_id` (the client `MNQ-…` id
  is refused too), logs tid 317 responses, and bounds its close. This supersedes the
  "cancel ACK reverse map" fix, which could not have helped.
- **ORDER_PLANT login refusal no longer hammers Rithmic** (`src/execution/executor_main.cpp`):
  a rejected login (`rp_code=13` — returned both for "too many rapid logins / duplicate
  session" and for bad credentials) used to `co_return` into the 10s cycle loop, i.e. a
  fresh login every ~13s that kept 13 coming back on its own. The rejection is now logged
  with the server's text plus an audit row, and the cycle loop backs off 300s
  (`g_reconnect_delay_s`, reset to 10s on the next successful login).
- **SIGTERM works outside a live session.** `main()` set SIGINT/SIGTERM to `SIG_IGN` and
  relied on the session's `asio::signal_set`, which is only armed after ORDER_PLANT login —
  so `pkill -SIGTERM` was a no-op for an executor stuck in the connect phase or the
  reconnect sleep. Both now route to `handle_signal` (flag only), the sleep is sliced 1s,
  and the handler is re-armed before each sleep.
- **Subscription rejections log the server's reason** (order updates tid=309, market data
  tid=101) instead of just the numeric `rp_code`; a rejected order-update subscription
  also writes an `audit_log` error.

### Added
- **`pg` market-data source: the executor trades on the collector's Postgres ticks**
  (`RITHMIC_MD_PROVIDER=pg`; `orb_config.hpp` `md_provider`/`md_feed_symbol`/`md_poll_ms`,
  `executor_main.cpp` `pg_feed_loop`). A prop-firm login allows ONE TICKER_PLANT session,
  so the executor's own MD login and the 24/7 collector could not coexist (each kicked the
  other every ~35s on 2026-09-21). In pg mode the executor opens no MD session at all:
  `pg_feed_loop` polls `ticks` (symbol `md_feed_symbol`, default NQ, every `md_poll_ms`)
  from "now" (never replays history; the ORB is restored from `live_sessions`) and feeds
  the same `process_tick` handler the WebSocket loop uses. A stale-feed watchdog
  (`tick_timeout_s`, active window or in-position) halts NEW entries with reason
  `pg_feed_stale`, writes an `audit_log` error and fires `grid-notify`; the first tick
  after that unhalts. "MD connected" (`md_up()`) reports feed freshness in pg mode.
  Enabled for `tradeify` via `.env.tradeify` (`RITHMIC_MD_PROVIDER=pg`); verified live
  mid-session: ORB/trade count restored, order plant OK, ticks ~300 ms behind the wire.
- **24/7 collector as a systemd user unit** (`deploy/rithmic-collector-local.service`,
  installed to `~/.config/systemd/user/`, `Restart=always`): the local box's only
  Rithmic MD session. Feeds the paper fleet, the chart and pg-mode executors.
- **Whole local stack under systemd user units, enabled at boot, linger on**
  (founder: "lets have it running 24/7"): `deploy/paper-engine-local.service` and the
  template `deploy/nq-executor-local@.service` (EnvironmentFile `.env` + `.env.%I`,
  `Restart=always`, NO_DEPLOY guard, SIGTERM with a 45s stop window for the exit fill).
  The executor's own midnight-ET rollover plus the live_sessions/live_trades restore on
  restart make a wrapper unnecessary. The dashboard's Go Live / Stop / Paper ▶■ now drive
  the units when installed (`systemctl --user restart|stop`), so a stop is never undone
  by `Restart=always`; without the units the nohup path is unchanged.
- **Trade context dataset** (`migrations/011_trade_context.sql`, computed by the
  dashboard's new `ui/routers/insights.py`): for every closed live/paper trade, the
  opening-range size, entry minutes after 09:30, distance beyond the level at fill,
  pre-5/15-minute range and volume, overnight (Globex) range, open gap, day range at
  entry, hold time, MAE/MFE and MFE capture — all from the collector's `ticks`. Served as
  win-rate/expectancy buckets on the dashboard's /strategies page ("What good trades
  look like"). Buckets under 20 trades are flagged thin.
- **Trade-quality model** (dashboard `ui/services/trade_model.py`, LightGBM): a classifier
  for p(win) and a regressor for expected P&L over the trade-context features (+ the
  strategy's stop/trail and hour of day). Time-ordered hold-out (latest 30%), reported
  against the win-rate baseline, plus a "trade only what the model liked" replay on the
  hold-out and gain-based importances. Refuses to train below 60 closed trades or 12 of
  either class (today: 22 trades, 21W/1L → refused, by design). Endpoints
  `GET /api/cpp/insights/model`, `POST /api/cpp/insights/train`, `POST /api/cpp/insights/score`;
  auto-retrains daily 16:30 ET; model scores annotate best/worst trades on /strategies.
  Verified on a synthetic set with a planted signal (AUC 0.70 out-of-sample, planted
  drivers ranked top). **The executor does not consult it** — strategy logic untouched;
  wiring a gate is a separate, explicit decision.
- **Model zoo — ten analysis models** (dashboard `ui/services/models/`, page `/models`,
  API `/api/cpp/models[/train-all|/{name}/train|/{name}/predict]`, daily train-all
  16:30 ET), ranked by expected payoff: 1 day-type classifier (pre-open, from overnight
  bars), 2 trade filter, 3 adaptive stop & trail (quantile regression on MAE / MFE),
  4 breakout-failure (events mined from ticks at every opening-range cross), 5 variant
  selection (Thompson bandit over the paper fleet), 6 exit/stall model (trade paths
  rebuilt from ticks, 30 s samples, stall-rule replay), 7 regime clustering (k-means on
  hourly bars, P&L per regime per strategy), 8 fill-quality (live trades only),
  9 trade-management policy search (offline-RL baseline replaying real tick paths under a
  trail/BE/time-stop grid), 10 GRU sequence model on 1-minute bars. Shared rules in
  `base.py`: refuse below the model's minimum data, time-ordered hold-out only, baseline
  next to every score. All ten refuse on 2026-09-21 data (3 days of ticks, 22 trades) and
  all ten train correctly on synthetic sets. None is consulted by the executor.
- **Paper fleet 24 → 82 strategies** (`config/paper_fleet.json`, founder: "as many
  different, different times, different timeframes"): ORB at other session times (07:00
  and 08:30 pre-market, 10:00/10:30 second range, noon, 13:30, 15:00 power hour, 18:00 and
  21:00 Globex, 03:00 London, 04:00 EU — each with an eod/last-entry that keeps the session
  on one ET date), range lengths 1/2/30/60 min, entry/management rules (confirmation buffer,
  no-chase, trail-now/late, break-even variants, one-shot/five-shot, morning-only, cooldown,
  wide-stop/tight-trail, tight), and MTF scalper variants (HTF 5/30/60 min; flag-retest,
  sweep, onset, FVG, stochastic, all triggers; open/afternoon/Globex/London/all-day
  sessions; long-only/short-only; fixed 15/35 bracket; VWAP; long hold; no time stop;
  SMT on 30-min HTF). Every key is one the engines already parse; verified 82/82 active.
- **Feature builder + validation** (dashboard `ui/services/features.py`,
  `ui/services/data_quality.py`, `migrations/012_trade_context_features.sql`): per trade, a
  50-feature multi-timeframe vector (1/5/15/60-min bars: returns, realized vol, ATR,
  range position, EMA9−21, EMA slope, volume z, VWAP distance, plus session context)
  built ONLY from bars that closed before the entry — the builder asserts the invariant —
  stored in `trade_context.features` with a validation verdict (NaN-heavy / out-of-range
  rows flagged, not used blindly). Data-quality report per trading day (RTH coverage,
  gaps, out-of-order, duplicates, bad prints, spikes, ES parity, feed staleness); days
  under 80% coverage are excluded from the day-level models. Feature selection
  (`FeatureSelector` in models/base.py): NaN/constant drop, |corr|>0.95 pruning,
  permutation importance on a time-ordered validation tail — fitted on the training
  slice only, applied consistently at predict time — wired into the trade filter,
  adaptive risk, breakout, exit, fill-quality and day-type models.
- **Account-list verification after ORDER_PLANT login** (tid=302 `RequestAccountList` →
  303, new messages in `proto/rithmic.proto`; `ResponseLogin` gains `fcm_id`/`ib_id`).
  The executor adopts the fcm/ib Rithmic reports for the configured `account_id` and
  refuses to proceed (300s backoff, audit row) when that account is not in the login's
  list — which is exactly what `1088 user has no permission to this account` meant on
  2026-09-21: the Tradeify configs carried the Rithmic *username* (`RTU…`) as
  `account_id`; the tradeable account is the `RTG…` id the list returns.

### Changed
- `config/tradeify1_config.json`: `trade_contract` MNQM6 → MNQZ6 (June contract had
  expired; MD subscription failed `rp_code=7`, no ticks).
- `config/tradeify_config.json` + `config/tradeify1_config.json` (+ the `.env`
  `RITHMIC_ENV_TRADEIFY_ORDER_ACCOUNT` / `RITHMIC_TRADEIFY_ACCOUNT` aliases):
  `account_id` RTU989361488 → **RTG25785042011**, the account Rithmic's account list
  returns for this login (RTU… is the username). Verified live: order-update
  subscription now `rp_code=0`. Both labels are the SAME prop account — run only one;
  `tradeify` carries the 25K Growth limits (-500 / 1000), `tradeify1` the template's.

### Removed
- **`tradeify1` account label retired** (founder: "remove tradeify1"). It was a
  dashboard-created duplicate of the same Rithmic account as `tradeify` with the template's
  loose limits ($50k / -$1000 / $2000 vs the real 25K Growth -$500 / $1000). Config moved to
  `config/archived/tradeify1_config.json` (the dashboard ignores `archived/`), its env file
  and backup moved alongside (mode 600), its `live_position`/`live_sessions` rows deleted;
  it never closed a trade.

- **Paper-engine replay / audit mode** (`src/paper/paper_main.cpp`, `paper_db.*`; founder:
  "audit, validate and test to see all strategies are live taking trades, bad ones too"):
  `paper_engine --replay-from "YYYY-MM-DD HH:MM" --replay-to … [--account-label audit]`
  runs the whole fleet over RECORDED ticks with the engine clock driven by tick timestamps
  (EOD, day rollover), no sleeps, no control polling, then exits. `PaperDb::set_trades_only`
  isolates it: only `paper_trades` (label `audit`) is written, no strategy/position/daily/
  account rows, and every `load_*` returns empty so a replay never inherits the live
  fleet's day counts or positions. Dashboard: `POST /api/cpp/paper/replay {from,to}`,
  `GET /api/cpp/paper/audit`, "Replay audit" section on /strategies with per-strategy
  results and a stated reason for every silent strategy. Learning queries exclude the
  `audit` label. **Incident worth the line**: the first replay (before isolation) overwrote
  the live fleet's `paper_strategies.account_label` and doubled today's `paper_daily`
  counts, because those tables are keyed by `strategy_id` alone — a SECOND fleet label on
  this schema collides with the first. Repaired from `paper_trades`; the isolation flag is
  the fix, the shared key is the standing caveat.

### Operational notes
- **Collector + executor cannot share the Tradeify login.** With the `tradeify` executor
  live (MD + ORDER_PLANT), starting `build/rithmic_engine` (reads `RITHMIC_AMP_*`, aliased
  to the same Tradeify credentials) made Rithmic force-logout the executor's MD (tid=77)
  every ~35s. Collector stopped; it needs its own MD login before the chart's intraday
  ticks and the paper fleet can run alongside a live Tradeify executor.
- Local launches (`~/Desktop/bot` backend, `CPP_LOCAL=1`) used to `.`-source `.env` /
  `.env.<account>`; an unquoted password containing `<` was truncated silently and the
  executor looped on `rp_code=13` for hours. Values in both files are now quoted and the
  backend loads them literally (systemd `EnvironmentFile=` semantics).

---

## 2026-09-20 — Paper per-trade MAE/MFE (excursion tracking)

### Added
- **MAE/MFE per paper trade** (`src/paper/paper_broker.hpp`, `paper_bracket_broker.hpp`):
  both brokers track max adverse/favorable excursion (points) per leg on every tick
  (exit tick folded in) and persist `mae_pts`/`mfe_pts` on the `paper_trades` row.
  Columns added via idempotent ALTERs in `PaperDb::ensure_schema()` +
  `migrations/010_paper_mae_mfe.sql`.

---

## 2026-09-20 — Paper fleet manual control channel (paper_control)

### Added
- **DB-backed manual control channel** for the paper fleet: new `paper_control`
  table (`migrations/009_paper_control.sql`, also ensured by `PaperDb::ensure_schema()`)
  with `(id, strategy_id, action, created_at, consumed_at)` and a pending-row index.
  The dashboard INSERTs a row; the engine polls unconsumed rows once per second
  (in-order by id), applies the action, and stamps `consumed_at`. Actions: `disable`
  → `halt_trading("manual_disable")`; `enable` → `unhalt_trading("manual_enable")`,
  refused with a WARN when the strategy's RiskManager is halted (halt left in place);
  `flatten` → broker `flatten("manual", ...)` at the last known price (safe no-op when
  flat). Unknown strategy ids and unknown actions are WARN-logged and consumed so a
  bad row is never re-applied. Poll errors rate-limit to one WARN per error-state entry,
  same as `poll_ticks`. Works with the market closed — the main loop spins on zero ticks.

### Changed
- **Startup enabled-merge**: `paper_strategies.enabled` is now operator-owned — the
  config seeds it on first insert but `upsert_strategy` no longer overwrites it on
  conflict. A config-enabled strategy whose DB row says `enabled=false` is still built
  at startup but immediately halted (`manual_disabled`) so a dashboard disable survives
  engine restarts. Config-disabled strategies keep the existing skip behavior.

---

## 2026-09-20 — ES reference feed (SMT live) + fleet feed-symbol starvation fix

### Fixed
- **Paper fleet consumed zero ticks**: `paper_fleet.json` polled `symbol='MNQ'` while the
  collector writes `symbol='NQ'` (front-month feed). Every strategy had been silently
  starved since the fleet launched — hidden by the weekend close. New explicit
  `feed_symbol` fleet field (default = `symbol`): trade the micro label (MNQ P&L maths)
  on the NQ feed, same price series. Verified live: 24/24 strategies now consume ticks.
- **`poll_ticks` WARN spam**: a missing `ticks` relation logged one WARN per 100ms poll
  (thousands of lines during the test_db window). Now logs once per error-state entry.

### Added
- **ES reference feed for the MTF SMT/intermarket module** (Pine v5 §1.8, previously
  inert): `RITHMIC_EXTRA_SYMBOLS` (comma-separated) makes the collector subscribe extra
  symbols LAST_TRADE|BBO alongside the primary (no depth); `.env` sets `ES`. The paper
  engine polls `reference_symbol`, aggregates ticks into 1m bars (minute-rollover, same
  convention as the strategy's own bars) and fans them out via
  `MtfScalperStrategy::on_reference_bar` to strategies that wired a reference feed
  (`wants_reference_feed()`). New fleet variant `mtf_smt_v5` (trigger_mode `smt`,
  `use_smt_entry`, reference ES, positive expected correlation) — fleet now 24.
- 4 new MTF tests: SMT bull divergence fires long, SMT inert without feed, correlation
  gate blocks uncorrelated reference, gate passes correlated/up-trending reference
  (37/37 green). End-to-end smoke over the weekend: synthetic NQ+ES ticks injected into
  `ticks` (source='synthetic', deleted after) → collector subscribed ES, engine logged
  `Reference feed wired (ES) — SMT machinery live`.

---

## 2026-09-19 — Paper fleet engine + MTF scalper port + collector heartbeat fix

### Added
- **Paper fleet** (`src/paper/`): local paper-trading engine running 20+ strategies
  (ORB variants + MTF scalpers) on a single account, fed by the PG tick stream — no
  per-strategy Rithmic sessions. Tables `paper_strategies/paper_positions/paper_trades/
  paper_daily/paper_account` (`migrations/007_paper_fleet.sql`), NOTIFY channel
  `paper_update`, config `config/paper_fleet.json`. Two broker modes: ORB-style exits
  (`paper_broker.hpp`) and strategy-driven brackets (`paper_bracket_broker.hpp`).
- **MTF Scalper** (`src/execution/mtf_scalper_strategy.hpp` + config): C++ port of the
  Pine v6 strategy, 33 unit tests (`tests/execution/test_mtf_scalper.cpp`). SMT filter
  disabled (no DXY feed locally). `auto_mode_flag_fix` defaults to intended semantics.

### Fixed
- **Collector weekend death spiral** (`src/client.cpp`): `receive_loop` no longer cancels
  the Beast read on heartbeat timeout; a sibling `link_watchdog` coroutine owns all
  heartbeat sends and closes the socket only after 2.5× heartbeat-interval silence.
  `LoginError` carries `rp_code` — 13 retries every 300s, other codes terminal.
- **Paper engine correctness**: ORB strategies now receive `notify_trade_filled` (was
  capped at 1 trade/day); bracket broker clears stale phantom exits; reversal flips
  supported; MTF halt resets at day rollover; restart-resume flattens at entry instead
  of feeding stale state; `MtfScalperStrategy::seed_state` added; daily-loss double
  count and `init_bracket` ratchet fixed.
- **`test_db` destroyed live data**: the suite created `ticks_test` then renamed it over
  the production `ticks` table and dropped `ticks`/`audit_log` on teardown. All tests now
  run inside an isolated `rithmic_test` schema via `options='-c search_path=rithmic_test'`
  on the connstr, recreated fresh at start and dropped at exit; catalog queries pinned to
  `schemaname/table_schema = 'rithmic_test'`. Verified: production row counts identical
  before/after a full run with the live collector writing.
- **Account mismatch**: `RITHMIC_TRADEIFY_ACCOUNT` and `RITHMIC_ENV_TRADEIFY_ORDER_ACCOUNT`
  in `.env` pointed at `RTSL25815692164`; aligned to the real Tradeify 25K account
  `RTU989361488` (matches `config/tradeify_config.json`).

---

## 2026-09-18 — Config/schema/doc drift fixes + local mode docs

### Changed
- **live_sessions primary key**: promoted from `(session_date, instrument, strategy)` to
  `(session_date, account_label, instrument, strategy)` locally, reusing the existing
  `live_sessions_acct_inst_strat_idx` unique index (`ADD PRIMARY KEY USING INDEX` — no table
  rewrite, brief lock). Two accounts trading the same instrument on the same day no longer
  collide. Migration `migrations/006_live_sessions_account_pk.sql` (idempotent `DO $$` guard)
  added for Oracle — **not run anywhere but local**.
- **Trade routes**: `trade_route` set to `"simulator"` in `config/MNQ_config.json`,
  `config/MES_config.json`, `config/MYM_config.json` (was `"Rithmic Order Routing"`, the route
  that silently cancels orders). `config/live_config.json` untouched (already `"simulator"`, frozen).
- **`.env.tradeify1`**: added `RITHMIC_TRADEIFY1_SYSTEM="Tradeify"` and
  `RITHMIC_TRADEIFY1_URL="wss://rprotocol-mobile.rithmic.com:443"` — executor was falling back
  to system `LegendsTrading` + wrong URL.
- **`audit_daemon`** (`src/audit_daemon_main.cpp`):
  - `write_metrics` INSERT fixed to match the real schema: `quality_metrics(metric, value,
    labels_json, ts)` (was nonexistent `labels`/`recorded_at` columns).
  - Config checks now honor `AUDIT_CONFIG_PATH` (default `config/live_config.json` preserved).
  - Data-freshness check queries `AUDIT_TICK_SYMBOL` (default `NQ`, matching the collector's
    `RITHMIC_SYMBOL`) instead of hardcoded `MNQ`.
- **Tradeify 25K config** (`config/tradeify_config.json`): `starting_balance` 25000,
  `trailing_drawdown_cap` 1000, `daily_loss_limit` -500, `trade_contract` `MNQZ6`,
  `trade_route` `"simulator"`, `order_env_prefix` `RITHMIC_TRADEIFY`.

### Removed
- **Legacy tables dropped locally**: `nq_trades`, `nq_session`, `nq_position` — all 0 rows,
  superseded by `live_trades` / `live_sessions` / `live_position`; nothing wrote to them.

### Docs
- **CLAUDE.md**: new "Local mode" section — dashboard `:3000`, backend `:8080` with
  `CPP_LOCAL=1` spawning collector + executor as `nohup` subprocesses; `deploy/*.service`
  are Oracle-only; warning to kill local executors before Oracle failback (no cross-host
  single-writer guard yet).
- **RUNBOOK.md**: per-account configs throughout — `nq_executor@tradeify` /
  `nq_executor-24x7@tradeify` (was nonexistent `nq_executor@RTH` / `nq_executor-24x7@default`);
  dry_run edits now target `config/tradeify_config.json`, with a note that frozen
  `config/live_config.json`'s `dry_run` is not read by any binary.
- **DATA.md**: `ticks.source` is no longer "Always `amp_rithmic`" — provider is Tradeify now.
- **scripts** (`hermes_lifecycle.sh`, `agents/deploy_manager.sh`, `provision_oracle.sh`):
  default Oracle service `nq_executor@RTH` → `nq_executor@tradeify`; provisioning checklist
  points at `config/tradeify_config.json`.

### Known issues
- `config/tradeify_config.json` and `config/tradeify1_config.json` share
  `account_id` `RTU989361488` — two configs point at one Rithmic account. Left unchanged
  pending a second account; do not run both executors simultaneously.

---

## 2026-05-09 — Hermes full lifecycle + multi-agent fleet

### Added
- **Hermes lifecycle** (`scripts/hermes_lifecycle.sh`): time-aware CI loop with 4 phases keyed to
  Eastern market hours — PRE_MARKET (fleet + deploy), PRE_SESSION (fast + Oracle health),
  SESSION (fast every 30 min, no deploy), POST_SESSION (fleet + session post-mortem + push),
  OVERNIGHT (fast every 90 min). Makefile targets: `hermes-lifecycle`, `hermes-lifecycle-once`,
  `hermes-lifecycle-local`.
- **`doc-syncer` fleet agent**: 5th specialist agent that audits all docs (CLAUDE.md, CHANGES.md,
  DATA.md, RUNBOOK.md) and Obsidian decisions against current code, fixes stale content, and
  creates missing decision notes automatically on every fleet run.
- **Obsidian vault** (`~/obsidian/rithmic/`): full vault setup with `.obsidian/` config (core
  plugins, daily notes, graph colors), templates (daily, session, decision), pre-seeded decision
  notes (simulator route, trail_be_offset, cancel+resubmit), and daily digest auto-generation.
- **`scripts/obsidian_daily.sh`**: generates/updates daily digest note aggregating all hermes runs
  and open audit warnings for the day.
- **`scripts/obsidian_session.sh`**: pulls trade data from PostgreSQL and writes a session
  post-mortem note. Accepts optional `DATE=YYYY-MM-DD` argument.
- **Makefile targets**: `hermes-note`, `hermes-fleet`, `hermes-fleet-fast`, `hermes-lifecycle`,
  `hermes-lifecycle-once`, `hermes-lifecycle-local`, `obsidian-daily`, `obsidian-session`.
- **Weekly digest**: lifecycle generates `daily/week-YYYY-WW.md` automatically on Fridays
  post-session with hermes health stats and session P&L summary.

### Removed
- ncurses dashboard (`src/dashboard.cpp`, `find_package(Curses REQUIRED)`) — UI lives in
  `bot/frontend` (Next.js). Removing also drops ncurses as a hard build dependency on Oracle.

---

## 2026-05-09 — Hermes codebase cleanup + doc refresh

### Removed
- **Dead callback machinery**: `OrderModifyCallback`, `modify_cb_`, `set_modify_callback()`,
  and `send_modify_order()` — the modify path was registered but never invoked; the executor
  uses cancel+resubmit instead. Removed from `order_manager.hpp`, `executor_main.cpp`, and both test fixtures.
- **Unused variable**: `constexpr int OFFSET = 20` in `executor_main.cpp` (shadowed by `UNWIND_OFFSET_TICKS`).
- **15 stale files**: old audit fragments (`docs/audit_gap_T{1,2,3}.md`, `AUDIT_GAP_REPORT.md`,
  `audit_gap_coordinator.md`), superseded deploy files (`deploy/nq_executor.service`, `deploy/update.sh`),
  archived configs (`config/legends_config.json`, `config/archived/tradeify_config.json`),
  orphaned scripts (`backup_pg.sh`, `cpp_build_check.sh`, `migrate_account_label.sql`, `validate-remote.sh`),
  stale docs (`docs/architecture.html`), untracked artefacts (`ticks.wal`, `rithmic.duckdb`, two `.docx` playbooks).

### Docs
- **RUNBOOK.md**: Full rewrite — Python-era workflow replaced with C++ operator procedures.
- **DATA.md**: Fixed unique index definition (5-column, not 3); removed Python psycopg2 example; updated file layout.
- **CLAUDE.md**: Fixed key files table (removed deleted `nq_executor.service`, added `nq_executor_24x7@.service`); removed "never push mid-session" rule.
- **engine_architecture.html**: Corrected audit check count (20 → 17 everywhere).

---

## 2026-05-04 — Hermes iteration 36 — dry-run full ORB cycle verified + FORCED_LOGOUT root cause fixed

### Fixed
- **Oracle session conflict**: Production `nq_executor` on Oracle VM was holding an
  active TICKER_PLANT session for `PAPA121797704102` (LegendsTrading), causing
  immediate FORCED_LOGOUT on every local dry-run connection. Stopping Oracle service
  releases the Rithmic session; local executor connects cleanly.
- **`src/execution/executor_main.cpp`**: Added 3-second ASIO-awaitable cooldown after
  FORCED_LOGOUT (tid=77) before reconnecting, preventing rapid reconnect storms from
  hammering the Rithmic server.
- **`src/execution/executor_main.cpp`**: Removed system-info probe from the MD reconnect
  loop — probe is only needed at initial startup. Probe→close→reconnect during each
  reconnect was adding unnecessary session churn.
- **`bot/frontend/src/app/live/page.tsx`**: Fixed `ReferenceError: cfg is not defined`
  — `cfg` state lives inside `EnvironmentCard`, not `LivePage`. Removed the
  `cfg?.dry_run` references from the Order status span; `op_connected=true` in dry-run
  (set by the C++ fix) is sufficient to show green "Connected".

### Added
- **`src/execution/executor_main.cpp`**: Executor now shuts down cleanly when the daily
  trade limit is reached and position is flat. After the last trade closes, logs
  `"Daily trade limit reached (N/N) — shutting down"`, flushes audit, sets
  `g_running=false` and stops the io_context. Previously the process stayed alive until
  EOD flatten time even when no further trades could be taken.

### Verified
- Full ORB dry-run cycle end-to-end: MD connected → 2-min range built (27861–27877)
  → SHORT breakout at 27860.75 → BE+offset trail → stop exit → DB write → 3 trades
  completed, +$2.00 daily P&L, daily limit respected, executor self-terminated.

---

## 2026-05-04 — Hermes iteration 35 — MD connection architecture restore + live page fix

### Added
- **`scripts/use-env.sh`**: Bash replacement for deleted `use_env.py`. Reads
  `RITHMIC_ENV_{NAME}_{ORDER,MD}_*` blocks and writes active aliases used by the
  C++ executor: `RITHMIC_LEGENDS_*` → ORDER_PLANT, `RITHMIC_AMP_*` → TICKER_PLANT.
  Applies per-env JSON config overrides via `jq`. No Python — pure bash.

### Fixed
- **`src/execution/orb_config.hpp`**: Reverted MD credential reading back to
  `RITHMIC_AMP_*` (original design). The `RITHMIC_MD_PROVIDER` dynamic lookup
  added in commit 7314556 broke compatibility with the env switcher pattern.
  Legends/Tradeify use WebSocket; AMP uses C++ R|API+ SDK.
- **`src/execution/executor_main.cpp`**: Executor now creates the `audit_log` table
  on startup via its own `audit_conn` (previously the table was only created by the
  `rithmic_engine` tick collector schema, not by `nq_executor`). Eliminates
  `relation "audit_log" does not exist` error flood during audit flush.
- **`src/execution/executor_main.cpp`**: MD plant system info probe now logs all
  available Rithmic system names and warns if the configured system is not in the
  list — aids diagnosis of login failures.
- **`src/execution/executor_main.cpp`**: `PQexec()` calls for audit schema creation
  now check result status and log errors instead of silently discarding failures.

### Infrastructure
- **`bot/.env`**: Set `CPP_PG_HOST=127.0.0.1 CPP_PG_PORT=5432` so the live page
  WebSocket (`/ws/cpp`) reads from local PostgreSQL during dry-run testing. The
  executor dry-run writes to local PG; the frontend now sees live position + price.
- **`bot/.env`**: Set `PG_PORT=5432` so `live.py` REST endpoints also read from
  local PG instead of Oracle SSH tunnel during local development.
- **AMP out of credits workaround**: Configured Legends credentials in `RITHMIC_AMP_*`
  with `USE_RAPI_SDK=OFF` (WebSocket mode). In dry-run, ORDER_PLANT is not opened
  so no session conflict; Legends TICKER_PLANT connects successfully. For live trading
  with real orders, AMP credits must be restored (or Tradeify MD configured).

### Results
- Hermes: **7/7 PASS** (build + 4 unit tests + db test + audit_daemon)
- Dry-run verified: live page at localhost:3000/live shows `md_connected=true`,
  live MNQ price (27810+), session equity, ORB state — all updating in real-time.

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
