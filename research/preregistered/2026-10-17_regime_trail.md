# Let winners run when the tape trends (pre-registered 2026-10-17, before any run)

Founder: "so we have to take winning trades with trail". Today's regime study: in New York a trending morning tape (1-minute
autocorrelation) is followed by bigger continuation (+5.8 bp vs about +1.3 bp). Hypothesis: on those days a runner exit beats
the fixed 2R target; on other days the target is better.
- Trades: the walk-forward trades of so_cascade and gap_or_retest (unchanged entries), 2010-07 → 2024-12; 2025+ judged once.
- Regime at entry: acr over the last 60 one-minute moves before the entry bar; trending = acr > 0 (fixed, not searched).
- Exits compared (edge_lab ExitSim): base (stop + 2R + 15:55), vwap_stall (current cascade exit), vwap_run (no target, out on a
  5-min close back through VWAP), trail_run (no target, 15-min trail).
- Switch rule: trending tape → vwap_run, otherwise the strategy's current exit (cascade vwap_stall, gap retest base).
- It counts only if the switch beats the current exit per trade overall AND in ≥ 2 of 3 eras including 2023-26, AND on the
  2025+ holdout. One rule, no threshold search.

## Results (2026-10-17) — NOT adopted
- Cascade: switch +5.58 vs current +4.99 pts/trade 2010-24, better in all three eras (+0.77/+0.69, +10.38/+9.69, +11.18/+9.25)
  — but the 2025+ holdout says no: +13.35 vs +14.20 (n 209). Fails the pre-registered rule.
- Gap retest: switch worse everywhere (+6.89 vs +9.03 in-sample, +4.10 vs +6.47 holdout); its fixed 2R target is best on trending
  and non-trending tape alike.
- The 15-minute trail is the worst exit in every slice for both strategies.
