# Pre-registration — the Vest plans on a FUNDED $5k account (written 2026-10-19, BEFORE any run)

Assumption (not verified for funded accounts): same rules as the evaluation — static floor $300 below the starting balance, $150 daily
loss, 80 % split, Claim Profit any time (claiming withdraws the profit, the floor stays). Policy: claim everything above the start
whenever profit ≥ X ∈ {$100, $250, $500}. Each account starts on a session 2019-01 → 2026, runs until it breaches or the data ends.
Plans (each trade sized to its own stop, lenient daily-loss model): FW, FW+W2, FW+M, FW+V, FW+W2+M+V; risk $40 / $50 / $75.
Measured: mean $ paid to the trader per funded account, share breached, median trading days alive; then per evaluation fee:
pass rate × mean paid − $40. The plan with the highest value per fee at its best (risk, X) is the funded recommendation; report it next
to FW's figure. Description-level: no hold-out, 2019+ only.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (scratchpad wl/fund_out*.txt). Value per $40 fee = eval pass × mean $ paid − 40.
Pre-registered grid (risk $40/50/75 × claim $100/250/500): best = **FW at $40, claim ≥ $500: pass 75.3 %, $1,364 paid per funded
account, value $988 per fee**; FW+W2 $821, FW+all $801, FW+M $740, FW+V $681; every $75 cell < $100 (accounts last ~8 days).
→ the adds do not help funded either; FW stays the plan. Higher claim thresholds pay more in every plan (the profit left in the
account is the buffer against the static floor).
Extension (description): FW $20 claim ≥ 500: pass 96.3 % (86 trade days), $2,550/account, 27 % breached by the data end, value $2,417,
$976 per account-year · $30 claim ≥ 1000: 85.5 % (53 days), $2,837, value $2,386, $1,560/yr · $40 claim ≥ 2000: 75.3 % (31), $2,837,
$2,098, $1,991/yr · $50 claim ≥ 500: 66.6 % (20), $802, $495, $3,472/yr. Small risk = most money per fee but slow; larger risk = more
per account-year but the account dies in weeks. Caveats: funded rules assumed equal to the eval's; 2019–26 only (the book's good era).

## Stress tests, pre-registered 2026-10-19 BEFORE running
The recommendation ($30 risk, claim ≥ $1,000; alternatives $20 / $500 and $40 / $2,000) re-run (a) on accounts started 2010-07 →
2018-12 (data cut at 2018-12-31) and (b) at 3 and 4 pts cost per trade (2019+). The recommendation stands only if value per fee
stays > 0 in (a) and > $500 at 3 pts in (b); otherwise the summary says it depends on the 2019+ era / on low costs.
Results of the stress tests (2026-10-19):
(a) 2010–18 starts at a FIXED 2 pts: $20/500 pass 36.9 %, value +$11 per fee · $30/1000 42.2 %, +$7 · $40/2000 41.7 %, −$26 → stays > 0
    only by a hair at $20 / $30: **at fixed costs the funded plan depends on the 2019+ era.**
(b) 2019+ at 3 pts: $20 93.9 % / +$2,017 · $30 81.7 % / +$1,906 · $40 70.6 % / +$1,791; at 4 pts: +$1,682 / +$1,381 / +$1,216 → holds.
Description (not pre-registered): 2010–18 ATR averaged 48 pts vs 330 in 2025–26, so a fixed 2 pts was ~7× today's cost relative to
the move. With cost scaled to today's ratio (cost × ATR / 320): 2010–18 $20/500 83.4 %, +$873 · $30/1000 67.8 %, +$746 · $40/2000
62.5 %, +$810; at 4 pts scaled +$547 / +$575 / +$589. The 2010–18 weakness of the book is largely the cost share of a small-range
market; at today's price level the plan is positive in both eras. (Ledger rule unchanged: research keeps the fixed real cost.)

## Full port, pre-registered 2026-10-19 BEFORE running (founder: "what about full porting")
Full port = the day's FIRST book trade only at $120 / $145 risk (2R, or 3.33R = the $500 goal at $150). Three ways, 2019+, 2 and
3 pts: (1) full port in eval AND funded (claim ≥ $500 / $1,000); (2) full port in the eval, then FW at $30 claim ≥ $1,000 funded;
(3) FW at $30 throughout. Value per $40 fee = eval pass × mean funded paid − 40; also median days to pass. Full port is
recommended only where its value per fee beats (3) at both costs.
Results of the full-port test (2026-10-19; 2 pts / 3 pts):
(1) Full port in eval AND funded: $120 claim ≥ 1000 value +$229 / +$203 per fee (funded accounts live 15–17 days); $145 (2R or 3.33R)
    −$1 … −$21 (funded accounts die in 3 days, 100 % breached). → never.
(2) Full port in the eval, then FW $30 claim ≥ 1000 funded: $120 first trade: pass 44.5 % / 42.8 % in a median 9 trade days →
    0.445 × 2,837 − 40 = **+$1,222** / 0.428 × 2,382 − 40 = **+$980** per fee; $145: 24.5 % in 5 days → +$655 / +$534.
