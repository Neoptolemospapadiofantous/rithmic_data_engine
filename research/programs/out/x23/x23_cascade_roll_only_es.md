# Book F cascade, fixed setting, roll window: only (`x23_cascade_roll_only_es`)

**Why it should work:** Data check: the calendar-rolled continuous contract mixes two contracts across the roll window, so open/pc/atr there are unreliable. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X4).

Market ES · real cost 0.35 pts/contract round trip · 1 variants · data from 2010-07-01 to 2026-10-16

## Verdict: **FAIL at SCREEN**

| gate | result |
|---|---|
| 1 Screen | FAIL — best variant (k1=0.25 k2=0.3 rr=2 stopk=0.35) net -0.42 pts/trade, t -0.42; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | FAIL — 155 out-of-sample trades, -0.66 pts/trade net, PF 0.90, t -0.50 |
| 3 Controls | FAIL — beats random direction in 58%, random time in 4%, random day in 23% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | FAIL — 1/6 checks (need 5, and the latest era positive: yes) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $0 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 9.4 trades/yr · win 40% · gross -0.31 pts · net -0.66 pts/trade = $-33 per ES / $-4.6 per MES
- picked blind (average variant): -148 pts total · walk-forward: -103 · hindsight best: -148
- months traded 94/175, positive 15

| year | trades | net pts | $ per ES |
|---|---|---|---|
| 2011 | 11 | -11 | -528 |
| 2012 | 13 | +6 | +287 |
| 2013 | 15 | +8 | +389 |
| 2014 | 11 | -26 | -1296 |
| 2015 | 16 | -15 | -731 |
| 2016 | 10 | +0 | +23 |
| 2017 | 3 | -14 | -697 |
| 2018 | 10 | -38 | -1879 |
| 2019 | 8 | +10 | +485 |
| 2021 | 6 | -37 | -1836 |
| 2022 | 13 | +8 | +418 |
| 2023 | 11 | -36 | -1778 |
| 2024 | 5 | +8 | +425 |
| 2025 | 18 | +107 | +5344 |
| 2026 | 5 | -75 | -3772 |

Years positive: 7/15

## Months

- positive 15/175 (9%), since 2019 6/87 (7%); longest run without a positive month 41

## Controls
Real net -103 pts; random direction mean -152, random time mean +114, random day mean +32.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | -1.00 pts/trade, n 89 |
| era 2019-22 | no | -0.69 pts/trade, n 27 |
| era 2023-26 | yes | +0.11 pts/trade, n 39 |
| cost × 1.5 | no | -103 pts total |
| entry 1 bar late | no | -251 pts total, n 145 |
| plateau (variants net > 0) | no | 0% of 1 |

## Prop (select_daily_50k, MES at 0.60 pts cost; eval → funded until breach → new eval)

| MES | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 14.1 | 2 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 14.1 | 5 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 14.1 | 8 | 1 | 0 | 0 | 0.0 | 0 | 0 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| k1=0.25 k2=0.3 rr=2 stopk=0.35 | 309 | -0.42 | 0.94 | -0.42 |

_edge_lab run in 0.3 s_
