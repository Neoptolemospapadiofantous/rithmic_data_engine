# Book F cascade, fixed setting, roll window: no (`x23_cascade_roll_no_es`)

**Why it should work:** Data check: the calendar-rolled continuous contract mixes two contracts across the roll window, so open/pc/atr there are unreliable. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X4).

Market ES · real cost 0.35 pts/contract round trip · 1 variants · data from 2010-07-01 to 2026-10-16

## Verdict: **PASS**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (k1=0.25 k2=0.3 rr=2 stopk=0.35) net +0.90 pts/trade, t 2.53; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | pass — 1173 out-of-sample trades, +1.44 pts/trade net, PF 1.21, t 2.50 |
| 3 Controls | pass — beats random direction in 100%, random time in 2%, random day in 96% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | pass — 5/6 checks (need 5, and the latest era positive: yes) |
| 5 Prop fit | pass — select_daily_50k: best break-even eval fee $1086 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 71.1 trades/yr · win 45% · gross +1.79 pts · net +1.44 pts/trade = $72 per ES / $6.0 per MES
- picked blind (average variant): +1889 pts total · walk-forward: +1691 · hindsight best: +1889
- months traded 115/184, positive 58

| year | trades | net pts | $ per ES |
|---|---|---|---|
| 2011 | 28 | -53 | -2653 |
| 2012 | 13 | -24 | -1223 |
| 2013 | 66 | -45 | -2238 |
| 2014 | 64 | -48 | -2413 |
| 2016 | 56 | -50 | -2512 |
| 2017 | 12 | -45 | -2258 |
| 2018 | 126 | +155 | +7730 |
| 2019 | 118 | +107 | +5341 |
| 2020 | 37 | -224 | -11177 |
| 2021 | 100 | +282 | +14103 |
| 2022 | 134 | +334 | +16722 |
| 2023 | 136 | +262 | +13085 |
| 2024 | 123 | +217 | +10875 |
| 2025 | 125 | +546 | +27275 |
| 2026 | 35 | +278 | +13898 |

Years positive: 8/15

## Months

- positive 58/177 (33%), since 2019 47/89 (53%); longest run without a positive month 40

## Controls
Real net +1691 pts; random direction mean -18, random time mean +2621, random day mean +553.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | -0.31 pts/trade, n 365 |
| era 2019-22 | yes | +1.28 pts/trade, n 389 |
| era 2023-26 | yes | +3.11 pts/trade, n 419 |
| cost × 1.5 | yes | +1434 pts total |
| entry 1 bar late | yes | +1844 pts total, n 1189 |
| plateau (variants net > 0) | yes | 100% of 1 |

## Prop (select_daily_50k, MES at 0.60 pts cost; eval → funded until breach → new eval)

| MES | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 14.2 | 5 | 3 | 13 | 5432 | 0.9 | 384 | 1086 |
| 4 | 14.2 | 20 | 8 | 28 | 14308 | 2.0 | 1011 | 715 |
| 6 | 14.2 | 47 | 14 | 18 | 12014 | 1.3 | 849 | 256 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| k1=0.25 k2=0.3 rr=2 stopk=0.35 | 2077 | +0.90 | 1.16 | 2.53 |

_edge_lab run in 0.6 s_
