# Direction from the gap / from a weak day, then the fleet's entry (pre-registered 2026-10-17, before any run)

Follow-up to 2026-10-17_direction_first.md (founder: "try the same with other directions like the gap and weak day").
Same six entries and the same benchmark, two new direction sources, no VIX:
- gap_*   s = sign(gap), only when |gap| ≥ 0.1 ATR (gap = (09:30 open − prior 16:00 close) / ATR), roll windows out
- wk_*    s = +1 (long only) when yesterday was weak: closed in its bottom 30 % (pibs ≤ 0.3) or RSI(2) ≤ 20
Entries scanned on completed 5-minute bars 09:35–14:00, first only, one trade a day, in direction s only:
vwap_pb, ema_pb, breakout, fib38, or_retest, trend60 (definitions as in direction_first, trend60 with lag 5); benchmark
*_market = market entry at 09:35. Stop 0.25 / 0.35 ATR, target 2R, flat 15:55. Data 2010-07 → 2024-12, 2025+ held back.
A family adds value only if it PASSES and beats its own *_market per trade. 14 ideas × 2 variants counted in the registry.

## Results (2026-10-17)
- gap direction: gap_breakout PASS (+7.43, 1268 trades) and gap_or_retest PASS (+9.03, 1220) beat gap_market (+2.32, FAIL at
  controls) — the confirming entry adds value here. Eras 0.8/13.6/10.4 and 0.4/16.1/16.9: a post-2019 effect. Monthly corr
  with so_cascade 0.38 / 0.33 (since 2019). Holdout 2025+ judged once, frozen stopk 0.35: breakout +1.01 (148 trades, PF 1.02)
  — collapsed; or_retest +7.15 (135, PF 1.13, t 0.6) — held up but not significant. Others: vwap_pb, trend60 fail controls,
  ema_pb, fib38 fail screen.
- weak day without VIX: every entry fails, the benchmark too (wk_market +1.92, era 2010-18 −5.5). The VIX ≥ 20 condition is
  what makes the weak-day long work; removing it removes the edge.