(3) FW $30 throughout: 85.5 % / 81.7 % in 53–56 days → **+$2,386 / +$1,906** per fee.
Verdict by the rule: full port does NOT beat (3) per fee. Description: per FUNDED account the two routes are almost equal — fees per
pass $90 (full port $120) vs $47 ($30), against ~$2,400–2,800 paid — and the full-port eval reaches funded ~44 trade days sooner.
If funded slots (max 10) or time are the constraint rather than fees, (2) at $120 is the faster route; never $145.

## Buffer claims and room-based sizing, pre-registered 2026-10-19 BEFORE running
(a) Funded FW: claim when profit ≥ T, withdraw down to a buffer B (T ∈ {1000, 1500, 2000}, B ∈ {0, 250, 500}), risk $30 fixed;
    and room-based funded risk = clamp((300 + profit) / D, $20, $80), D ∈ {10, 15}, with the best (T, B).
(b) Eval: risk each day = min(Rmax, (300 + profit) / D) — Rmax ∈ {$120} on the first trade only (B1) and {$50} on FW, D ∈ {2.5, 4} —
    vs the fixed $120 / $30. Measured: pass %, median days, then value per fee with the funded policy from (a).
2019+, 2 pts and 3 pts. A change is recommended only if better at both costs.
Results (2026-10-19; 2 pts / 3 pts):
(a) Funded FW $30, claim at T down to buffer B — paid per account (breached by the data end): T1000 B0 $2,837 (67 %) / $2,382 (63 %);
    **T1000 B250 $3,365 (35 %) / $2,857 (36 %)**; T1000 B500 $3,190 (13 %) / $2,859 (17 %); T1500 / T2000 lower. Per account-year B250 is a
    little lower ($1,404 vs $1,560) because accounts live longer (and many are still alive at the data end, so per-account $ is a floor).
    → **claim at $1,000, leave $250 in: ADOPTED** (better per account at both costs, breaches halved).
    Room-based funded risk (300+profit)/10 or /15, $20–80: $313 / $1,354 (2 pts), $489 / $1,741 (3 pts) → worse; keep $30 fixed.
(b) Eval room-based risk: without a size floor the sim showed 99.9 % pass — an artefact (shrinking size never reaches the floor, so
    losing accounts stay "open" and drop out of the count). With a realistic floor ($10 / $20): first trade min(120, room/3…5)
    43–50 % in 12–20 days vs fixed $120 44.5 % in 9; FW min(50, room/3…5) 60–68 % in 20–23 days vs fixed $50 66.6 % in 20. → no gain;
    keep fixed sizes. Lesson: any sim with shrinking size needs a minimum size or it hides the failures.

## Portfolio of 10, pre-registered 2026-10-19 BEFORE running (description — no pass/fail rule)
Every trading day: funded accounts (max 10) trade FW at $30, claim at $1,000 down to $250; evals trade the first book trade at $120;
new evals are bought so that funded + running evals = 10 (at most 3 a day, $40 each); a passed eval takes a free funded slot the next
day (else it is dropped). All accounts take the same trades (same direction — no hedging). Each 12-month window starting every month
2019-01 → 2025-10: fees, payouts (80 %), net, worst cumulative cash position, average funded accounts. 2 and 3 pts.
Results of the portfolio test (2026-10-19; 82 twelve-month windows 2019-01 → 2025-10; scratchpad wl/port_*.txt, wl/port2.awk):
- As pre-registered ($30, claim $1,000 keep $250): mean net +$6,234 / +$5,609 per year (2 / 3 pts), median +$5,476 / +$4,940, worst
  −$2,120 / −$2,800, 27 / 32 losing windows, 26 / 32 windows with NO payout (33R to reach a claim at $30 is too slow for cash flow),
  worst cash dip −$2,360 / −$2,920, ~8.6 funded accounts on average. By start year: 2019 +$546, 2020 +$10.3k, 2021 +$11.5k,
  2022 +$1.3k, 2023 +$5.3k, 2024 +$13.3k, 2025 +$0.3k.
- Sweep (description): **$50 risk, claim at $500 keep $250: mean +$13,372 / +$12,290, median +$10,497 / +$10,177, worst −$1,971 /
  −$2,355, losing windows 4 / 13 of 82, zero-payout 0 / 0, worst cash dip −$4,280 / −$4,520.** $40 / 500 / 250: +$11,305 / +$9,609,
  losing 4 / 15. $60 / 750: +$15,485 / +$13,172 but 9 / 21 losing. $75: worse (+$6,817 / +$4,688, 29 / 44 losing).
Reading: per ACCOUNT small risk + late claims pays most; per OPERATION (slots refilled with $40 full-port evals) a faster cycle wins —
$50 risk with frequent $500 claims keeps money coming out before the identical accounts breach together. All accounts take the same
trades, so they live and die together; the buffer and frequent claims are what smooth that.

