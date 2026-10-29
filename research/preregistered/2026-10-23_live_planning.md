# Live planning tests — EV by start year, 5-account programs under live fills, decay, daily-stop level — pre-registered 2026-10-23

Founder: "do more tests". Written BEFORE any run. Candidate = BOOK2 + gap sizing + 15m trend filter; LIVE = the same with pessimistic
fills (flag_realism G1all: entry 1 tick through, target 1 tick through, stops slip 2 ticks). Account = `flag_prop_ev … mtm`, conservative
marks, 2 open per market, $500 daily stop unless stated. `flag_prop_ev` gains one additive output line per plan (`YEAR|`: EV per eval by
start year); existing outputs are unchanged.

## H1 — EV per eval by start year (descriptive)
Topstep / Lucid / Select Daily / Builder 25K, candidate and LIVE: EV per eval for starts in each year 2014 … 2025; min, median, max, and
the number of start years with EV below $150 (roughly an eval fee). Read: how often a whole year of starts would not pay for itself.

## H2 — 5-account programs under live fills (descriptive)
LIVE, FPEV_MTM_PROG = 300: five accounts started 0 / 1 / 5 / 10 / 20 trading days apart, each bought once; share of programs where at least
one account paid >= $300 (before the split), average total paid after the 80 % split, share that got >= $250 back. Same four plans.

## H3 — Is the edge decaying? (descriptive with one test)
Candidate trades (NQ + ES, BOOK2 rows of the candidate files): mean net R per calendar year 2014 … 2026; OLS slope of yearly mean R on year
with its t (13 points); last 12 months' mean R and its percentile among all rolling 12-month windows. Warning (not a pass/fail) if the
slope is negative with t < −2, or the last 12 months sit in the bottom 10 %.

## H4 — Daily stop level under live fills
LIVE with daily stop $300 / $400 / $500 / $750 / $1,000. The level with the best average EV across the 3 plans on 2014-19 is CHOSEN; it
replaces $500 only if it beats $500 on 2020-25 on 2 of 3 plans AND with +1 pt. Otherwise $500 stays.

## Results (run 2026-10-23, after the text above)
Output: `research/programs/out/live_planning_mtm.txt`. The added YEAR line leaves every existing number unchanged (candidate PLAN rows
identical to before).

**H1 — EV per eval by start year (conservative; LIVE = pessimistic fills):**
| plan | candidate min / median / max | LIVE min / median / max | start years < $150 (cand / LIVE) |
|---|---|---|---|
| Topstep 50K | 0 / 1,574 / 6,343 | 0 / 1,340 / 4,860 | 3 / 3 of 12 |
| Lucid 50K | 0 / 1,399 / 4,218 | 0 / 1,282 / 3,336 | 3 / 3 |
| Select Daily 50K | 0 / 1,125 / 5,705 | 0 / 1,032 / 5,428 | 4 / 4 |
| Builder 25K | 0 / 279 / 935 | 0 / 199 / 860 | 5 / 5 |
Weak start years: 2016, 2018, 2019 (0 on every plan), 2020 (~$150-350). 2021-25 are all strong. One year in four, a whole year of starts
would not have paid for its evals; 2017 alone (≈ $5-6K) lifts the average — the median is the honest planning figure.

**H2 — 5-account programs, LIVE fills, P(at least one account paid >= $300 before the split):**
| plan | same day | 1 day apart | 5 days | 10 days | 20 days | avg paid after split |
|---|---|---|---|---|---|---|
| Topstep 50K | 28.9 % | 32.1 % | 39.2 % | 45.3 % | 56.7 % | ~$5,250 |
| Lucid 50K | 28.9 % | 32.1 % | 39.2 % | 45.3 % | 56.7 % | ~$4,340 |
| Select Daily 50K | 20.8 % | 24.7 % | 32.4 % | 37.5 % | 46.8 % | ~$4,950 |
| Builder 25K | 16.2 % | 20.4 % | 29.7 % | 39.2 % | 51.3 % | ~$1,150 |
Spacing the five starts ~20 trading days apart roughly doubles the chance that at least one pays. The average is pulled up by the good
programs; even at 20 days apart, 43-53 % of programs have no account reaching $300.

