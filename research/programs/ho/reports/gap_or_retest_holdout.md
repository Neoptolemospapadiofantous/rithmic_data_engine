# Gap direction set, then a retest of the opening range (`gap_or_retest_holdout`)

**Why it should work:** The overnight gap is repositioning that tends to continue into the cash session (gap-agree cascade). The 15-minute opening range edge holds on the retest. Pre-registered 2026-10-17 (research/preregistered/2026-10-17_direction_sources.md).

Market NQ · real cost 1.00 pts/contract round trip · 1 variants · data from 2025-01-01 to 2026-10-16

## Verdict: **QUICK-LOOK**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (stopk=0.35) net +7.15 pts/trade, t 0.60; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | FAIL — 20 out-of-sample trades, +17.02 pts/trade net, PF 1.28, t 0.51 |
| 3 Controls | skipped (--quick) |
| 4 Robustness | partial (--quick) — 3/3 checks (need 3, and the latest era positive: yes) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $0 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 13.3 trades/yr · win 50% · gross +18.02 pts · net +17.02 pts/trade = $340 per NQ / $33.0 per MNQ
- picked blind (average variant): +424 pts total · walk-forward: +340 · hindsight best: +424
- months traded 4/10, positive 2

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2026 | 20 | +340 | +6808 |

Years positive: 1/1

## Months

- positive 2/3 (67%), since 2019 2/3 (67%); longest run without a positive month 1

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | no trades |
| era 2019-22 | no | no trades |
| era 2023-26 | yes | +17.02 pts/trade, n 20 |
| cost × 1.5 | yes | +330 pts total |
| plateau (variants net > 0) | yes | 100% of 1 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 0.2 | 1 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 0.2 | 3 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 0.2 | 2 | 0 | 0 | 0 | 0.0 | 0 | 0 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| stopk=0.35 | 135 | +7.15 | 1.13 | 0.60 |

_edge_lab --quick run in 0.7 s_
