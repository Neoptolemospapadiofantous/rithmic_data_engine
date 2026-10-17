# 2026-10-16 — confirm the gap-agree finding + options expiry (pre-registered before any run)

Costs as edge_lab: NQ 1.0, ES 0.35, MNQ 1.5, MES 0.6 pts per round trip. Family of 3 strategy tests → controls at
98.3 % (0.05 / 3), 400 seeds. Month-end / quarter-end rebalancing is NOT re-run: `month_end_rebalance` and
`turn_of_month` already failed controls (2026-10-16 09:42). Index-rebalance days (~4/yr) are too few to test.

## Strategy tests
1. `es_cascade_gap_agree` — the NQ `cascade_gap_agree` spec unchanged, on ES. CONFIRMS the NQ finding only if: gates
   pass AND it beats `es_cascade` (+1.10 ES pts/trade) AND the latest era is positive. If it fails, the NQ result is
   downgraded to "possibly luck" (it stays a paper candidate, sizing up on it is not recommended).
2. `opex_fade` — options-expiry Friday (3rd Friday): at 11:00 / 12:00 / 13:00 if |px − open| > k × ATR, fade toward
   the open, flat 15:55; k ∈ {0.3, 0.5}, stop {0.25, 0.4} × ATR, target 1R / to-flat. Why: dealers long gamma into
   expiry hedge against the move (buy dips / sell rips), pinning price near large strikes. Fleet candidate only if gates
   pass AND both eras since 2019 positive.
3. `cascade_skip_opex` — the plain cascade with opex days removed (+ `_compl` = opex days only). Adopted only if the
   removed opex days lose money after cost.

## Exit test (edge_lab exits, its own 99 % rule)
4. `cascade_gap_agree` + the VWAP thesis exit (and vwap+stall). Adopted only if it passes the exits gates on
   cascade_gap_agree's own trades.

## Prop simulation (not a strategy test — no new parameters chosen)
5. Select Flex 50K, eval 8 MNQ → funded 2 MNQ, cascade + VWAP/stall, 2019–26 months resampled (10,000 years), measured
   cost 1.5 pts/MNQ: base sizing vs DOUBLE size on gap-agree days (gap · dir ≥ 0.1 ATR). Reported: median weeks to the
   first payout, % within 3 months, % of evals failed. Recommend the sizing only if test 1 CONFIRMS and the median is
   ≥ 2 weeks faster without a higher eval failure rate.

## Results (2026-10-16, as registered)
1. `es_cascade_gap_agree` — **PASS, CONFIRMS**: +1.97 ES pts/trade (854 trades, PF 1.42, t 3.69) vs the ES cascade's
   +1.10; eras +0.51 / +4.36 / +1.94 (latest positive); controls: random direction 100 %, random day 100 %, random
   time 64 % (passes on the day control). The NQ gap-agree result is no longer a single-market finding.
2. `opex_fade` — FAIL at screen (26 trades, −2.48 pts/trade). No pinning edge at 1-minute resolution.
3. `cascade_skip_opex` — FAIL (+4.50, below the plain cascade); opex days alone are +16.36 on 41 trades (complement,
   fails controls) — removing them would cost money. Not adopted.
4. `cascade_gap_agree` + exits — **PASS (vwap)**: +10.05 vs +8.59 pts/trade, PF 1.72, max drawdown 508 vs 1,152 pts.
   vwap + stall +9.57 / PF 1.74. Paper candidate `so_cascade_2r_gapagree_vwap`.
5. Prop simulation (10,000 resampled years / real 2019–26 sequence):
   | sizing | Flex 50K median first payout | within the year | evals until it | Daily 50K median |
   |---|---|---|---|---|
   | base (cascade + VWAP/stall) | 16.0 wks (real seq. 16.4) | 95 % (real 100 %) | 3.3 | 23.4 wks (real 24.6) |
   | 2× on gap-agree days | 17.8 wks (real 16.8) | 90 % (real 95 %) | 4.3 | 21.2 wks (real 20.8) |
   | gap-agree trades only (orientation, not registered) | 30.4 wks | 69 % | 1.7 | 34.2 wks |
   Rule not met on Flex (slower, more failed evals). On Daily it is ~2–4 weeks faster but needs ~1 more eval → NOT
   recommended. Fewer, better trades are slower to a payout: the plan rewards trading every day.
