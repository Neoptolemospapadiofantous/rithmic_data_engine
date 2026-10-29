# 2026-10-24 — can BOOK2 trade at a HIGHER WIN RATE and pay out faster? (PRE-REGISTERED before the run)
Founder: "how can we make our NQ strategy high win rate, obviously less PF, but at least we will be getting more payouts faster".
Already tested (2026-10-23 H6, not re-run): small FIXED targets 0.33-1 R → win rate 55-73 % but negative mean R after costs,
eval pass 0-10 %. This test covers trade MANAGEMENT that raises the win rate while keeping the measured-move runners.

Trades: BOOK2 NQ ONLY (founder: "for NQ flags") 2014-01 → 2026-10 (wide15 export), entry/stop/target unchanged, re-simulated on 1-minute cache bars
from the fill minute to the exit or 15:55 ET; same-bar stop-and-target = stop first (conservative); costs 1.5 NQ / 0.5 ES pts per
round trip. Base replication against the export's R is printed first; a variant is only read if base replicates.
Variants (fixed now):
- V0 base (measured-move target, fixed stop).
- V1 BE1: once price reaches +1 R, stop to entry (+ the cost, so a scratch is ~0 R).
- V2 PARTIAL1: half off at +1 R, stop to break-even, other half to the measured-move target.
- V3 PARTIAL1_2R: half off at +1 R, stop to break-even, other half at +2 R.
- V4 TRAIL1: after +1 R, the stop trails 1 R behind the best price (checked bar by bar).
- V5 BE0.5: stop to break-even at +0.5 R.
Report per variant: win rate (> 0 R), scratch rate, mean R, profit factor, both halves (2014-19 / 2020-26).
Account: flag_prop_ev mtm with FPEV_MTM_MKT=0 (NQ only), 2 micros (so a half exit is a real micro), $1,000 daily stop, extreme mark — pass %, paid %,
days to pass, days to first payout, $ per eval (Topstep 50K, Lucid Flex 50K), both halves. Caveat: the mtm mark uses each trade's
final exit minute, so after a partial the open exposure is overstated (pessimistic for V2/V3).
"Better" = paid % AND $ per eval ≥ base in BOTH halves, or days to first payout lower with $ per eval within 10 % of base.

## Results (2026-10-24; research/programs/book2_highwr.py, outputs research/programs/out/book2_highwr_*.txt)
Base replication: bar sim 0.173 R vs export 0.181, corr 0.998, 99.5 % same win/loss. BOOK2 NQ 3,239 trades.
| variant | win % | mean R | PF | Topstep pass / paid | days to first payout | Topstep $/eval | worst month |
| V0 base | 31.9 | 0.173 | 1.23 | 38 / 24 % | 44 | $1,192 | −$5,856 |
| V1 BE at +1R | 19.1 (+36 % scratches) | 0.122 | 1.24 | 36 / 26 % | 63 | $1,245 | −$4,213 |
| V2 half off at +1R, rest BE → target | 54.9 | 0.056 | 1.11 | 37 / 32 % | 58 | $976 | −$3,423 |
| V3 half at +1R, rest at 2R | 54.9 | 0.005 | 1.01 | 27 / 24 % | 58 | $442 | −$3,705 |
| V4 trail 1R after +1R | 47.1 | 0.046 | 1.09 | 35 / 28 % | 56 | $698 | −$4,021 |
| V5 BE at +0.5R | 11.2 (+59 % scratches) | 0.050 | 1.15 | 26 / 19 % | 78 | $472 | −$2,246 |
(account: NQ only, 2 micros, $1,000 daily stop, mtm extreme mark; Lucid Flex shows the same ordering.)
Verdict: no variant pays FASTER — the base is quickest to a first payout (44 days); every higher-win-rate version takes 56-63
days because a smaller edge builds the payout buffer more slowly. V2 (half off at +1R) raises the share of evals that ever get paid
24 % → 32 % and halves the worst month, but pays $976 vs $1,192 per eval and is weaker in 2020-25 starts ($680 vs $960). V1 (BE at
+1R) is the only one level with base on $ ($1,245 vs $1,192, inside the noise) with a smaller drawdown, but slower (63 days).
No change is recommended on payout speed; V2 is the choice only if "more accounts paid, smaller swings" matters more than $/eval.

