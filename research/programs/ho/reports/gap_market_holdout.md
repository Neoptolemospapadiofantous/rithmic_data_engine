# Gap direction set, market entry at 09:35 (benchmark) (`gap_market_holdout`)

**Why it should work:** The overnight gap is repositioning that tends to continue into the cash session (gap-agree cascade). The plain entry the patterns must beat. Pre-registered 2026-10-17 (research/preregistered/2026-10-17_direction_sources.md).

Market NQ · real cost 1.00 pts/contract round trip · 1 variants · data from 2025-01-01 to 2026-10-16

## Verdict: **QUICK-LOOK**

| gate | result |
|---|---|
| 1 Screen | FAIL — best variant (stopk=0.25) net -3.14 pts/trade, t -0.39; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | FAIL — 36 out-of-sample trades, -11.19 pts/trade net, PF 0.81, t -0.56 |
| 3 Controls | skipped (--quick) |
| 4 Robustness | partial (--quick) — 0/3 checks (need 3, and the latest era positive: NO) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $0 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 24.0 trades/yr · win 31% · gross -10.19 pts · net -11.19 pts/trade = $-224 per NQ / $-23.4 per MNQ
- picked blind (average variant): -708 pts total · walk-forward: -403 · hindsight best: -708
- months traded 4/10, positive 1

| year | trades | net pts | $ per NQ |
|---|---|---|---|
| 2026 | 36 | -403 | -8057 |

Years positive: 0/1

## Months

- positive 1/3 (33%), since 2019 1/3 (33%); longest run without a positive month 2

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | no trades |
| era 2019-22 | no | no trades |
| era 2023-26 | no | -11.19 pts/trade, n 36 |
| cost × 1.5 | no | -785 pts total |
| plateau (variants net > 0) | no | 0% of 1 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 0.2 | 3 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 0.2 | 5 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 0.2 | 9 | 2 | 0 | 0 | 0.0 | 0 | 0 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| stopk=0.25 | 215 | -3.14 | 0.94 | -0.39 |

_edge_lab --quick run in 0.6 s_