## Portfolio eval policy, pre-registered 2026-10-19 BEFORE running (description)
Funded fixed at $50 / claim $500 / keep $250. Eval: first book trade at $100 / $120 / $145, or all FW setups at $40 / $50; new evals
per day at most 1 / 3 / 10. Same 82 windows, 2 and 3 pts. Pick = highest mean net with losing windows ≤ the $120 / 3-a-day baseline.
Results (2 pts / 3 pts; mean net per year, losing windows of 82): first trade $100, 3/day +$13,059 / +$12,065 (10 / 13) · **$120, 3/day
+$13,372 / +$12,290 (4 / 13)** · $145 +$11,407 / +$10,187 (26 / 25, fees $4.6–4.8k/yr) · all setups $40 +$10,273 / +$9,054 (23 / 25) ·
$50 +$11,775 / +$10,332 (13 / 19) · $120 1/day +$12,480 / +$11,201 (13 / 19) · **$120 10/day +$13,941 / +$12,727 (5 / 10)**, cash dip
$4.4k, ~7.4 funded on average. → Pick: full-port first trade at $120, refill every free slot the same day (up to 10). The full-port eval
beats an all-setups eval at the portfolio level (funded slots fill faster: ~7.3 vs 5–6 on average).

## Out-of-period check of the operation plan, pre-registered 2026-10-19 BEFORE running
Every setting (eval $120 first trade, refill up to 10/day, funded $50, claim $500 keep $250) was chosen on 2019–25. Re-run on
12-month windows starting 2010-07 → 2017-12 (data cut 2018-12-31), cost scaled to today's ATR ratio (2 and 3 pts × ATR / 320), plus the
fixed-cost version for reference. The plan "holds" if mean net per year > 0 and losing windows ≤ half at the scaled 3-pt cost.
Note: the NQ cache has no data 2026-03-20 … 2026-09-18, so 2025-start windows (2019+ test) run partly on a gap.
Results (89 windows starting 2010-07 → 2017-12; scratchpad wl/port_old_*.txt):
- Cost scaled to today's ATR ratio: 2 pts **+$10,722/yr** (median +$10,705, worst −$3,600, losing 5/89), 3 pts **+$10,062/yr** (median
  +$9,973, worst −$4,000, losing 5/89); every start year positive (+$4.3k … +$17.7k); cash dip $5.6k. → **HOLDS.**
- Fixed 2 pts (2–7× today's cost relative to the range): +$850/yr, 46/89 losing; fixed 3 pts −$3,771/yr, 65/89 losing.
Reading: the operation plan is not a 2019+ artefact — on 2010–18 at today's cost-to-range ratio it earns about what 2019–25 does. What
it depends on is cost: it fails when a round trip is a large share of the move. Vest's real fee + spread on NQ perps must be checked.

## Cost sweep of the operation plan (description, 2026-10-19)
Round-trip cost in today's NQ points (scaled by ATR / 320 in older years), mean net per year (losing windows), 2019–25 / 2010–18:
2 pts +$15.5k (7/82) / +$10.7k (5/89) · 4 +$14.1k / +$9.5k · 6 +$11.5k (19) / +$7.6k (8) · 8 +$9.6k (23) / +$6.5k (11) ·
10 +$8.2k (23) / +$5.0k (22) · 12 +$6.7k (31) / +$3.4k (24) · 15 +$5.1k (40) / +$1.3k (43) · 20 +$2.4k (48) / −$1.1k (60) · 30 −$1.8k (56) / −$5.2k (80). Break-even ≈ 15–20 pts round trip.
A percent-of-notional fee matters: at NQ ≈ 30,000, 1 bp per side = 3 pts per side = 6 pts round trip. Vest's NQ fee is not public in
any source found (web search 2026-10-19); it must be read from the platform before sizing the plan.

## High-cost plan, pre-registered 2026-10-19 BEFORE running (description)
If Vest's fee is a percent of notional, the round trip may be 10–20 pts. Funded set ∈ {FW all setups, first trade + weakness long,
cascade only} × funded risk {$50, $75} × claim {$500, $750} keep $250; eval = first trade $120, refill 10/day. Cost 10 / 15 / 20 pts
(scaled), windows 2019–25 and 2010–18. Pick per cost = highest mean of the two eras' means.
Results (scratchpad wl/hicost.txt; mean of the two eras' net per year): FW $50 claim $500 is best at EVERY cost — 10 pts +$6,564
(next: cascade-only $75 +$4,573, first trade + W $75 +$4,293), 15 pts +$3,196 (cascade-only $75 +$1,997), 20 pts +$645 (every other cell
< 0). Trading fewer setups does NOT protect against a high fee: the fee is paid per trade, but the extra setups earn more than it.
At 15+ pts per round trip, more than half the windows lose whatever the plan — the operation is not worth running there.
