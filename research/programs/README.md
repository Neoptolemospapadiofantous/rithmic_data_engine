# research/programs — the one-off C++ programs behind the 2026-10-15 … 10-17 research

Every pre-registered test in `research/preregistered/` that did not go through `build/edge_lab` was run by one of these
programs. They were written in a session scratchpad and copied here on 2026-10-17 so every result can be re-run.
`out/` holds the exact output each one printed (the numbers quoted in the pre-registration files).

Build and run from the repo root (they read `data/edge_lab/cache` — export it with `scripts/edge_lab.sh export` — and
`data/regime` for VIX; read-only, no Postgres writes):

    g++ -O2 -std=c++20 -Isrc -Iresearch/programs research/programs/<name>.cpp -o /tmp/<name> && /tmp/<name>

Inputs that are not in the repo:
- `fleetq` / `fleetq2` read a CSV export of every `nq_hist` fleet trade (570 MB, not kept):
  `\copy (select strategy_id, extract(epoch from entry_time at time zone 'America/New_York')::bigint, extract(epoch from exit_time at time zone 'America/New_York')::bigint, case when direction in ('LONG','long','BUY','buy','B') then 1 else -1 end, pnl_pts, mfe_pts, mae_pts from hist_trades where account_label='nq_hist') to 'fleet_trades.csv' csv`
- `perm` reads `out/mtf_ldn.csv` (mtf_london_v5 trades, exported from `paper_trades`).
- `ho/` = full-range (holdout-inclusive) copies of edge_lab idea specs used by `t145` and friends.
Never run a replay or rebuild `build/` 09:25–16:05 ET on a weekday (these programs only read files, so they are safe).

