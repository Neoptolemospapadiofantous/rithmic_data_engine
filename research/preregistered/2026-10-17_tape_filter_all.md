# The tape regime as a filter on every strategy family (pre-registered 2026-10-17, before any run)

Founder: "have we tested other strategies with this new regime" + "possible 1-2-3-4-5 m". Regime = lag-1 autocorrelation of the
last 30 bar-to-bar moves, acr(rt, 30), on rt ∈ {1, 2, 3, 4, 5}-minute bars (a setting; the walk-forward picks it each month).
- Trend-following → trade only when the tape trends, acr(rt,30) > 0 (id suffix __tape_trend):
  so_cascade, gap_or_retest, dir_market, dir_breakout, orb, strong_open, cond_brk_none, cond_trend_none, cond_pull_none, ema_cross_vwap
- Reversal → trade only when the tape mean-reverts, acr(rt,30) < 0 (id suffix __tape_rev):
  cond_rev_none, vwap_fade, gap_fade_open, range_day_fade, exhaustion_fade, weak_day_long
- Everything else in each spec unchanged (same dates, entries, exits). The filter "works" for a strategy only if the filtered idea
  PASSES and its walk-forward pts/trade beats the unfiltered idea's. 16 ideas × 5 regime timeframes counted in the registry.

## Results (2026-10-17) — the tape filter helps nothing
Unfiltered → filtered, walk-forward pts/trade: so_cascade PASS 4.80 → FAIL 3.04; gap_or_retest PASS 9.03 → PASS 5.98;
strong_open PASS 4.18 → FAIL 4.02; dir_market PASS 5.63 → FAIL 1.71; ema_cross_vwap PASS 6.79 → FAIL 3.39; weak_day_long PASS
13.20 → FAIL 5.79; dir_breakout 3.67 → 2.28; orb 1.49 → 0.80; cond_pull 2.12 → 0.11; range_day_fade 0.72 → −0.78;
exhaustion_fade 4.49 → −1.87. Slightly better but still failing: cond_brk 2.39 → 3.32, gap_fade 2.41 → 2.94, cond_rev −0.61 →
−0.32, vwap_fade −1.04 → −0.16. No failed strategy became a pass; five passes became fails.
