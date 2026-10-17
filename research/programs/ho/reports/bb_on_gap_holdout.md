# Break → box → go: overnight high / low — only on the gap side (`bb_on_gap_holdout`)

**Why it should work:** Breakout traders who were right add after a pause; the pause candle (the box) holds liquidity just beyond the level, and its break shows the level has flipped. Pre-registered 2026-10-17 (research/preregistered/2026-10-17_break_box.md). Gap-side follow-up, pre-registered 2026-10-17.

Market NQ · real cost 1.00 pts/contract round trip · 1 variants · data from 2025-01-01 to 2026-10-16

## Verdict: **QUICK-LOOK**

| gate | result |
|---|---|
| 1 Screen | pass — best variant (r=0.75 rr=2) net +8.02 pts/trade, t 0.40; single-test t needed for 1 variants ≈ 1.96 |
| 2 Walk-forward | FAIL — 0 out-of-sample trades, +0.00 pts/trade net, PF 0.00, t 0.00 |
| 3 Controls | skipped (--quick) |
| 4 Robustness | partial (--quick) — 1/2 checks (need 2, and the latest era positive: NO) |
| 5 Prop fit | FAIL — select_daily_50k: best break-even eval fee $0 (need > $150) |

## Walk-forward (each month uses the variant best over the previous 12 months)

- 0.0 trades/yr · win 0% · gross +0.00 pts · net +0.00 pts/trade = $0 per NQ / $-3.0 per MNQ
- picked blind (average variant): +0 pts total · walk-forward: +0 · hindsight best: +0
- months traded 0/0, positive 0

| year | trades | net pts | $ per NQ |
|---|---|---|---|

Years positive: 0/0

## Months

- positive 0/0 (0%), since 2019 0/0 (0%); longest run without a positive month 0

## Robustness

| check | ok | detail |
|---|---|---|
| era 2010-18 | no | no trades |
| era 2019-22 | no | no trades |
| era 2023-26 | no | no trades |
| cost × 1.5 | no | +0 pts total |
| plateau (variants net > 0) | yes | 100% of 1 |

## Prop (select_daily_50k, MNQ at 1.50 pts cost; eval → funded until breach → new eval)

| MNQ | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |
|---|---|---|---|---|---|---|---|---|
| 2 | 0.0 | 0 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 4 | 0.0 | 0 | 0 | 0 | 0 | 0.0 | 0 | 0 |
| 6 | 0.0 | 0 | 0 | 0 | 0 | 0.0 | 0 | 0 |

## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)

| variant | trades | net pts/trade | PF | t |
|---|---|---|---|---|
| r=0.75 rr=2 | 22 | +8.02 | 1.24 | 0.40 |

_edge_lab --quick run in 0.8 s_
