# The prop plan against reality: Monte Carlo + measured live costs (2026-10-16)

> **CORRECTION (same day):** the "4.6 ticks / 2.4 pts per MNQ" below read `entry_slippage_ticks`, which is UNSIGNED
> (|fill − signal|, latency_logger.hpp). Signed, from the stored prices (`scripts/execution_costs.sh`): MNQ entries
> ≈ 1 tick BETTER than the signal on average (median 0), stop exits ≈ 3 ticks worse, commission 0.91 pts → a real round
> trip ≈ **1.3 pts per MNQ** — at or below the 1.5 every edge_lab result already uses. The 2.4-pt tables below are too
> pessimistic; the 1.5-pt rows (first Monte Carlo, and the re-run at the end) are the ones to plan with.

## Measured live execution (live_trades, account tradeify, 45 real trades 2026-09-21 … 10-09, ORB + rota) — SUPERSEDED, see the correction above
- Entry slippage vs the trigger price: **4.6 ticks average, median 4** (≈ 1.15 pts); stop exits ≈ 1.4 ticks (0.35 pts).
- Signal → order submit 0.1 ms; submit → fill **median 1.2 s**, average 10.6 s (a few very slow fills).
- Real round trip ≈ **2.4 pts per MNQ** (slippage + $1.82 commission) vs the 1.5 the research assumed; ≈ 1.6 per NQ
  vs 1.0.
- At the measured cost (2010–26 walk-forward): cascade **+4.38 pts/trade** (was +4.80; random-day control 94 % — just
  under the 95 % bar), weak-day **+14.0** (passes). Edges survive; the cascade's margin shrinks.

## Monte Carlo of year 1 (10,000 synthetic years from real 2019–26 calendar months, Select Daily 50K, eval → funded with
## one withdrawal a month → new eval after a breach)
At the measured 2.4 pts/MNQ:
| setup | 1st eval passes | any payout in year 1 | first payout (median day) | months paid | paid in year 1: mean / worst 10 % / best 10 % | evals bought (mean / worst 10 %) |
|---|---|---|---|---|---|---|
| cascade 2 MNQ | 46 % | 49 % | 136 | 1.7 | $1,165 / $0 / $3,852 | 3.2 / 5 |
| cascade 4 MNQ | 31 % | 71 % | 96 | 2.7 | $2,439 / $0 / $6,778 | 8.1 / 13 |
| cascade + VWAP/stall 2 MNQ | 64 % | 58 % | 141 | 2.2 | $1,540 / $0 / $4,588 | 2.0 / 3 |
| cascade + VWAP/stall 4 MNQ | 45 % | 73 % | 101 | 3.1 | $2,871 / $0 / $7,616 | 4.7 / 8 |
| one account: cascade 4 + weak-day 1 | 42 % | 75 % | 99 | 3.2 | $3,059 / $0 / $8,113 | 5.0 / 8 |
| two accounts: cascade 4 / weak-day 1 | 65 % | 77 % | 103 | 3.3 | $3,043 / $0 / $7,848 | 6.4 / 10 |

Reading:
- Year 1 is much weaker than the continuous 2019–26 replay (60 % of months paid there): the first ~3–5 months go to
  passing the eval and building the +$2,100 cushion, and 1 year in 10 pays nothing at any size.
- Net of evaluation fees, year 1 at 4 MNQ with the VWAP/stall exit ≈ $2,871 − 4.7 × (eval fee). Every eval-fee dollar
  matters: at $150/eval that is ≈ $2,170; at $300 ≈ $1,460.
- 2 MNQ buys fewer evals (2.0) but pays less and later; 4 MNQ pays more often but blows more evals.
- Two strategies on one account ≈ two accounts in money; two accounts pass the first eval more often but buy more evals.
- The biggest lever left is execution: ~0.9 pts/MNQ of the 2.4 is slippage from entering at market after the signal.

## Fastest first payout (founder: "we want to make the payouts as fast as possible")
Same Monte Carlo (10,000 years, real 2019–26 months, cascade + VWAP/stall exit, 2.4 pts/MNQ), withdrawing as soon as the
plan allows, evaluation size and funded size chosen separately:
| plan | eval MNQ → funded MNQ | first payout ≤ 2 months | ≤ 3 months | within the year | median | evals until it |
|---|---|---|---|---|---|---|
| Select Daily 50K | 4 → 4 | 8 % | 19 % | 74 % | 20.6 wks | 2.7 |
| Select Daily 50K | 8 → 4 | 11 % | 23 % | 78 % | 19.0 wks | 4.3 |
| **Select Flex 50K** | **6 → 2** | **10 %** | **29 %** | **92 %** | **17.0 wks** | **2.9** |
| **Select Flex 50K** | **8 → 2** | 12 % | 31 % | 93 % | 16.4 wks | 3.5 |
| Select Flex 50K | 6 → 2, 3 accounts a week apart | 12 % | 32 % | 93 % | 16.2 wks | 2.8 |
| Select Flex 25K | 6 → 1 | 12 % | 30 % | 93 % | 16.6 wks | 4.7 |
| Select Daily 25K | 6 → 2 | 10 % | 21 % | 77 % | 19.6 wks | 6.0 |
Reading: **Flex beats Daily for speed and reliability; size up in the evaluation (6–8 MNQ — a failed eval only costs the
fee) and trade small funded (2 MNQ — a breach loses the account).** Median first payout ≈ 16–17 weeks, ~30 % within
3 months, ~92 % within the year. 25K is not faster and burns more evals. Staggering three accounts barely helps (the
same trades). The earlier "~7 weeks on Flex" (2026-10-15) assumed lower costs; at the measured 2.4 pts/MNQ it is
~16 weeks. What is left to speed it: lower slippage (the executor reading the tick stream / limit entries) and more
trading days per account.
Cross-check on the REAL 2019–26 sequence (a fresh account chain started on every trading day, horizon 252 days):
Flex 50K eval 8 → funded 2: first payout median 83 trading days (16.6 wks), quartiles 59 / 128, 96 % within a year,
26 % within 60 days; eval 6 → funded 1: median 80 days but only 87 % within a year; Daily 50K eval 6 → funded 2: median
121 days, 68 %. The Monte Carlo and the real sequence agree; the 2026-10-15 "~7 weeks" was optimistic.

## Re-run at the corrected cost (1.5 pts/MNQ)
Fastest first payout: Flex 50K eval 8 → funded 2 MNQ — Monte Carlo median 16.0 weeks (33 % within 3 months, 95 % within
the year, 3.3 evals); real 2019–26 sequence median 82 trading days (16.4 wks), 28 % within 60 days, 100 % within 252 days.
Daily 50K: ~23–25 weeks. Year 1 (monthly withdrawal, Daily 50K): cascade + VWAP/stall 4 MNQ — any payout 77 %, first ~day
99, $3,263 mean (worst 10 % $0), 4.3 evals. Execution cost barely moves payout speed — the edge size and the plan's rules do.