## Part 2 (founder: "we want to increase our WR AND increase our position size") — PRE-REGISTERED before the run
The high-WR variants swing less (V2 daily sd $343 vs $584 at 2 micros), so compare at EQUAL RISK: V1, V2, V4 at 3, 4, 5 micros vs base
at 2 (and base at 3 for reference). Same mtm model, NQ only, $1,000 daily stop. Read: paid %, days to first payout, $ per eval,
daily sd, worst month, max EOD drawdown. "Better" = more $ per eval AND paid % ≥ base at a daily sd / worst month no larger than base.

### Part 2 results (research/programs/out/book2_highwr_size.txt) — Topstep 50K, NQ only, $1,000 daily stop
| version | daily sd | worst month | EOD dd | paid % | days to 1st payout | $/eval (2014-19 / 2020-25 starts) |
| base 2 micros | $584 | −$5,856 | $10,403 | 24 | 44 | $1,192 (1,404 / 960) |
| base 3 micros | $862 | −$8,697 | $14,434 | 13 | 43 | $833 |
| half-off 3 micros | $512 | −$4,504 | $8,681 | 23 | 41 | $895 (1,290 / 464) |
| half-off 4 micros | $677 | −$5,154 | $11,474 | 22 | 38 | $1,076 (1,477 / 639) |
| half-off 5 micros | $822 | −$7,681 | $20,247 | 13 | 38 | $890 |
| BE at +1R 3 / 4 micros | $719 / $950 | −$5,689 / −$6,734 | | 15 / 11 | 55 / 53 | $1,064 / $1,070 |
| trail 3 / 4 micros | $526 / $695 | −$5,402 / −$6,351 | | 19 / 18 | 44 / 38 | $612 / $715 |
Verdict: half-off at +1R with 4 micros reaches a first payout ~6 days sooner (38 vs 44) with a smaller worst month, but pays less per
eval ($1,076 vs $1,192), slightly fewer paid accounts (22 vs 24 %) and much less for 2020-25 starts ($639 vs $960). Reason: the
edge per position falls from 2 × 0.173 = 0.35 R to 4 × 0.056 = 0.22 R while risk per day rises; beyond ~4 micros the drawdown rules
cut paid % sharply (13 % at 5). FAIL by the pre-registered rule (needs more $ AND ≥ paid % at no more risk).

## Part 3 (founder: "could be 0.5 R") — PRE-REGISTERED before the run
V6 PARTIAL05: half off at +0.5 R, stop to break-even, other half to the measured-move target. Trade stats, then the account at 2-5
micros (NQ only, $1,000 daily stop) vs base 2 micros, same "better" rule as Part 2.

### Part 3 result (half off at +0.5 R): win rate 64.2 % (2020-26 70.9 %), mean R 0.001, PF 1.00. Topstep $/eval: 2 micros $194,
3 $497, 4 $554, 5 $629 (first payout 82 / 64 / 45 / 40 days) vs base $1,192 / 44 days. FAIL.

## Part 4 — the MATRIX (founder: "could be 0.7", "test the matrix") — PRE-REGISTERED before the run
Partial level L ∈ {0.3, 0.5, 0.7, 1.0, 1.5} R × fraction taken f ∈ {1/3, 1/2, 2/3} (rest: stop to break-even, run to the measured
move) × size q ∈ {2, 3, 4, 5} micros = 60 cells, plus base at 2-5 micros. NQ only, mtm, $1,000 daily stop, Topstep 50K + Lucid Flex.
Choice: the cell with the highest Topstep $/eval on 2014-19 STARTS (h1); then read its 2020-25 starts (h2) once. Pass = h2 $/eval ≥
base-2-micro h2 ($960) AND paid % ≥ base AND first payout ≤ base. The full matrix is reported either way (win rate, mean R, $/eval,
paid %, days to first payout).

