# Market intraday momentum: first half hour (incl. overnight) predicts the last half hour (`x23_intraday_mom`)

**Why it should work:** Gao, Han, Li & Zhou (2018): informed traders who act at the open and late-informed / rebalancing traders who act near the close trade the same way, so the return to 10:00 predicts 15:30-16:00. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X2).

Market NQ · real cost 1.00 pts/contract round trip · 3 variants · data from 2010-07-01 to 2024-12-31

## Verdict: **FAIL at SCREEN**

| gate | result |
|---|---|
| 1 Screen | FAIL — best variant (k=0.5) net -0.34 pts/trade, t -0.49; single-test t needed for 3 variants ≈ 2.39 |
| 2 Walk-forward | FAIL — 483 out-of-sample trades, +0.20 pts/trade net, PF 1.02, t 0.15 |
| 3 Controls | FAIL — beats random direction in 76%, random time in 66%, random day in 76% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | FAIL — 2/7 checks (need 5, and the latest era positive: NO) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $17 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 29.3 trades/yr · win 45% · gross +1.20 pts · net +0.20 pts/trade = $4 per NQ / $-0.6 per MNQ
- picked blind (average variant): -1739 pts total · walk-forward: +97 · hindsight best: +3014
- months traded 47/162, positive 22

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2012 | 3 | -3 | -54 |
| 2013 | 48 | -28 | -565 |
| 2015 | 8 | -24 | -487 |
| 2018 | 51 | -217 | -4333 |
| 2019 | 14 | -19 | -384 |
| 2020 | 175 | +906 | +18119 |
| 2021 | 123 | -385 | -7690 |
| 2023 | 36 | -155 | -3110 |
| 2024 | 25 | +22 | +437 |

Years positive: 2/9

## Months

- positive 22/146 (15%), since 2019 17/72 (24%); longest run without a positive month 58

## Controls
Real net +97 pts; random direction mean -307, random time mean -54, random day mean -295.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | -2.47 pts/trade, n 110 |
| era 2019-22 | yes | +1.61 pts/trade, n 312 |
| era 2023-26 | no | -2.19 pts/trade, n 61 |
| cost × 1.5 | no | -755 pts total |
| entry 1 bar late | yes | +127 pts total, n 520 |
| confirm on ES | no | -260 pts total, n 465 |
| plateau (variants net > 0) | no | 0% of 3 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 11.8 | 2 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 11.8 | 7 | 1 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 11.8 | 15 | 3 | 1 | 255 | 0.1 | 22 | 17 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| k=0.5 | 1027 | -0.34 | 0.95 | -0.49 |
| k=0.25 | 1844 | -0.90 | 0.87 | -1.77 |
| k=0 | 3045 | -1.17 | 0.83 | -3.03 |

_edge_lab run in 2.3 s_
