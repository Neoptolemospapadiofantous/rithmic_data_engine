# Monday RTH long (09:30 -> 15:55) (`monday_long_holdout`)

**Why it should work:** No one can hedge over the weekend, so funds and dealers cut exposure into Friday's close and carry less risk through it; on Monday they put it back on during the cash session (2026-10-17 behaviour map: Monday RTH +12 bp vs +3.6 bp an average session, up in 13 of 17 years). Pre-registered from that map; holdout 2025-01 on is judged once.

Market NQ · real cost 1.00 pts/contract round trip · 1 variants · data from 2025-01-01 to 2026-10-16

## Verdict: **QUICK-LOOK**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (stopk=0) net +66.30 pts/trade, t 2.15; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | FAIL — 8 out-of-sample trades, +104.78 pts/trade net, PF 2.70, t 1.22 |
| 3 Controls | skipped (--quick) |
| 4 Robustness | partial (--quick) — 3/3 checks (need 3, and the latest era positive: yes) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $0 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 5.3 trades/yr · win 62% · gross +105.78 pts · net +104.78 pts/trade = $2096 per NQ / $208.6 per MNQ
- picked blind (average variant): +838 pts total · walk-forward: +838 · hindsight best: +838
- months traded 10/10, positive 3

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2026 | 8 | +838 | +16765 |

Years positive: 1/1

## Months

- positive 3/5 (60%), since 2019 3/5 (60%); longest run without a positive month 2

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | no trades |
| era 2019-22 | no | no trades |
| era 2023-26 | yes | +104.78 pts/trade, n 8 |
| cost × 1.5 | yes | +834 pts total |
| plateau (variants net > 0) | yes | 100% of 1 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 0.3 | 1 | 1 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 0.3 | 2 | 1 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 0.3 | 2 | 1 | 0 | 0 | 0.0 | 0 | 0 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| stopk=0 | 48 | +66.30 | 2.26 | 2.15 |

_edge_lab --quick run in 0.6 s_
