# Plan checks: stacking the setups, news days, a quiet-market stand-aside (pre-registered 2026-10-18, before any run)

Founder: "do all of them" (the 2026-10-17 list of further checks). Program: research/programs/planchk.cpp → out/planchk.txt.
Input: the REAL ENGINE's trades (paper_engine on the 188 NQ 1-minute files, label `mom_hist`, fleet costs: 3 ticks slippage,
1.82 commission, $ per 1 MNQ), exported to research/programs/out/planchk_trades.csv by the SQL in the program's header.
Strategies, all as frozen today:
- C = so_cascade_2r_nofomc (the dry `tradeify_cascade` executor's twin: 09:50 / 10:30 / 11:00 / 11:30 checks, stop 0.25 ATR, 2R)
- G = gap_or_retest (gap ≥ 0.1 ATR side, 15-min opening-range retest, stop 0.35 ATR, 2R)
- M = mom_dir_45_vwap_plan (45-min momentum + VWAP pullback, plan agrees, stop 0.25 ATR, BE +1R, hold)
- also reported, not decided on: mom_dir_45_vwap, mom_dir_45_dip, so_cascade_2r_vwap_stall.
Eras 2010-18 / 2019-22 / 2023-24. 2025-26 printed separately: NOT clean (the momentum family's holdout was looked at 2026-10-17).

## 1. Stacking (correlation between the setups)
- Daily P&L correlation C–G, C–M, G–M on days both traded; share of those days with the same direction.
- Policies, 1 MNQ per trade: C · C+G · C+G+M (every trade) · C+G+M ONE AT A TIME (a trade is skipped if it would start while
  another is still open). Per era: total $, worst day, 5th-percentile day, worst end-of-day drawdown, total / drawdown.
- 25K Lightning payout test (2019-01 → 2024-12, every trading day as a start): 2 MNQ per trade, the account reaches +$1,500
  before an end-of-day drawdown of $1,000 from its high (the trailing floor stops trailing at the start balance + $100), within
  40 trading days → "paid"; drawdown hit first → "blown"; else "neither".
- ADOPT stacking (C+G+M either form) over C+G only if total / drawdown is higher in ALL THREE eras AND the payout test's
  paid-minus-blown is higher. Same test for C+G over C alone.

## 2. News days
- $ per trade for each strategy on CPI, NFP, PPI (08:30 ET) and FOMC decision days (calendar table) vs all other days, per era.
- ADOPT a "skip <event> days" rule for a strategy only if its $ / trade on those days is below zero overall 2010-24 AND in at
  least two of the three eras AND n ≥ 30. Otherwise no change (the cascade already skips FOMC days).

## 3. Quiet-market stand-aside (the 2010-18 problem), walk-forward
- Known at the open: prior VIX close; ATR% = prior session's ATR-14 / its close.
- Rule: trade the day only if the filter ≥ a threshold. Grids: VIX 11, 12, …, 22; ATR% 0.6 %, 0.7 %, …, 2.0 %.
- Walk-forward by year 2013 → 2024: the threshold chosen for year Y is the one with the highest total $ over all years before Y
  (expanding window, ties → the LOWER threshold = trade more); "always trade" is a candidate (threshold off).
- Applied to the plan basket C+G+M ONE AT A TIME, and printed per strategy.
- ADOPT a filter only if its walk-forward total beats always-trading in 2013-18 AND overall 2013-24, and is not below it in
  either later era by more than 10 %. The VIX and ATR% filters are judged separately; if both pass, the one with the higher
  walk-forward total. Re-picking each year is how a chosen rule would actually be run — the in-sample best threshold is not.

## Results (2026-10-18; research/programs/planchk.cpp → out/planchk.txt, engine trades out/planchk_trades.csv, 10,535 trades)
1. STACKING — the setups are largely ONE bet: daily-$ correlation C–G +0.59 (75 % same direction), C–M +0.62 (98 %), G–M +0.72.
   By total / drawdown C+G beats C in every era (−0.48 / 9.09 / 5.70 vs −0.60 / 2.44 / 4.25), but on the 25K LIGHTNING test stacking
   is worse: C alone paid 8.4 % / blown 41.8 %; C+G 3.7 % / 56.4 %; C+G+M 1.6 % / 59.2 %; one-at-a-time ≈ 9–10 % / 52 %.
   PARKED 2026-10-18 (founder: "put aside for now") — not applied to the plan page or playbook; the 10-17 agree-only rule stands.
   VERDICT: no stacking adopted for Lightning. Extra (not pre-registered, matches the plan's agree-only rule): C+G agree 3.8 % / 52.7 %.
   Diagnostic: WITHOUT the 20 % best-day rule C+G agree pays 42 % in 14 days (C alone 26 % in 21) — the consistency rule is what
   decides it (stacked days are big days). This does not contradict 2026-10-17_more_trades (Select Daily / Flex 25K, no 20 % rule:
   stacking adopted there): stack on Select / Flex, take the FIRST setup only on Lightning.
2. NEWS DAYS — pass by the letter: cascade on CPI (−0.39 $/trade, 109) and PPI (−0.49, 107) days; gap retest on FOMC days (−9.25, 52).
   The cascade cells are ≈ $0 (skipping changes almost nothing); the gap-retest FOMC cell is the only one with size. Momentum
   (mom_dir_45_vwap_plan) has no cell that passes (FOMC −10.4 on 13 trades, n < 30). Also passing, information only:
   mom_dir_45_dip CPI / PPI / FOMC, so_cascade_2r_vwap_stall PPI / FOMC. Multiple testing: 30 cells — expect a few by chance.
   ADOPTED: plan page — cascade "stand aside" on CPI / PPI days, gap retest off on FOMC days; paper id gap_or_retest__nofomc.
3. QUIET-MARKET STAND-ASIDE — FAIL both. Walk-forward (basket C+G+M one at a time) VIX: 2013-18 −59 vs always −2,547, but total
   +16,644 vs +19,641 (2019-24 gives back more than 2013-18 saves); ATR %: 2013-18 +722 vs −2,547, total +17,307 vs +19,641.
   Descriptive: in 2010-18 the basket is ≈ 0 or negative at every VIX and ATR % level (best +0.8 $/trade, VIX 16–20) — the weak era is not a
   low-volatility problem a filter known at the open can fix. VERDICT: no filter adopted; 2010-18 stays the open risk.
