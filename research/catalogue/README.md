# Research catalogue — every test in the project (built 2026-10-19)

One place for everything tested on NQ in this repo. Four inventories, each a table with one row per test:

| file | covers | rows |
|---|---|---|
| `ledger_and_preregs.md` | research/README.md ledger + all 118 files in research/preregistered/ (2026-10-16 → 10-19) | 218 tests |
| `edge_lab_ideas.md` | every edge_lab idea spec (research/ideas/), its report, and the research DB (research_ideas / research_runs) | 462 ideas, 801+ runs |
| `history_before_edge_lab.md` | the paper fleet, payout / prop simulators, walk-forward, the 2026-10-11 intraday search (ledger `research_tests`), holds, crypto, order flow, docs/, CHANGES.md | 172 studies |
| `starnet_desk.md` | the StarNet desk's own proposals, judgements and lessons since it was relaunched as an edge desk (2026-10-16) | 10 ideas, 66 variants |

## How trustworthy the record is (checked 2026-10-19)
- **Pipeline self-test** (`scripts/edge_lab_validate.sh`): 76 / 76 checks as expected — planted edges found at 8 pts, missed at 0; coin-flip
  rules fail on real and synthetic data; look-ahead traps make no trades; buying every day fails at controls; every one of the 46 PASS
  ideas re-runs to the same verdict and pts/trade. Smallest edge the gates can see ≈ 5 pts/trade.
- **Reproducibility** (`scripts/research_repro.sh`): 26 / 26 one-off programs re-run byte-identical, including the five 2026-10-19 programs,
  now registered with their inputs saved in `research/programs/inputs_20261019/`. The Vest operation simulator re-run from those inputs
  gives the reported +$16,139 / yr (5 of 82 windows losing) exactly.
- **DB = reports**: every research_ideas row matches its report file (verdict and time). The 26 specs of 10-18 / 10-19 that had been run
  outside the loader were re-run and loaded on 2026-10-19 (462 ideas).

## What survived (the only things that pass every gate and hold up later)
| edge | form | evidence (pts/trade NQ unless noted) | where it is |
|---|---|---|---|
| NY-open direction on a straight path | book F: cascade (09:50 > 0.25 ATR, re-checks 10:30/11:00/11:30) + gap retest + 10:00 gap rule, 0.35 ATR stop, 2R, side by side | +0.15 / +15.05 / +13.86 pts/session (2010–18 / 2019–24 / 2025–26) at 2 pt; 10:00 gap rule +28.3 2019–24, +18.4 holdout | the plan (Vest), paper |
| Buying weakness when the market is afraid | gap-down weak-close long 09:30 → 15:55 (1.5 ATR stop); overnight hold after a weak close + VIX ≥ 20 (O3); the full weakness × VIX book (PF 1.4–2.4, 14–15 / 17 years, ES confirms) | +23 / +24 / +78 (gap-down long); +5 / +44 / +69 (O3) | the plan (Vest), dry executor `tradeify_day` / `tradeify_book` |
| Size rules on those | VIX-fell cascade shorts earn more (NQ and ES, every era); Friday is the book's best day 2010–24 | size rules, small effects | VIX: not used on Vest; Friday 1.5× adopted with a drop condition |
| Prop-account mechanics | small funded risk, claim at +$500 keep $250, full-port evals, refill slots | 10-account operation +$10–16k / yr, 5–6 % losing 12-month windows, positive in both eras at today's cost-to-range | `2026-10-19_vest_funded.md` |

Everything the operation earns rests on these two edges. Both are ONE kind of bet each: "go with the day's straight move after the
open" and "buy fear". Neither is forecastable before it shows up — NQ's direction is not predictable at any session's open
(2026-10-19_session_model, behaviour per session).

## What did not survive (counts from the inventories)
- edge_lab base ideas: **46 PASS** of 426; failed at screen 100, walk-forward 74, controls 177, robustness 3. Controls (beating random direction,
  random time and random day) is where most ideas die: their profit is the day's move, not the pattern.
