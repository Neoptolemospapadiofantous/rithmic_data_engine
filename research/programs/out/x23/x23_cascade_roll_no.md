# Book F cascade, fixed setting, roll window: no (`x23_cascade_roll_no`)

**Why it should work:** Data check: the calendar-rolled continuous contract mixes two contracts across the roll window, so open/pc/atr there are unreliable. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X4).

Market NQ · real cost 1.00 pts/contract round trip · 1 variants · data from 2010-07-01 to 2026-10-16

## Verdict: **FAIL at CONTROLS**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (k1=0.25 k2=0.3 rr=2 stopk=0.35) net +5.26 pts/trade, t 3.53; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | pass — 1677 out-of-sample trades, +6.46 pts/trade net, PF 1.25, t 3.28 |
| 3 Controls | FAIL — beats random direction in 100%, random time in 10%, random day in 93% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | pass — 7/7 checks (need 5, and the latest era positive: yes) |
| 5 Prop fit | pass — select_daily_50k: best break-even eval fee $925 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 101.6 trades/yr · win 45% · gross +7.46 pts · net +6.46 pts/trade = $129 per NQ / $11.9 per MNQ
- picked blind (average variant): +11916 pts total · walk-forward: +10837 · hindsight best: +11916
- months traded 146/184, positive 80

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2011 | 38 | +53 | +1052 |
| 2012 | 135 | +352 | +7049 |
| 2013 | 117 | -261 | -5216 |
| 2014 | 61 | -102 | -2047 |
| 2015 | 18 | -109 | -2178 |
| 2016 | 22 | -25 | -498 |
| 2017 | 134 | +112 | +2239 |
| 2018 | 143 | +528 | +10552 |
| 2019 | 130 | +244 | +4884 |
| 2020 | 138 | +275 | +5493 |
| 2021 | 147 | +2632 | +52642 |
| 2022 | 155 | +1272 | +25440 |
| 2023 | 99 | -307 | -6131 |
| 2024 | 151 | +3416 | +68322 |
| 2025 | 149 | +1857 | +37147 |
| 2026 | 40 | +900 | +17997 |

Years positive: 11/16

## Months

- positive 80/176 (45%), since 2019 52/89 (58%); longest run without a positive month 27

## Controls
Real net +10837 pts; random direction mean +1379, random time mean +13273, random day mean +6265.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | yes | +0.82 pts/trade, n 668 |
| era 2019-22 | yes | +7.76 pts/trade, n 570 |
| era 2023-26 | yes | +13.36 pts/trade, n 439 |
| cost × 1.5 | yes | +9758 pts total |
| entry 1 bar late | yes | +10054 pts total, n 1538 |
| confirm on ES | yes | +1691 pts total, n 1173 |
| plateau (variants net > 0) | yes | 100% of 1 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 14.1 | 18 | 9 | 35 | 16546 | 2.5 | 1177 | 919 |
| 4 | 14.1 | 72 | 22 | 51 | 40415 | 3.6 | 2875 | 561 |
| 6 | 14.1 | 79 | 26 | 72 | 73044 | 5.1 | 5197 | 925 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| k1=0.25 k2=0.3 rr=2 stopk=0.35 | 2261 | +5.26 | 1.24 | 3.53 |

_edge_lab run in 1.4 s_
