# Book F cascade, fixed setting, roll window: only (`x23_cascade_roll_only`)

**Why it should work:** Data check: the calendar-rolled continuous contract mixes two contracts across the roll window, so open/pc/atr there are unreliable. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X4).

Market NQ · real cost 1.00 pts/contract round trip · 1 variants · data from 2010-07-01 to 2026-10-16

## Verdict: **FAIL at WALK-FORWARD**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (k1=0.25 k2=0.3 rr=2 stopk=0.35) net +1.29 pts/trade, t 0.32; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | FAIL — 187 out-of-sample trades, +0.79 pts/trade net, PF 1.03, t 0.13 |
| 3 Controls | FAIL — beats random direction in 42%, random time in 50%, random day in 68% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | pass — 5/7 checks (need 5, and the latest era positive: yes) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $0 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 11.3 trades/yr · win 41% · gross +1.79 pts · net +0.79 pts/trade = $16 per NQ / $0.6 per MNQ
- picked blind (average variant): +431 pts total · walk-forward: +148 · hindsight best: +431
- months traded 108/175, positive 19

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2011 | 7 | -34 | -676 |
| 2012 | 12 | +41 | +822 |
| 2013 | 19 | +50 | +1002 |
| 2014 | 20 | +14 | +280 |
| 2015 | 18 | -15 | -307 |
| 2016 | 5 | -66 | -1325 |
| 2017 | 5 | -61 | -1229 |
| 2018 | 15 | -39 | -770 |
| 2019 | 16 | +43 | +851 |
| 2020 | 6 | -373 | -7461 |
| 2021 | 5 | -110 | -2202 |
| 2022 | 21 | +442 | +8836 |
| 2023 | 6 | -202 | -4045 |
| 2024 | 8 | +297 | +5943 |
| 2025 | 19 | +270 | +5406 |
| 2026 | 5 | -108 | -2163 |

Years positive: 7/16

## Months

- positive 19/171 (11%), since 2019 9/87 (10%); longest run without a positive month 34

## Controls
Real net +148 pts; random direction mean +323, random time mean +151, random day mean -224.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | -1.09 pts/trade, n 101 |
| era 2019-22 | yes | +0.02 pts/trade, n 48 |
| era 2023-26 | yes | +6.76 pts/trade, n 38 |
| cost × 1.5 | yes | +322 pts total |
| entry 1 bar late | yes | +558 pts total, n 149 |
| confirm on ES | no | -103 pts total, n 155 |
| plateau (variants net > 0) | yes | 100% of 1 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 13.8 | 4 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 13.8 | 9 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 13.8 | 11 | 1 | 0 | 0 | 0.0 | 0 | 0 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| k1=0.25 k2=0.3 rr=2 stopk=0.35 | 319 | +1.29 | 1.06 | 0.32 |

_edge_lab run in 0.9 s_
