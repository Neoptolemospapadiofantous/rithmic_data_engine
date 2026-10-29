# Rotation in groups started two weeks apart, Lucid Pro 50K, paid odds — pre-registered 2026-10-24

Founder: "paid not funded" → "rotate accounts 2 weeks apart in groups". Written BEFORE the run. New program
`research/programs/rotation_groups.cpp` (the 2026-10-23 rotation / payout_speed simulator was not kept in the repo).

Trades: the flag candidate (BOOK2 + gap sizing + 15m filter) with LIVE fills (flag_realism G1all), NQ + ES, 1 MNQ / 1 MES per unit
(gap-with flags 2), at most 2 open per market. Cascade NOT included (flags only). $ = net points × $2 (MNQ) / $5 (MES) × size.
Lucid Pro 50K, rules as recorded 2026-10-23 (memory project_prop_firm_rules_2026-10): eval target +$3,000, $2,000 EOD trailing max
loss, one-day pass, no consistency; funded: $2,000 EOD trailing that locks at +$100 once the balance reaches +$2,100, first payout
when profit ≥ $2,600 ($2,100 buffer + $500 cycle) and the best day ≤ 40 % of the profit, paid = min($2,000, profit − $2,100) × 90 %.
Fee $50 per account (coupon). Only the FIRST payout of each account is counted.

Rotation inside a group: one account trades at a time (eval and funded alike); a trade belongs to the account active at its entry;
when the active account's day P&L reaches +$750 (lock) or −$1,000 (its daily stop) it stops for the day and the next live account in the
group takes the next flags. A failed account leaves the group (not replaced).
Groups: G groups of k accounts; group j starts 10 trading days (2 weeks) after group j−1. Configurations: total 4 accounts (4×1, 2×2,
1×4), 6 accounts (6×1, 3×2, 2×3, 1×6), 8 accounts (4×2, 2×4); plus 4 and 6 accounts with groups 5 sessions apart (1 week) for
comparison. Programs start every 5th session 2014-01 → 2025-04, followed for 160 sessions from the first start.
Reported: P(at least one account paid), P(paid within 30 / 45 / 60 / 90 sessions), median sessions to the first payout, paid accounts
per program, expected total first payouts net, fees, net = payouts − fees; for all starts and for starts 2020-25 separately.
Descriptive: no configuration is adopted from this file; it answers how many accounts and how to group them.

## Results (run 2026-10-24, after the text above; `research/programs/out/rotation_groups.txt`)
Correction made before reading: the first run booked raw points; trades are now scaled to today's ATR exactly as flag_prop_ev does
(otherwise 2014-19 trades are ~4× too small). Each group gets its own 160 sessions (the plan said 160 from the first start).
| groups × accounts, apart | P(paid) | ≤ 30 / 45 / 60 / 90 sessions | median 1st payout | paid per program | E[payouts] | fees | net | starts 2020-25: P(paid) / net |
|---|---|---|---|---|---|---|---|---|
| **4 × 1, 2 weeks** | **46.1 %** | 1 / 4 / 11 / 28 % | 84 | 1.35 | $2,254 | $200 | **+$2,054** | 59.4 % / +$2,650 |
| 2 × 2, 2 weeks | 39.9 % | 0 / 0 / 1 / 7 % | 119 | 0.84 | $1,381 | $200 | +$1,181 | 48.0 % / +$1,231 |
| 1 × 4 | 6.3 % | 0 / 0 / 0 / 1 % | 140 | 0.07 | $102 | $200 | −$98 | 4.4 % / −$140 |
| **6 × 1, 2 weeks** | **53.5 %** | 1 / 4 / 11 / 28 % | 89 | 2.03 | $3,393 | $300 | **+$3,093** | 69.3 % / +$4,094 |
| 3 × 2, 2 weeks | 44.7 % | 0 / 0 / 1 / 8 % | 120 | 1.30 | $2,138 | $300 | +$1,838 | 54.6 % / +$1,950 |
| 2 × 3, 2 weeks | 30.7 % | 0 / 0 / 0 / 3 % | 129 | 0.62 | $1,063 | $300 | +$763 | 29.7 % / +$654 |
| 1 × 6 | 0.8 % | — | 150 | 0.01 | $7 | $300 | −$293 | 0 % |
| 4 × 2, 2 weeks | 48.6 % | 0 / 0 / 1 / 8 % | 121 | 1.76 | $2,914 | $400 | +$2,514 | 59.4 % / +$2,700 |
| 2 × 4, 2 weeks | 10.0 % | — | 147 | 0.16 | $243 | $400 | −$157 | 9.2 % |
| 4 × 1, 1 week | 40.7 % | 1 / 5 / 11 / 27 % | 80 | 1.36 | $2,268 | $200 | +$2,068 | 52.4 % / +$2,638 |
| 6 × 1, 1 week | 44.7 % | 1 / 5 / 11 / 28 % | 82 | 2.03 | $3,388 | $300 | +$3,088 | 57.6 % / +$3,967 |
| 3 × 2, 1 week | 40.3 % | 0 / 0 / 1 / 8 % | 119 | 1.26 | $2,072 | $300 | +$1,772 | 48.5 % / +$1,850 |
Read: with the flag book alone (~1.4 trades a day) and the paid (not funded) yardstick, ROTATION INSIDE A GROUP HURTS: the accounts take
turns, so each one collects its ~$5,600 (eval $3,000 + funded $2,600) several times slower, and the Lucid Pro buffer means slow = blown or
unpaid inside 160 sessions. The more accounts per group, the worse (1 × 6: 0.8 %). Separate accounts that each trade every flag, started two
weeks apart, are best on every column; two weeks beats one week. The 2026-10-23 finding (rotation raises P(funded)) was about getting FUNDED
with a lock-driven eval; it does not carry to getting PAID on Lucid Pro with flags only. Not tested here: rotation with the cascade added
(more trades per day), and a longer horizon.