| Program | Pre-registration / topic | What it does (first line) |
|---|---|---|
| `behavior2` | — | 2026-10-17 behaviour map, part 2: intraday structure, volatility, cross-market, news days (real BLS / FOMC dates). |
| `behavior` | — | 2026-10-17 "how does NQ behave": a descriptive map of NQ 1-minute bars 2010-07 → 2026-10. |
| `session_model` | research/preregistered/2026-10-19_session_model.md | 2026-10-19 session model: does what is known at a session's open predict the Asia / London / NY session's direction, size and trend? |
| `intraday_models` | research/preregistered/2026-10-19_intraday_models.md | 2026-10-19 intraday models: A) predict the NY RTH range at 09:30; B) at 09:50 / 10:30 predict the move to 15:55 and compare with "go with the move". |
| `behavior3` | research/preregistered/2026-10-19_behavior_part3.md | 2026-10-19 behaviour part 3 + order flow: pullback depth, gap fills, sweeps of yesterday's high/low, round numbers, afternoon, early volume, aggressor delta / divergence / absorption / book imbalance (2023+). |
| `target_by_range` | research/preregistered/2026-10-19_attack_behaviour.md | 2026-10-19: book trades re-simulated on 1m bars with targets by the predicted NQ range (1.5 / 2 / 3R) vs 2R everywhere. |
| `behavior_sessions` | research/preregistered/2026-10-19_behavior_per_session.md | 2026-10-19: every behaviour test (VWAP, straightness, pullbacks, prior-session levels, round numbers, second half, RVOL, order flow) for Asia, London and NY separately. |
| `ema_cross_study` | research/preregistered/2026-10-19_ema_9_14_cross_study.md | 2026-10-19: what NQ does after an EMA 9/14 cross on the 1 / 5 / 15-min chart, by session and context. |
| `mtf_entry_study` | research/preregistered/2026-10-19_ema_9_14_cross_study.md | 2026-10-19: 5-min setups with 1-min entries (market / midpoint limit / pullback limit / dip-then-resume), stop beyond the 5-min bar, 2R, 60 min; argv 0 / 1 / 2 = 1 pt / no cost / scaled cost. |
| `awk_20261019/` | research/preregistered/2026-10-19_vest_funded.md and the other 2026-10-19 studies | the Vest eval / funded / portfolio simulators and the session-behaviour scripts (awk), with their saved outputs in out/ |
| `book3` | — | #include "research/meta.hpp" |
| `both` | research/preregistered/2026-10-17_more_trades.md | 2026-10-17 pre-registered A (research/preregistered/2026-10-17_more_trades.md): take both the cascade and the gap retest |
| `calm` | — | Calm-day sizing: the cascade's walk-forward trades at 2 units when the prior VIX close < 20, else 1 — vs flat 1 unit. |
| `combo` | — | 2026-10-17 "do all and test all": the working pieces as ONE daily plan, on the real 2019–26 sequence. |
| `combo_es` | — | 2026-10-17 "we need to do better": NQ and ES together on one Flex 50K account. Real 2019–26 walk-forward trades. |
| `daily` | — | 2026-10-17 "a trade every day": (1) the cascade's trades by check slot (09:50 / 10:30 / 11:00 / 11:30 / 12:00) per era,  |
| `dbg` | — | #include "../../../../../home/theone/Desktop/rithmic_engine/src/research/meta.hpp" |
| `emaexit` | research/preregistered/2026-10-17_ema_entries_trails.md | 2026-10-17 EMA-14 hand trade: entries × stops × trailing exits (research/preregistered/2026-10-17_ema_entries_trails.m |
| `emamom` | research/preregistered/2026-10-17_ema_sweep.md | 2026-10-17 EMA by hand: timeframe × length × pullback × trend × momentum × exit (research/preregistered/2026-10-17_ |
| `emamtf` | research/preregistered/2026-10-17_ema_sweep.md | 2026-10-17 EMA by hand: timeframe × length × pullback × trend × momentum × exit (research/preregistered/2026-10-17_ |
| `emastop` | research/preregistered/2026-10-17_ema_stops.md | 2026-10-17 EMA-14 hand trade with structural stops (research/preregistered/2026-10-17_ema_stops.md). |
| `emasweep` | research/preregistered/2026-10-17_ema_sweep.md | 2026-10-17 EMA by hand: timeframe × length × pullback × trend × momentum × exit (research/preregistered/2026-10-17_ |
| `evalspeed` | research/preregistered/2026-10-17_reentry.md | 2026-10-17 re-entries in the day's direction (research/preregistered/2026-10-17_reentry.md). |
| `fast25` | — | 2026-10-17 founder: "1-3 trades a day ... 400-500 ... on a 25k if I take 3 winning days I can reach 1500 then daily payo |
| `fast` | — | Fastest first payout: plan × evaluation size × funded size, 10,000 synthetic years (real 2019–26 months resampled), |
| `fast_seq15` | — | Fastest first payout: plan × evaluation size × funded size, 10,000 synthetic years (real 2019–26 months resampled), |
| `fast_seq` | — | Fastest first payout: plan × evaluation size × funded size, 10,000 synthetic years (real 2019–26 months resampled), |
| `fleetfam` | — | 2026-10-17: the fleet × tape-regime result (fleetreg.cpp) broken down by strategy family. Same method: the side (trend- |
| `fleetlock2` | — | 2026-10-17 fleet × tape regime × daily profit lock, all 2026 1-minute data we have (Jan, Mar, Sep, Oct). |
| `fleetlock` | — | 2026-10-17 fleet × tape regime × daily profit lock, all 2026 1-minute data we have (Jan, Mar, Sep, Oct). |
| `fleetq2` | — | 2026-10-17 follow-up to fleetq: "the entries are fine, the exits kill the edge" — tested the strict way |
| `fleetq` | — | 2026-10-17 "are the fleet's entries or its exits the problem?"  fleetq TRADES_CSV |
| `fleetreg` | — | 2026-10-17: the paper fleet with the tape regime ON, on all 2026 1-minute data we have (Jan, Mar, Sep, Oct — Feb not r |
| `gaponly` | — | 2026-10-16 confirm_gap test 5: base sizing vs 2x on gap-agree days. |
| `gaps` | — | #include "research/edge_lab.hpp" |
| `gapsize` | — | 2026-10-16 confirm_gap test 5: base sizing vs 2x on gap-agree days. |
| `gh` | — | 2026-10-17 tests G and H on the adopted 25K plan: NQ cascade + gap retest, both when they agree, 2 micros per trade, the |
| `integrity` | — | Data integrity of the edge_lab cache: per symbol — trading days, full RTH sessions, the 09:30 bar present year-round |
| `lock` | research/preregistered/2026-10-17_daily_lock.md | 2026-10-17 daily profit lock (research/preregistered/2026-10-17_daily_lock.md): close and stop for the day once the day' |
| `mc` | — | Monte Carlo of the prop plan: 10,000 synthetic years built from real calendar months (2019–26, sampled with |
| `mix` | — | 2026-10-16 round 3 tests 5 + 6: account mix (paid months / withdrawn / evals) and worst case at 8 MNQ. |
| `monthly` | — | How many staggered accounts for ≥ 1 payout every calendar month? cascade walk-forward trades, base vs VWAP+stall exit. |
| `months` | — | #include "research/meta.hpp" |
| `news10` | research/preregistered/2026-10-17_test_f_news10.md | 2026-10-17 test F (research/preregistered/2026-10-17_test_f_news10.md): 10:00 ET releases (JOLTS, ISM manufacturing) and |
| `newscheck` | — | verify BLS release dates against NQ's own 08:30 volume spike |
| `nullgen` | — | Null market from real NQ 1m bars: inside every trading day the bars' moves (open→close step, high/low offsets, |
| `openreg` | research/preregistered/2026-10-17_open_regime_ema.md | 2026-10-17 opening regime → EMA-14 trades (research/preregistered/2026-10-17_open_regime_ema.md). |
| `planchk` | research/preregistered/2026-10-18_plan_checks.md | 2026-10-18 plan checks: stacking the setups, news days, quiet-market stand-aside — on the REAL ENGINE all-year trades (out/planchk_trades.csv, SQL in the header). |
| `pa1m` | — | 2026-10-17 "from the regime can we follow price action on the 1m": at every RTH minute (09:45–15:30) of NQ 2010-07 → |
| `parity` | — | edge_lab's cascade trades (paper so_cascade_2r's exact rules) on the recorded sessions, for a trade-by-trade parity |
| `payfast` | research/preregistered/2026-10-17_reentry.md | 2026-10-17 re-entries in the day's direction (research/preregistered/2026-10-17_reentry.md). |
| `perm` | research/preregistered/2026-10-17_ema_permission.md | 2026-10-17 direction + permission for the founder's hand trades (research/preregistered/2026-10-17_ema_permission.md, ex |
| `reentry` | research/preregistered/2026-10-17_reentry.md | 2026-10-17 re-entries in the day's direction (research/preregistered/2026-10-17_reentry.md). |
| `reentry_pay` | research/preregistered/2026-10-17_reentry.md | 2026-10-17 re-entries in the day's direction (research/preregistered/2026-10-17_reentry.md). |
| `regcal` | — | 2026-10-17 calibration for the live regime chart (bot /chart overlay). |
| `regime2` | — | 2026-10-17 regime measurement, per session, with a validated instrument. |
| `regime` | — | 2026-10-17 "can we see if it is trending, mean-reverting or choppy — accurately?" |
| `regime_synth` | — | Synthetic NQ with PLANTED regimes, to prove the regime measurement can see regimes when they exist. |
| `regopt` | research/preregistered/2026-10-17_regime_optimize.md | 2026-10-17 regime optimisation, walk-forward (research/preregistered/2026-10-17_regime_optimize.md). |
| `sens` | — | #include "research/meta.hpp" |
| `speed` | — | 2026-10-17 "payouts need to be faster": where the time to the first payout goes, and which levers shorten it. |
| `speed_mon` | — | 2026-10-17 "payouts need to be faster": where the time to the first payout goes, and which levers shorten it. |
| `sprint2` | — | 2026-10-17 one-week payout sprint search (founder: "we want to make it happen", "test all", "find a way"). |
| `sprint3c` | — | 2026-10-17 one-week payout sprint search (founder: "we want to make it happen", "test all", "find a way"). |
| `sprint3` | — | 2026-10-17 one-week payout sprint search (founder: "we want to make it happen", "test all", "find a way"). |
| `sprint4` | research/preregistered/2026-10-17_manual_execution.md | 2026-10-17 manual-execution tests on the sprint (research/preregistered/2026-10-17_manual_execution.md). |
| `sprint` | — | 2026-10-17 one-week payout sprint search (founder: "we want to make it happen", "test all", "find a way"). |
| `t145` | research/preregistered/2026-10-17_day_engine_plan.md | 2026-10-17 T1, T4, T5 (research/preregistered/2026-10-17_day_engine_plan.md). |
| `t2` | — | #include "research/edge_lab.hpp" |
| `trail` | research/preregistered/2026-10-17_regime_trail.md | 2026-10-17 pre-registered: let winners run when the tape trends (research/preregistered/2026-10-17_regime_trail.md). |
| `volsize` | — | 2026-10-17 volatility sizing: do the cascade / weak-day / Monday trades earn more per point of risk on quiet or wild day |