**H3 — decay:** yearly mean R 2014 +0.30, 2015 +0.22, 2016 +0.02, 2017 +0.33, 2018 +0.16, 2019 −0.17, 2020 +0.14, 2021 +0.33,
2022 +0.36, 2023 +0.27, 2024 +0.48, 2025 +0.40, 2026 (to 10-15, 90 trades) +0.16. Slope +0.014 R/yr, t +1.12 — no decay. Last 12 months
+0.34 R on 282 trades, 65th percentile of 137 rolling windows. No warning.

**H4 — daily stop under LIVE fills (2014-19 → 2020-25, +1 pt 2020-25):**
| stop | Topstep | Lucid | Select Daily | avg 2014-19 |
|---|---|---|---|---|
| $300 | 965 → 552 (453) | 729 → 492 (421) | 562 → 363 (314) | 752 |
| $400 | 1230 → 930 (827) | 961 → 815 (786) | 994 → 648 (601) | 1,062 |
| $500 | 1091 → 1557 (1306) | 808 → 1398 (1187) | 1132 → 1299 (1051) | 1,010 |
| $750 | 1169 → 1692 (1421) | 896 → 1493 (1300) | 1081 → 1557 (1375) | 1,049 |
| **$1,000** | 1201 → 1601 (1344) | 932 → 1410 (1228) | 1115 → 1483 (1306) | **1,083** |
Chosen on 2014-19: $1,000. On 2020-25 it beats $500 on 3 of 3 plans and with +1 pt → PASS by the rule: the daily stop moves $500 →
$1,000. Caveats: (1) the 2014-19 differences between $400 and $1,000 are small (1,010-1,083), so the choice is noisy — what is clear is
that $300 is too tight and $400 falls behind after 2019; (2) $1,000 equals some plans' own daily loss limit, so a live day that slips past the stop can breach the
plan's rule — set the bot's stop a little under the plan's limit (e.g. $900 where the limit is $1,000) and confirm each plan's rule first.

## Follow-up (founder: "so 50k" / "how many accounts do we need"; descriptive, run after the above)
`flag_prop_ev` gains env FPEV_MTM_PROGN (accounts per program, default 5 — default output unchanged). LIVE fills, daily stop $1,000,
accounts started 20 trading days apart, each bought once; P(at least one account paid >= $300 before the split) and average total paid
after the 80 % split (`out/live_planning_naccounts.txt`):
| accounts | Topstep 50K | Lucid 50K (Flex model) | Select Daily 50K |
|---|---|---|---|
| 1 | 31.7 % · $1,099 | 31.7 % · $902 | 23.4 % · $1,052 |
| 2 | 41.2 % · $2,165 | 41.2 % · $1,783 | 33.7 % · $2,060 |
| 3 | 48.1 % · $3,225 | 48.1 % · $2,659 | 40.8 % · $3,043 |
| 5 | 61.1 % · $5,303 | 61.1 % · $4,369 | 53.8 % · $4,930 |
| 8 | 75.7 % · $8,491 | 75.7 % · $6,986 | 68.4 % · $7,834 |
| 10 | 82.7 % · $10,662 | 82.7 % · $8,763 | 75.4 % · $9,837 |
Accounts are not independent (a bad year hurts all of them): 10 accounts give 83 %, not the 98 % independence would suggest.

