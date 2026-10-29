# Leveraged-ETF close rebalance: follow today's move from 15:45 (`x23_letf_close_1545`)

**Why it should work:** Daily-reset leveraged and inverse ETFs must buy into the close after an up day and sell after a down day, in proportion to today's return from the prior close; the flow is forced and price-insensitive. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X1).

Market NQ · real cost 1.00 pts/contract round trip · 3 variants · data from 2010-07-01 to 2024-12-31

## Verdict: **FAIL at SCREEN**

| gate | result |
|---|---|
| 1 Screen | FAIL — best variant (p=0.5) net -0.27 pts/trade, t -0.60; single-test t needed for 3 variants ≈ 2.39 |
| 2 Walk-forward | pass — 455 out-of-sample trades, +2.82 pts/trade net, PF 1.39, t 2.51 |
| 3 Controls | pass — beats random direction in 100%, random time in 98%, random day in 84% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | FAIL — 4/7 checks (need 5, and the latest era positive: NO) |
| 5 Prop fit | pass — select_daily_50k: best break-even eval fee $5068 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 27.6 trades/yr · win 51% · gross +3.82 pts · net +2.82 pts/trade = $56 per NQ / $4.6 per MNQ
- picked blind (average variant): -382 pts total · walk-forward: +1284 · hindsight best: +1830
- months traded 64/162, positive 35

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2015 | 39 | -33 | -668 |
| 2016 | 19 | -39 | -784 |
| 2018 | 42 | -30 | -608 |
| 2019 | 18 | -8 | -168 |
| 2020 | 69 | +571 | +11412 |
| 2021 | 107 | +254 | +5073 |
| 2022 | 119 | +632 | +12650 |
| 2023 | 42 | -62 | -1232 |

Years positive: 3/8

## Months

- positive 35/99 (35%), since 2019 26/53 (49%); longest run without a positive month 28

## Controls
Real net +1284 pts; random direction mean -406, random time mean +799, random day mean +837.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | -1.03 pts/trade, n 100 |
| era 2019-22 | yes | +4.63 pts/trade, n 313 |
| era 2023-26 | no | -1.47 pts/trade, n 42 |
| cost × 1.5 | yes | +1180 pts total |
| entry 1 bar late | yes | +1947 pts total, n 521 |
| confirm on ES | yes | +61 pts total, n 418 |
| plateau (variants net > 0) | no | 0% of 3 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 8.0 | 1 | 1 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 8.0 | 1 | 1 | 10 | 5068 | 1.2 | 633 | 5068 |
| 6 | 8.0 | 7 | 4 | 5 | 4593 | 0.6 | 573 | 656 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| p=0.5 | 1504 | -0.27 | 0.95 | -0.60 |
| p=1 | 659 | -0.74 | 0.87 | -1.06 |
| p=0.75 | 1011 | -0.51 | 0.91 | -0.91 |

_edge_lab run in 0.9 s_
