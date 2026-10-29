# Book F cascade, fixed setting, roll window: all (`x23_cascade_roll_all`)

**Why it should work:** Data check: the calendar-rolled continuous contract mixes two contracts across the roll window, so open/pc/atr there are unreliable. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X4).

Market NQ · real cost 1.00 pts/contract round trip · 1 variants · data from 2010-07-01 to 2026-10-16

## Verdict: **FAIL at CONTROLS**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (k1=0.25 k2=0.3 rr=2 stopk=0.35) net +4.77 pts/trade, t 3.41; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | pass — 1738 out-of-sample trades, +5.55 pts/trade net, PF 1.21, t 2.86 |
| 3 Controls | FAIL — beats random direction in 100%, random time in 14%, random day in 84% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | pass — 7/7 checks (need 5, and the latest era positive: yes) |
| 5 Prop fit | pass — select_daily_50k: best break-even eval fee $971 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 105.3 trades/yr · win 45% · gross +6.55 pts · net +5.55 pts/trade = $111 per NQ / $10.1 per MNQ
- picked blind (average variant): +12347 pts total · walk-forward: +9643 · hindsight best: +12347
- months traded 133/184, positive 72

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2011 | 49 | +72 | +1448 |
| 2012 | 161 | +421 | +8421 |
| 2013 | 151 | -181 | -3629 |
| 2014 | 65 | -155 | -3100 |
| 2015 | 18 | -109 | -2178 |
| 2017 | 129 | +120 | +2409 |
| 2018 | 164 | +587 | +11741 |
| 2019 | 152 | +251 | +5024 |
| 2020 | 104 | -656 | -13129 |
| 2021 | 139 | +1771 | +35418 |
| 2022 | 176 | +1714 | +34275 |
| 2023 | 76 | -721 | -14419 |
| 2024 | 141 | +3609 | +72185 |
| 2025 | 168 | +2128 | +42553 |
| 2026 | 45 | +792 | +15834 |

Years positive: 10/15

## Months

- positive 72/176 (41%), since 2019 44/89 (49%); longest run without a positive month 27

## Controls
Real net +9643 pts; random direction mean +2089, random time mean +11894, random day mean +5983.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | yes | +1.03 pts/trade, n 737 |
| era 2019-22 | yes | +5.39 pts/trade, n 571 |
| era 2023-26 | yes | +13.51 pts/trade, n 430 |
| cost × 1.5 | yes | +8882 pts total |
| entry 1 bar late | yes | +9518 pts total, n 1617 |
| confirm on ES | yes | +1344 pts total, n 1390 |
| plateau (variants net > 0) | yes | 100% of 1 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 14.1 | 23 | 9 | 36 | 19471 | 2.6 | 1383 | 847 |
| 4 | 14.1 | 70 | 18 | 55 | 42001 | 3.9 | 2982 | 600 |
| 6 | 14.1 | 82 | 20 | 81 | 79623 | 5.8 | 5654 | 971 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| k1=0.25 k2=0.3 rr=2 stopk=0.35 | 2580 | +4.77 | 1.22 | 3.41 |

_edge_lab run in 1.4 s_
