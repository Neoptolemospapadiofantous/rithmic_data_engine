# Leveraged-ETF close rebalance: follow today's move from 15:30 (`x23_letf_close_1530`)

**Why it should work:** Daily-reset leveraged and inverse ETFs must buy into the close after an up day and sell after a down day, in proportion to today's return from the prior close; the flow is forced and price-insensitive. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X1).

Market NQ · real cost 1.00 pts/contract round trip · 3 variants · data from 2010-07-01 to 2024-12-31

## Verdict: **FAIL at SCREEN**

| gate | result |
|---|---|
| 1 Screen | FAIL — best variant (p=0.5) net -0.21 pts/trade, t -0.37; single-test t needed for 3 variants ≈ 2.39 |
| 2 Walk-forward | pass — 469 out-of-sample trades, +1.69 pts/trade net, PF 1.16, t 1.17 |
| 3 Controls | FAIL — beats random direction in 98%, random time in 74%, random day in 71% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | FAIL — 4/7 checks (need 5, and the latest era positive: NO) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $0 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 28.4 trades/yr · win 48% · gross +2.69 pts · net +1.69 pts/trade = $34 per NQ / $2.4 per MNQ
- picked blind (average variant): -474 pts total · walk-forward: +795 · hindsight best: +2371
- months traded 68/162, positive 29

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2013 | 19 | -53 | -1050 |
| 2015 | 11 | -54 | -1089 |
| 2017 | 4 | -23 | -461 |
| 2018 | 47 | +241 | +4825 |
| 2019 | 61 | -61 | -1222 |
| 2020 | 88 | +635 | +12704 |
| 2021 | 88 | -194 | -3877 |
| 2022 | 106 | +477 | +9541 |
| 2023 | 45 | -174 | -3476 |

Years positive: 3/9

## Months

- positive 29/125 (23%), since 2019 24/56 (43%); longest run without a positive month 58

## Controls
Real net +795 pts; random direction mean -616, random time mean +590, random day mean +414.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | yes | +1.37 pts/trade, n 81 |
| era 2019-22 | yes | +2.50 pts/trade, n 343 |
| era 2023-26 | no | -3.86 pts/trade, n 45 |
| cost × 1.5 | yes | +648 pts total |
| entry 1 bar late | yes | +153 pts total, n 448 |
| confirm on ES | no | -195 pts total, n 430 |
| plateau (variants net > 0) | no | 0% of 3 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 10.1 | 1 | 1 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 10.1 | 4 | 2 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 10.1 | 11 | 3 | 0 | 0 | 0.0 | 0 | 0 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| p=0.5 | 1499 | -0.21 | 0.97 | -0.37 |
| p=0.75 | 987 | -0.60 | 0.92 | -0.85 |
| p=1 | 634 | -1.29 | 0.84 | -1.43 |

_edge_lab run in 7.0 s_