## H5 — eval size × funded size (founder: "what is the best sizing we can do"; written BEFORE the run)
LIVE fills, daily stop $1,000, gap sizing kept (the size column doubles on gap-with flags). Eval multiplier Q ∈ {1, 2, 3} × funded
multiplier QF ∈ {1, 2, 3} (micros per market = multiplier × the flag's size). The cell with the best average EV across Topstep / Lucid /
Select Daily on 2014-19 is CHOSEN; it replaces (1, 1) only if it beats (1, 1) on 2020-25 on 2 of 3 plans AND with +1 pt.

### H5 results (`out/live_planning_sizing.txt`; per eval 2014-19 → 2020-25, +1 pt 2020-25, pass / paid %)
| eval × funded | Topstep | Lucid | Select Daily | avg 2014-19 |
|---|---|---|---|---|
| **1 × 1** | 1201 → 1601 (1344) 46 / 32 % | 932 → 1410 (1228) 46 / 32 % | 1115 → 1483 (1306) 36 / 23 % | **1,083** |
| 1 × 2 | 1565 → 2334 (2077) 46 / 26 % | 786 → 1486 (1224) 46 / 26 % | 851 → 1307 (1137) 36 / 20 % | 1,067 |
| 1 × 3 | 1686 → 1206 (1224) 46 / 19 % | 745 → 813 (663) | 723 → 905 (888) | 1,051 |
| 2 × 1 | 775 → 1040 (862) 31 / 19 % | 611 → 883 (774) | 781 → 927 (832) | 722 |
| 2 × 2 | 1141 → 1537 (1328) 31 / 16 % | 569 → 966 (741) | 603 → 838 (811) | 771 |
| 2 × 3 / 3 × any | all lower | | | 451-761 |
Chosen on 2014-19: 1 × 1 → it stays (nothing to replace). Bigger eval size passes faster (22 vs 30 days) but passes far less often
(31 % vs 46 %). Funded × 2 lifts Topstep only (EV 2,334 vs 1,601 — Topstep's funded rules reward size) while fewer accounts get paid
(26 % vs 32 %) and Lucid / Select gain nothing; a Topstep-only option, not adopted by the rule.

## H6 — high win rate, small targets (founder: "if we switch to a high WR small wins how faster would be to pass and get a payout";
written BEFORE the run)
Same candidate trades and entries, LIVE fills (entry 1 tick through, target 1 tick through, stops slip 2 ticks), target = entry + k × risk
with k ∈ {0.33, 0.5, 0.75, 1.0, 1.5} instead of the measured move (pole top + pole), stop unchanged, flat 15:55; daily stop $1,000.
Report win rate, EV per eval, pass %, paid %, median days to pass and to the first payout. Chosen on 2014-19 (best average EV across
Topstep / Lucid / Select Daily); replaces the measured move only if it beats it on 2020-25 on 2 of 3 plans AND with +1 pt.

### H6 results (`out/live_planning_highwr_mtm.txt`; LIVE fills, $1,000 stop; trade stats 2014-26, 3,761 trades)
| target | win rate | avg win / avg loss | mean R | Topstep EV 14-19 → 20-25 · pass · days to pass / pay | Select Daily EV 20-25 |
|---|---|---|---|---|---|
| measured move (current) | 34.3 % | +2.78 / −1.16 | **+0.186** | 1201 → 1601 · 46 % · 30 / 57 d | 1,483 |
| 0.33R | 73.0 % | +0.21 / −1.06 | −0.133 | 0 → 0 · 0 % | 0 |
| 0.5R | 69.8 % | +0.37 / −1.19 | −0.101 | 0 → 0 · 0 % | 0 |
| 0.75R | 61.2 % | +0.62 / −1.19 | −0.083 | 25 → 4 · 3 % · 49 / 85 d | 0 |
| 1R | 55.3 % | +0.87 / −1.19 | −0.051 | 124 → 55 · 10 % · 43 / 75 d | 27 |
| 1.5R | 47.0 % | +1.34 / −1.17 | +0.006 | 377 → 361 · 27 % · 41 / 67 d | 205 |
Chosen on 2014-19: the measured move (every small target is lower) → no change. Small targets RAISE the win rate (up to 73 %) but every
one below 1.5R loses money after costs and slippage: the flag's profit is in the few big runs, and cutting them leaves the stop-outs
(which slip) larger than the wins. They also pass SLOWER (41-49 days vs 30), because a negative-expectancy book rarely reaches the target.
