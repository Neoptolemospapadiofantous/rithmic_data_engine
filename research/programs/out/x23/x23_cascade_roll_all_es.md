# Book F cascade, fixed setting, roll window: all (`x23_cascade_roll_all_es`)

**Why it should work:** Data check: the calendar-rolled continuous contract mixes two contracts across the roll window, so open/pc/atr there are unreliable. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X4).

Market ES · real cost 0.35 pts/contract round trip · 1 variants · data from 2010-07-01 to 2026-10-16

## Verdict: **FAIL at CONTROLS**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (k1=0.25 k2=0.3 rr=2 stopk=0.35) net +0.73 pts/trade, t 2.17; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | pass — 1390 out-of-sample trades, +0.97 pts/trade net, PF 1.14, t 1.83 |
| 3 Controls | FAIL — beats random direction in 97%, random time in 0%, random day in 84% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | pass — 5/6 checks (need 5, and the latest era positive: yes) |
| 5 Prop fit | pass — select_daily_50k: best break-even eval fee $1629 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 84.2 trades/yr · win 44% · gross +1.32 pts · net +0.97 pts/trade = $48 per ES / $3.6 per MES
- picked blind (average variant): +1741 pts total · walk-forward: +1344 · hindsight best: +1741
- months traded 118/184, positive 57

| year | trades | net pts | $ per ES |
|---|---|---|---|
| 2011 | 33 | -65 | -3232 |
| 2012 | 40 | -24 | -1197 |
| 2013 | 108 | -46 | -2322 |
| 2014 | 77 | -60 | -3025 |
| 2015 | 15 | -41 | -2045 |
| 2016 | 64 | -57 | -2846 |
| 2018 | 119 | +84 | +4213 |
| 2019 | 135 | +83 | +4131 |
| 2020 | 45 | -415 | -20734 |
| 2021 | 117 | +238 | +11883 |
| 2022 | 157 | +376 | +18789 |
| 2023 | 158 | +184 | +9186 |
| 2024 | 139 | +233 | +11637 |
| 2025 | 143 | +652 | +32619 |
| 2026 | 40 | +203 | +10126 |

Years positive: 8/15

## Months

- positive 57/177 (32%), since 2019 46/89 (52%); longest run without a positive month 41

## Controls
Real net +1344 pts; random direction mean -11, random time mean +2510, random day mean +505.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | -0.46 pts/trade, n 456 |
| era 2019-22 | yes | +0.62 pts/trade, n 454 |
| era 2023-26 | yes | +2.65 pts/trade, n 480 |
| cost × 1.5 | yes | +1302 pts total |
| entry 1 bar late | yes | +1411 pts total, n 1295 |
| plateau (variants net > 0) | yes | 100% of 1 |

## Prop (select_daily_50k, MES at 0.60 pts cost; eval → funded until breach → new eval)

| MES | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 14.2 | 6 | 2 | 26 | 9774 | 1.8 | 689 | 1629 |
| 4 | 14.2 | 25 | 7 | 24 | 12490 | 1.7 | 881 | 500 |
| 6 | 14.2 | 54 | 14 | 14 | 8424 | 1.0 | 594 | 156 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| k1=0.25 k2=0.3 rr=2 stopk=0.35 | 2386 | +0.73 | 1.13 | 2.17 |

_edge_lab run in 0.6 s_