- Families with **zero** lasting survivors: price action / SMC / candle patterns / Wyckoff (18 tests), EMA rules (~15 looks), overnight session
  direction (8 studies), mean reversion / VWAP fades / level fades, round numbers, news-day rules, calendar effects, NQ–ES lead,
  order-flow / book-imbalance rules (2023+ only), ML models of direction (session and intraday), fixed small targets, break-even / trailing
  / runner exits, crypto.
- Behaviour that is real but NOT tradeable on its own: session size is forecastable (corr +0.42–0.52) but R per trade does not depend on it;
  ~83 % of breaks of the prior high / low come back within 30 min but fading them loses; small gaps fill ~75 % but the fade failed its holdout;
  NQ drifts up (week low Monday, high Friday) — already in the longs.

## Corrected or superseded (do not quote the old numbers)
- 2026-10-11 payout-sim stack figures (per-unit MAE) → invalid; as one position the stack breaches a $2K trail ~12×/yr.
- Select 50K plan with a 36-pt stop → does not work at today's NQ price (V6).
- First Flex payout ≈ 17 weeks, not 35–41 days (real trades and costs).
- Book ablation (one-trade book) → superseded the same day by book F.
- "Validated Asia cascade" (prop_daily_stop) → not validated (negative holdout).
- 10-19 room-based sizing 99.9 % pass → simulator artefact (no size floor); real result ≈ flat sizing.
- 10-19 ES operation (first run) → wrong ATR scale; re-run: ES weaker, not added.
- `gap_fade_m2` shows PASS in the DB / dashboard but FAILED its one-look holdout (−7.91 on 70) → not usable.
- `so_cascade_dev__meta` PASS (10-16) is stale: its parent `so_cascade__meta` failed at shuffled labels on 10-17.
- `so_cascade` (PASS) and `cond_trend_none` are the same rules; the stricter control run says FAIL at CONTROLS — read so_cascade's pass as borderline.

## Open problems (known, not fixed)
1. **Holdout overlap**: 238 of 416 spec-backed reports (23 of the PASSes, incl. so_cascade, strong_open, weak_day_long, cascade_gap_agree)
   were judged on data that runs into 2025–26 (no `to` field). Their "2025+" checks are not clean; only future sessions are.
2. **Several PASSes have walk-forward t < 2**; the PASS list is dominated by three families (09:50 cascade, gap-or-retest, weak-day).
3. **Data**: NQ has no data 2026-03-20 → 09-18 and no RTH bars on quarterly expiry Fridays (Databento NQ.c.0 calendar roll).
4. **Vest's real costs** (fee, spread, funding) are unknown; the operation's break-even is ≈ 15–20 NQ pts per round trip.
5. **Paper**: pd_gap_1000 / pd_mom_1000 are in the paper config but the paper engine was not restarted to run them; the exact Vest plan
   is not yet forward-tested on paper (needs the founder's go-ahead for an engine change).
6. **Desk**: of the four StarNet research lanes, RESEARCHER and FLOWLAB wrote nothing since the 2026-10-18 resume (short runs — likely
   their own skip check); 10 desk ideas judged, none passed. Two untested desk ideas wait: levg_etf_close_pret, macro_shock_follow.
7. **Process**: two older studies used Python outside the repo (fleet_consensus, zoo_study) against the project rule; zoo_study never finished.
8. `atr.csv` used by most 10-18 / 10-19 Vest sims is edge_lab's ATR lagged one day (mean ratio 0.981); the plan's numbers were re-checked on
   the exact ATR and do not change.

## Method lessons (from what went wrong)
- A result counts only after a one-look holdout or future data; three ideas passed every gate and failed there (gap_fade, flags, London fade).
- Profit that comes from the day's move shows up as "edge" until it is compared with random entries on the same days — always run controls.
- Simulators lie in specific ways: overlapping positions (fake edges), per-unit drawdown (fake payouts), shrinking size without a floor
  (accounts that never fail), stale or mismatched ATR scales (wrong size). Check each before trusting a sim.
- Small fixed costs in points were 7× heavier relative to the range in 2010–18 than today; compare eras at the same cost-to-range ratio.
