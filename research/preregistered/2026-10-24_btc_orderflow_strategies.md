# 2026-10-24 — standalone ORDER-FLOW strategies on BTC (ETH as the out-of-market check) — PRE-REGISTERED before any run

Founder: "we want to do more tests to get an accurate representation of a good strategy on BTC since we have a lot of order flow
data". Not a filter on the flag book: strategies whose signal IS the order flow.

## Data
Binance spot aggTrades (data/history/binance, every taker order with its side), BTCUSDT and ETHUSDT 2020-01 → 2026-10-09.
1-minute flow bars built by research/programs/flow_bars.cpp: open/high/low/close, volume, taker-buy volume, trade count,
LARGE taker buy / sell volume (one aggregated taker order ≥ $100,000 notional), largest order.

## Costs and execution
Entry at the NEXT 1-minute bar's open after the signal bar closes (no look-ahead); exit at a fixed horizon H's bar close.
Cost per round trip: TAKER 0.10 % of price (baseline — market in, market out), MAKER 0.04 % (sensitivity only). Funding ignored
for H ≤ 4 h. One position per strategy at a time (signals while in a trade are skipped).

## Strategy families (parameters fixed now; H ∈ {15, 60, 240} minutes for all)
- S1 BURST-FOLLOW: bar delta ratio |2·buy−vol|/vol ≥ x (0.30, 0.50) AND volume ≥ k × its trailing 60-bar median (k 3, 5) → trade
  WITH the delta.
- S2 BURST-FADE: same trigger, trade AGAINST the delta.
- S3 DELTA-DIVERGENCE: the close is the highest (lowest) of the last 60 bars while the 60-bar cumulative delta is BELOW (ABOVE) its
  value at the previous such extreme → trade against the new extreme. Variant: lookback 30 / 60.
- S4 ABSORPTION: volume ≥ 3× median, delta ratio ≤ −y (0.30, 0.50) yet the bar closes in its upper half (close-location ≥ 0.6) →
  long; mirrored for shorts.
- S5 WHALE-FOLLOW: net large-order delta over the last 15 bars in its top / bottom 2 % (trailing 30 days) → trade with it.
  Variant: top 2 % / top 5 %.
- S6 CASCADE-FADE: price moved ≥ m × ATR(60 bars) (m 3, 5) within 5 bars with delta ratio over those bars ≤ −0.3 (down) / ≥ +0.3
  (up) → fade it. S6b CASCADE-FOLLOW: same trigger, trade with it.
- S7 CVD-TREND: 240-bar cumulative delta and 240-bar price change have the same sign and |CVD| is in its top 20 % (trailing 30 d)
  → trade with both, re-evaluated every 240 bars.
- S8 SESSION-OPEN FLOW: delta of the first 30 minutes after 13:30 UTC (NY open; 14:30 in EST months handled by US DST) and after
  00:00 UTC (Asia) → trade with it until H.

## Evaluation
- Splits: CHOOSE on 2020-01 → 2022-12 (best variant per family by mean net return per trade with ≥ 150 trades), VALIDATE
  2023-01 → 2024-12, HOLDOUT 2025-01 → 2026-10 read ONCE, then ETH 2020-2026 with the BTC-chosen parameters.
- Control: random-entry twins — the same number of entries, same direction mix, same hour-of-day mix, same H, inside the same
  period; edge = strategy mean − twin mean (2,000 draws, p).
- Overfitting: CSCV probability of backtest overfitting over ALL variants (S = 16 blocks, 2020-2024).
- PASS (all): net mean per trade > 0 at TAKER cost in validation AND holdout; beats its twin (p < 0.05) in both; net > 0 on ETH;
  family PBO < 0.3. A family passing only at MAKER cost is reported as "maker-only" (needs limit execution), not a pass.
- Survivors then get the prop-style account check (daily-loss / drawdown) as a separate step.

## Amendment (2026-10-24, before any strategy result was seen) — more history
The existing files are Binance USDⓈ-M PERPETUAL FUTURES aggTrades (scripts/binance_history_download.sh pulls futures/um), which
start in 2019-09. Binance SPOT aggTrades go back to 2017-08 and are added for 2017-08 → 2019-12 (downloaded month by month into the
scratchpad, turned into flow bars, the zip removed — the disk has < 5 GB free). The pre-registered splits do NOT move: 2017-08 →
2019-12 is an EXTRA, EARLIER out-of-sample period read once with the chosen parameters ("pre-history"), reported separately because
it is spot, not perps. Pass rules unchanged; the pre-history result is reported next to them.

## Results (2026-10-24) — outputs research/programs/out/btc_flow_strategies_*.{csv,txt}
Flow bars: BTC + ETH perps 2020-01 → 2026-10-09 (82 months each), BTC + ETH spot 2017-08 → 2019-12 (29 months each), built by
flow_bars.cpp. 60 variants (8 families × parameters × H 15/60/240).
- NOTHING PASSES. BTC 2020-22: gross mean per trade median +0.0 bp, best +4.3 bp across all 60 variants; 0 variants positive at the
  10 bp taker cost, 1 at the 4 bp maker cost. Every family's chosen variant loses at taker AND maker cost in validation (2023-24)
  and holdout (2025-26), and on ETH (−9 to −13 bp).
- Edges over random entries exist but are tiny: burst-FADE holdout twin p 0.024, divergence holdout p 0.008 (pre-history p 0.002),
  CVD-trend validation p 0.009 — gross 2-5 bp, below even the maker fee.
- Pre-history (spot 2017-19, read once): cascade-FOLLOW m5 H15 +11.0 bp gross (+1.0 bp at taker, twin p 0.002) — it worked in the
  older market and fails in every period since 2020 (−10 bp). Median variant +0.6 bp gross.
- PBO reads 0.00 for most families because every variant loses and the least-bad stays least-bad: a ranking statistic, not a sign
  of profit.
Verdict: at 15-240-minute horizons, BTC/ETH order flow does not pay its fees. A viable crypto strategy needs moves much larger than
the fee (the flag book: 0.42 % stop, passes at maker cost) or near-zero execution cost (maker on both legs / fee rebates).