### Part 4 results — the matrix (research/programs/book2_partial_matrix.py; out book2_partial_matrix_{trades,accounts}.txt)
Trade level: the earlier the partial, the higher the win rate and the lower the mean R, without exception — 0.3 R: win 55-65 %,
mean R −0.03 to −0.05; 0.5 R: win 68-70 %, mean R −0.03 to +0.01; 0.7 R: win 62 %, −0.02 to +0.02; 1.0 R: win 55 %, +0.03 to +0.07;
1.5 R: win 45 %, +0.06 to +0.10; base: win 32 %, +0.17.
Account (Topstep 50K, NQ only, $1,000 stop, 64 runs): PRE-REGISTERED CHOICE = best on 2014-19 starts = a third off at +1 R, 3 micros
($1,496) → 2020-25 starts $585 vs base $960, paid 22 vs 24 %, first payout 44 vs 44 days → FAIL. Every partial cell is weaker than
base on 2020-25 starts: partials cap the winners the recent trending years paid on.
Not the pre-registered choice, reported for the founder: a THIRD off at +1.5 R, 2 micros = $1,196/eval (base $1,192), paid 30 % vs
24 %, daily sd $436 vs $584, but 51 vs 44 days to the first payout and 2020-25 starts $912 vs $960 — about level on money with more
accounts paid, at a 45 % win rate. Anything at 0.3-0.7 R pays $1-830 per eval.

## Part 5 (founder: "since it's 68-70 % we full port") — run before reading
Half off at +0.5 R (70 % win) and a third off at +0.5 R (68 % win) at 5 / 8 / 10 / 15 / 20 micros vs base at the same sizes; NQ only,
mtm, $1,000 daily stop, Topstep 50K + Lucid Flex. Report $/eval, paid %, pass %, days to first payout, worst month.
### Part 5 result (out book2_highwr_fullport.txt): full size on the 68-70 % versions FAILS. Half off at +0.5 R: 5 micros $647/eval
(paid 19 %), 8 → $461 (8 %), 10 → $567 (8 %), 15 → $185 (2 %), 20 → $63 (1 %); third off at +0.5 R: 20 micros $21 (0 % paid).
Base at the same sizes also collapses (20 micros $84, 1 % paid). Every version is best at 2-5 micros; the 70 % win rate rests on
~0 R per trade, so size only adds swings, and the $1,000 daily stop / drawdown rules end most accounts. Base 2 micros stays $1,192.
### Part 5b: fixed 4 minis (40 micros) — base, half-off-0.5R and third-off-0.5R all pay $0, 0 % paid (pass 1-2 %); a median BOOK2
NQ stop is 33 pts = $2,647 at 4 minis, more than the whole $2,000 drawdown. FAIL.

