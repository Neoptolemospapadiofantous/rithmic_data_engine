# Absorbed 5m bar (delta against the close), loosened threshold (`x23_absorb_loose`)

**Why it should work:** A bar whose aggressor delta pushed one way but closed the other way shows resting size absorbing the aggressors; the absorbed side must cover. Loosened so it fires at least 100 times, as the StarNet desk asked. Pre-registered 2026-10-23 (research/preregistered/2026-10-23_starnet_open_items.md X3).

Market NQ · real cost 1.00 pts/contract round trip · 2 variants · data from 2023-03-01 to 2024-12-31

## Verdict: **FAIL at CONTROLS**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (k=0.05) net +0.28 pts/trade, t 0.07; single-test t needed for 2 variants ≈ 2.24 |
| 2 Walk-forward | pass — 73 out-of-sample trades, +1.96 pts/trade net, PF 1.06, t 0.20 |
| 3 Controls | FAIL — beats random direction in 60%, random time in 78%, random day in 54% of 200 seeds (need 95% on the claimed dir + (time or day)) |
| 4 Robustness | pass — 4/5 checks (need 4, and the latest era positive: yes) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $0 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 6 months)

- 20.9 trades/yr · win 37% · gross +2.96 pts · net +1.96 pts/trade = $39 per NQ / $2.9 per MNQ
- picked blind (average variant): +202 pts total · walk-forward: +143 · hindsight best: +2201
- months traded 5/16, positive 3

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2024 | 73 | +143 | +2867 |

Years positive: 1/1

## Months

- positive 3/5 (60%), since 2019 3/5 (60%); longest run without a positive month 1

## Controls
Real net +143 pts; random direction mean -28, random time mean -282, random day mean +88.

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | no trades |
| era 2019-22 | no | no trades |
| era 2023-26 | yes | +1.96 pts/trade, n 73 |
| cost × 1.5 | yes | +107 pts total |
| entry 1 bar late | yes | +300 pts total, n 73 |
| confirm on ES | no | +0 pts total, n 0 |
| plateau (variants net > 0) | yes | 50% of 2 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 0.4 | 3 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 0.4 | 7 | 1 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 0.4 | 10 | 1 | 0 | 0 | 0.0 | 0 | 0 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| k=0.05 | 251 | +0.28 | 1.01 | 0.07 |
| k=0.02 | 381 | -0.53 | 0.98 | -0.12 |

_edge_lab run in 1.1 s_
