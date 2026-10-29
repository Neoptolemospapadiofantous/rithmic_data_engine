# 2026-10-24 — the video's "hedge fund Markov method" tested as stated (PRE-REGISTERED before the run)

Source: Lewis Jackson Investing, "I Re-Created A Quant Trading Strategy With Claude Code" (2026-05-19; transcript in
research/external/). Founder: "yes" (test the recipe on NQ and BTC).

## Recipe, as the video states it
- State each day from the trailing 20-day return: ≥ +5 % bull, ≤ −5 % bear, else sideways.
- Transition matrix from the counted day-to-day state changes; signal = P(bull tomorrow) − P(bear tomorrow) given today's state;
  the sign is the direction, the size scales with the value.
- Walk-forward: the matrix for day d is built ONLY from transitions before d (expanding window).

## Test
- NQ: RTH daily closes 2010-2026 from the 1-minute cache; hold close → next close. BTC: daily closes (00:00 UTC) from Binance
  1-minute flow bars (spot 2017-08 → 2019-12, perps 2020 → 2026-10).
- Position for day d+1 = signal (between −1 and +1), cost charged on the change in position: NQ 0.02 %, BTC 0.10 % of notional.
- Compared with: buy-and-hold; plain 20-day MOMENTUM (position = sign of the 20-day return, same states, no matrix);
  RANDOM signs with the same average |position| (500 draws).
- Reported: annual return, Sharpe, worst drawdown, by period (NQ 2011-2019 / 2020-2026; BTC 2018-2021 / 2022-2026), the matrix's
  diagonal (stickiness) with overlapping 20-day states vs non-overlapping 20-day blocks.
- Verdict: the recipe adds value only if it beats BOTH buy-and-hold (risk-adjusted) and plain momentum in BOTH periods after costs,
  and its Sharpe is above the 95th percentile of the random-sign controls.

## Results (2026-10-24; research/programs/video_markov_recipe.py, out research/programs/out/video_markov_recipe.txt)
- Stickiness is built in: with overlapping 20-day states the diagonal is 79-90 % (NQ) / 76-88 % (BTC); on non-overlapping 20-day
  blocks it drops to 0-66 % (NQ) / 29-37 % (BTC). The signal correlates 0.54 (NQ) / 0.81 (BTC) with plain 20-day momentum.
- NQ Sharpe: 2011-19 recipe −0.21 vs buy & hold 1.02, momentum 0.17, random 95th pct 0.39; 2020-26 recipe 0.08 vs 0.87 / −0.03 / 0.56.
- BTC Sharpe: 2018-21 recipe 0.93 vs buy & hold 0.79, momentum 1.08, random 95th pct 0.44; 2022-26 recipe −0.15 vs 0.49 / 0.10 / 0.22.
- FAIL on both markets: never beats buy-and-hold in either NQ period; on BTC it worked only in the 2018-21 trend years, worse than
  plain momentum there, and lost money after costs in 2022-26. (Max DD is on summed daily returns, so it can exceed 100 %.)

## Re-evaluation (founder: "re evaluate and re run") — PRE-REGISTERED before the second run
Faults found in run 1: NQ cost 0.02 % per unit of position change is ~4× too high (a tick + commission ≈ 0.005 %); only the video's
exact settings were tried; raw Sharpe compared a part-time position with always-long buy & hold.
Run 2: costs NQ 0.005 % and 0; BTC 0.04 % (maker) and 0.10 %. Grid: lookback {10, 20, 60} days × threshold {3, 5, 10} % × mode
{long-short, long-only, sign-only (±1 instead of the probability difference)} = 27 variants per market. CHOOSE the best Sharpe on the
first period (NQ 2011-19, BTC 2018-21), READ the second period ONCE. Report buy & hold, plain momentum with the same lookback, and
500 random-sign controls; also the recipe's return scaled to buy & hold's volatility. Verdict rule unchanged: the chosen variant must
beat buy & hold, momentum and the random 95th percentile on Sharpe in the second period.