## Part 6 (founder: "up to max 4 minis") — SCALE UP WITH THE CUSHION, PRE-REGISTERED before the run
Day-level account simulator (research/programs/book2_scale_sim.py) on the mtm engine's per-day dump at 1 micro with no own stop
(FPEV_MTM_DUMP: day P&L and the day's worst intraday mark, $ at today's ATR). Size is chosen at the START of each day; day P&L and
worst mark scale linearly with size. Topstep 50K rules from payout_sim.hpp (start 50,000; $2,000 EOD trailing floor that stops at
the start; $1,000 soft daily loss = day closes at −$1,000 if the worst mark reaches it; eval target $3,000, best day ≤ 50 % of the
profit; funded payout after 5 winning days ≥ $150, 45 % of the profit, cap $5,000, 5-mini limit); Lucid Flex 50K likewise (no daily
limit, floor locks at start + $100, payout needs $500 profit, 4-mini limit). A breach = the worst intraday mark reaches the floor.
Starts every 5th trading day 2014-01 → 2025-04; eval ≤ 60 trading days, funded 120.
Sizing (cap 40 micros = 4 minis): F2 fixed 2 (reference — must land near the mtm engine's base), F40 fixed 40, SCALE250 = 2 + 1 micro
per $250 of profit above the start, ROOM = the largest size whose 95th-percentile worst day (1-micro, 2014-19) uses ≤ 50 % of the room
above the floor (min 1). Each phase (eval, funded) sizes from its own balance. Trades: base, half off at +0.5 R, half off at +1 R,
a third off at +1.5 R. Report: pass %, paid %, days to first payout, $ per eval (all payouts in the funded window), by start half.
Better = more $ per eval AND paid % ≥ F2 base in BOTH start halves.

### Part 6 results (book2_scale_sim.py; out book2_scale_up.txt) — scaling up to 4 minis FAILS
Validation: F2 base Topstep = pass 38.5 %, paid 23.9 %, first payout 42 days, $1,179/eval — the mtm engine gives 38 / 24 / 44 / $1,192.
Topstep 50K $/eval (paid %): base F2 $1,179 (24 %), SCALE250 $2 (1 %), ROOM $318 (15 %), F40 $0 (0 %). Half off at +0.5 R (70 % win):
F2 $221, SCALE250 $30, ROOM $244, F40 $0. Half off at +1 R: F2 $1,049 (35 %), ROOM $614, SCALE250 $129. A third off at +1.5 R: F2 $1,262
(32 %), ROOM $558, SCALE250 $15. Lucid Flex the same ordering. Scaling up gets the FEW survivors paid sooner (24-34 days) but ends most
accounts first: a 50K account's $2,000 drawdown cannot carry more than a few micros of this strategy's daily swing (1-micro worst
day 95th pct ≈ $230-310 at today's ATR). Fixed small size wins for every exit variant.

## Part 7 (founder: "we must get payouts faster consistently. check online") — LucidDaily, PRE-REGISTERED before the run
Online (Oct 2026, third-party summaries of Lucid's rules): LucidDaily 50K — eval $3,000 target, $2,000 max loss (EOD chosen), 50 %
best-day rule, no minimum days; funded: INTRADAY trailing $2,000 that locks at $50,100 once the balance passes $52,100; payout ANY
day the balance is ≥ $52,600 (min $500; everything above the $52,100 buffer, no cap, no consistency), 90/10; 4 minis max; red-folder
news trading = hard breach (not modelled — needs a news filter before going live); price $185 (EOD, no DLL) list.
Simulated in book2_scale_sim.py next to Topstep 50K and Lucid Flex 50K at fixed 1-4 micros: intraday trail = floor follows the day's
best mark first, then the worst mark is checked (conservative). Report pass %, paid %, days to first payout, payouts per paid account,
$ per eval in 120 funded days, by start half.

### Part 7 results (book2_scale_sim.py; out book2_lucid_daily*.txt) — LucidDaily is SLOWER and pays LESS for this strategy
BOOK2 NQ base, $ per eval / paid % / days to first payout: Topstep 50K at 2 micros $1,179 / 24 % / 42; LucidDaily 50K at 1 micro
$665 / 22 % / 77, at 2 micros $218 / 5 % / 42, at 3 micros $49 / 1 %. The optimistic intraday ordering gives the same picture
($605 / 20 % at 1 micro; $239 / 5 % at 2). Partial variants on LucidDaily: best $559 (a third off at +1.5 R, 2 micros, 12 % paid).
Cause: the funded drawdown trails INTRADAY until the $52,100 lock — the flags often run into profit and come back, so the floor
ratchets up on open profit and most funded accounts end before reaching the $52,600 payout line. Daily-payout products found online
(LucidDaily, MFFU Rapid, Take Profit Trader PRO) pair daily payouts with intraday trailing drawdown or tight consistency rules,
both of which cut this strategy; the $2,000-$2,100 buffer before the first payout exists on all of them. Keep EOD-drawdown plans
(Topstep / Lucid Flex / Lucid Pro) at 1-2 micros; payout speed comes from more staggered accounts.
