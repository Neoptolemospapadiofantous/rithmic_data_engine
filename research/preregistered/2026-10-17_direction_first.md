# Direction first, then the fleet's entry (pre-registered 2026-10-17, before any run)

Founder: "remove vix and try just after we set a direction". Question: once the morning has set a direction, does a
fleet-style entry IN THAT DIRECTION make money, and does it beat simply entering?

- Direction: at 10:00, s = sign(10:00 price − 09:30 open), only when |10:00 − open| > 0.25 ATR. No VIX, no other filter.
- Entries, scanned on completed 5-minute bars 10:05–14:00, first one only, one trade a day, in direction s only:
  dir_vwap_pb   a 5-min bar touches the session VWAP and closes back on the trend side
  dir_ema_pb    the same with the 5-min EMA(20)
  dir_breakout  a 5-min close beyond the day's extreme so far (new high for longs, new low for shorts)
  dir_fib38     price has given back ≥ 38.2 % of the open → extreme move and the 5-min bar closes back in the trend direction
  dir_or_retest a 5-min bar touches the 15-minute opening range edge and closes back outside it
  dir_trend60   a 5-min close beyond the last 60 minutes' high (low)
  dir_market    benchmark: market entry at 10:05
- Exits for all: stop 0.25 or 0.35 ATR, target 2R, flat 15:55 (2 variants each).
- Verdicts: each through edge_lab's five gates on 2010-07 → 2024-12; 2025+ held back. A family "adds value" only if it
  PASSES and its walk-forward pts/trade beats dir_market's. 7 ideas × 2 variants are counted in the registry.
- 2026-10-17 fix before any result was used: dir_trend60 first compared the bar's close with a 60-minute high that INCLUDED that bar (never true, 0 trades); now the 60 minutes before it (lag 5).
