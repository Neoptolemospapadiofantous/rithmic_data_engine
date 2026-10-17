# Gap direction set, then a new day extreme (`gap_breakout_holdout`)

**Why it should work:** The overnight gap is repositioning that tends to continue into the cash session (gap-agree cascade). A fresh high (low) in that direction. Pre-registered 2026-10-17 (research/preregistered/2026-10-17_direction_sources.md).

Market NQ · real cost 1.00 pts/contract round trip · 1 variants · data from 2025-01-01 to 2026-10-16

## Verdict: **QUICK-LOOK**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (stopk=0.35) net +1.01 pts/trade, t 0.09; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | FAIL — 10 out-of-sample trades, +27.25 pts/trade net, PF 1.38, t 0.47 |
| 3 Controls | skipped (--quick) |
| 4 Robustness | partial (--quick) — 3/3 checks (need 3, and the latest era positive: yes) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $0 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 6.7 trades/yr · win 50% · gross +28.25 pts · net +27.25 pts/trade = $545 per NQ / $53.5 per MNQ
- picked blind (average variant): +469 pts total · walk-forward: +273 · hindsight best: +469
- months traded 3/10, positive 1

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2026 | 10 | +273 | +5450 |

Years positive: 1/1

## Months

- positive 1/2 (50%), since 2019 1/2 (50%); longest run without a positive month 1

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | no trades |
| era 2019-22 | no | no trades |
| era 2023-26 | yes | +27.25 pts/trade, n 10 |
| cost × 1.5 | yes | +268 pts total |
| plateau (variants net > 0) | yes | 100% of 1 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 0.1 | 2 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 0.1 | 2 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 0.1 | 3 | 0 | 0 | 0 | 0.0 | 0 | 0 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| stopk=0.35 | 148 | +1.01 | 1.02 | 0.09 |

_edge_lab --quick run in 0.7 s_
