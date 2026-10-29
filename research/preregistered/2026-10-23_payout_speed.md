# Purchase → first payout: how fast can it be? — written 2026-10-23 05:05 Cyprus, BEFORE the run

Founder: "3 months for a payout … first lets speed up the payout process". Pro Eval plans (screenshot 2026-10-23): 25K target
$1,250 / max loss $1,000 EOD / DLL $600 optional / fee $64.80 / max 20 micro; 50K $3,000 / $2,000 / $1,200 / $103.20 (≈ $50 with
the founder's coupon) / 40 micro; 1-day pass allowed, no consistency rule, "3 days to payout", on-demand payouts. Funded rules
NOT seen: assumed = same EOD max loss, payout request allowed once 3 winning days of ≥ $150 since funding (or the last
payout) AND balance above a buffer B ∈ {$0, $1,000}; first payout = profit above B. Trade stream = flags + cascade, $ at
today's price, in-sample lists, OOS sets started 2020-26. Eval: rotation with A ∈ {1, 2, 3} accounts active at once (A = N
means all parallel), N ∈ {3, 5, 8}, size ∈ {1, 2, 3} MNQ, lock $750 (eval only), hand-over at the DLL. Funded: the account
trades every trade at 1 MNQ, no lock (as the plan says). Clock = sessions from purchase to the FIRST payout request in the set.
Judged: median clock, P(first payout within 30 / 45 sessions), fee per first payout, mean first payout $. Target: ≤ 30 sessions
median with P(any payout within 120 sessions) ≥ 0.8. Output research/programs/out/payout_speed.txt.

## Results (2026-10-23; research/programs/out/payout_speed.txt; "mean $" in the output = the set's total of first payouts, not per account)
| plan | N | active | eval size | buffer | P(≥1 funded) | 1st pass (sessions) | P(payout ≤ 30) | P(≤ 45) | P(≤ 120) | 1st payout (median sessions) | fee per paid set |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 25K | 8 | 1 | 3 MNQ | $0 | 0.97 | **4** | **0.59** | 0.59 | 0.59 | **14** | $872 |
| 25K | 5 | 1 | 3 MNQ | $0 | 0.91 | 3 | 0.46 | 0.46 | 0.46 | 12 | $705 |
| 25K | 3 | 1 | 2 MNQ | $0 | 0.80 | 4 | 0.42 | 0.42 | 0.42 | 13 | $464 |
| **50K** | **8** | **1** | **3 MNQ** | $0 | **0.99** | 14 | 0.50 | **0.69** | **0.77** | **26** | **$519** |
| 50K | 5 | 1 | 3 MNQ | $0 | 0.89 | 12 | 0.42 | 0.53 | 0.58 | 24 | $430 |
| 50K | 5 | 1 | 2 MNQ | $0 | 0.91 | 18 | 0.32 | 0.46 | 0.59 | 30 | $420 |
| 50K | 3 | 1 | 1 MNQ | $0 | 0.80 | 38 | 0.09 | 0.22 | 0.50 | 49 | $300 |
| 50K | 8 | 2–3 | 3 MNQ | $0 | 0.70–0.84 | 8–10 | 0.47 | 0.49–0.57 | 0.49–0.58 | 18–19 | $688–822 |
Read: (1) **The Pro rules collapse the eval clock.** With a one-day pass and no consistency cap, 3 MNQ in the eval passes in a
median 3–4 sessions on 25K and 12–14 on 50K (P(≥1 funded) 0.9–0.99) — the three-week eval of the Select-style model is gone.
(2) **The bottleneck moved to the funded stage.** On 25K, P(payout ≤ 45) = P(payout ≤ 120): every set that has not paid by
session 45 never pays, because the funded 25K ($1,000 EOD max loss) blows before it collects three $150 days — 97 % get
funded, 59 % ever get paid. The 50K ($2,000 room) keeps paying later (0.69 → 0.77). (3) **Speed vs certainty of payout**:
25K · 8 · 3 MNQ pays within 30 sessions 59 % of the time (median 14 sessions ≈ 3 weeks) but caps there; 50K · 8 · 3 MNQ pays
within 45 sessions 69 % of the time and within 120 77 % (median 26 ≈ 5 weeks). (4) Two or three accounts active at once do
NOT speed the payout — the eval is already fast at 3 MNQ, and the extra correlation lowers P(≥1 funded) (0.99 → 0.70–0.84).
(5) The $1,000 buffer costs ~5 points of payout probability and ~35 % of the first payout size. (6) The $50 coupon makes
fee per PAID set $420–520 on 50K and $300 at the slow N3/1 MNQ setting — fees are no longer the constraint; funded survival is.
**Open:** the Pro funded rules (buffer, min day, cap, split) are assumed; funded-stage protection (a daily lock once funded, or
NQ-only once funded) was not tested here and is the obvious next lever for payout certainty. Nothing changes any config.

### Funded-stage protection (same run; N8 · 1 active · eval 3 MNQ · funded 1 MNQ · buffer $0)
| plan | funded rule | P(payout ≤ 30) | ≤ 45 | ≤ 120 | 1st payout (median) | funded blown before paying | $ per paid account |
|---|---|---|---|---|---|---|---|
| 50K | whole book, no lock | 0.50 | 0.69 | 0.77 | 26 | 35 % | 1,879 |
| **50K** | **NQ flags + cascade only (drop ES)** | **0.53** | **0.74** | **0.84** | 26 | **29 %** | 1,530 |
| 50K | NQ flags only | 0.39 | 0.59 | 0.80 | 31 | 24 % | 1,330 |
| 50K | whole book, +$300 daily lock | 0.45 | 0.64 | 0.73 | 28 | 44 % | 512 |
| 50K | whole book, stop the day at −$300 | 0.46 | 0.69 | 0.77 | 26 | 32 % | 1,907 |
| 25K | whole book, no lock | 0.58 | 0.58 | 0.58 | 13 | 55 % | 1,905 |
| 25K | NQ flags + cascade only | 0.61 | 0.61 | 0.61 | 14 | 46 % | 1,642 |
| 25K | NQ flags only | 0.58 | 0.69 | 0.69 | 18 | 35 % | 1,492 |
Read: dropping ES once funded is the one protection that helps — ES adds the same bet at a worse per-trade R (2026-10-22
D2/three_way), so it adds drawdown without adding payout days: 50K payout-ever 0.77 → 0.84, blown 35 % → 29 %, same speed.
A daily lock on the funded account does nothing for speed and cuts the first payout to a third ($512–721) — locks belong in
the eval only. Early daily stops do nothing. The 25K stays capped at ~0.6–0.7 payout-ever whatever the rule: its $1,000 room
is too small for this book at 1 MNQ; NQ-flags-only lifts it to 0.69 at the price of a slower first payout (18 sessions).
**Plan update: eval 3 MNQ rotation → funded 1 MNQ on NQ flags + cascade (no ES), no lock.**

### LucidPro 50K with the EXACT funded rules (looked up 2026-10-23: buffer = max loss + $100 = $52,100 not withdrawable; minimum cycle
profit $500; best day ≤ 40 % of cycle profit; first payout cap $2,000, later $2,500; 90/10; MLL $2,000 EOD trailing, locks at +$100
once the buffer is reached; DLL $1,200 optional; eval one-day pass). Eval 3 MNQ rotation N8 lock $750; 72 OOS sets; 160-session horizon.
| funded size | DLL | streams | P(payout ≤ 45) | ≤ 60 | ≤ 90 | ≤ 160 | median 1st payout | funded blown before paying | net 1st payout | fee per paid set |
|---|---|---|---|---|---|---|---|---|---|---|
| **1 MNQ** | **on** | **NQ + cascade** | 0.14 | 0.31 | 0.51 | **0.60** | **60** | 46 % | $1,563 | **$670** |
| 1 MNQ | on | book + ES | 0.14 | 0.25 | 0.46 | 0.54 | 64 | 52 % | $1,713 | $738 |
| 2 MNQ | on | NQ + cascade | 0.14 | 0.26 | 0.40 | 0.49 | 59 | 63 % | $1,689 | $823 |
| 3 MNQ | on | NQ + cascade | 0.17 | 0.29 | 0.46 | 0.50 | 56 | 61 % | $1,781 | $800 |
| 3 MNQ | off | book + ES | 0.08 | 0.21 | 0.35 | 0.38 | 59 | 76 % | $1,760 | $1,067 |
Read: **the assumed "three $150 days" rule was far too kind.** LucidPro's first payout needs ≈ $2,600 of funded profit ($2,100
buffer + $500 cycle) spread so that no day is over 40 % of it — at this book's ~$60/day at 1 MNQ that is ~45 trading days of
profit, during which 46 % of funded accounts hit the $2,000 trail. Result: median purchase → first payout **60 sessions
(~12 weeks)**, paid-ever 0.60, i.e. the three months the founder objected to. Bigger funded size does not buy speed: it blows
more accounts (61–76 %) and the cap holds the payout at ~$1,700 anyway. DLL on is slightly better than off (it caps bad days
before the trail does). Dropping ES helps a little (0.54 → 0.60). **The fast Pro EVAL is real (14 sessions to funded); the slow
part is Lucid's funded buffer.** The lever is therefore the FIRM's funded rules, not our trading: Topstep 50K (no buffer, 5 days
≥ $150, 50 % of balance above start, cap $2,000) measured 46 days purchase → first payout on the flag book in candidate_by_firm
(before the cascade and the rotation were added) — the next step is the same end-to-end run on Topstep's and Lucid Flex's exact
rules, and on any firm whose funded stage has no buffer.

### Funded stage IN ROTATION, exact LucidPro 50K rules (NQ + cascade, DLL on; FA = funded accounts active at once; 72 OOS sets, 160 sessions)
| funded size | FA | funded lock | P(≤ 45) | ≤ 60 | ≤ 90 | paid-ever | median 1st payout | paid accounts/set | blown funded | fee per paid set |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 MNQ | all | none | 0.14 | 0.31 | 0.51 | 0.60 | 60 | 1.92 | 46 % | $670 |
| **1 MNQ** | **1** | **$750** | 0.18 | 0.35 | **0.61** | **0.79** | 67 | 1.67 | 46 % | **$505** |
| 1 MNQ | 1 | none | 0.07 | 0.19 | 0.44 | 0.79 | 87 | 1.26 | 45 % | $505 |
| 1 MNQ | 2 | none | 0.14 | 0.31 | 0.54 | 0.72 | 66 | 1.64 | 52 % | $554 |
| **2 MNQ** | **1** | **$750** | **0.33** | **0.47** | 0.57 | 0.58 | **43** | 1.31 | 65 % | $686 |
| 2 MNQ | 2 | $750 | 0.25 | 0.36 | 0.40 | 0.40 | 41 | 1.04 | 71 % | $993 |
| 3 MNQ | 1 | $750 | 0.26 | 0.43 | 0.54 | 0.56 | 46 | 1.22 | 66 % | $720 |
Read: rotating the FUNDED accounts (one active at a time, with a $750 lock) does what it did in the eval — each account sees
a different slice, so the set stops failing together: paid-ever 0.60 → 0.79 at 1 MNQ, fee per paid set $670 → $505, at the
cost of a slower first payout (60 → 67 sessions). A funded lock helps ONLY under rotation (in parallel it starved the payout);
with rotation it hands the next trade on and lifts "paid within 90" from 0.44 to 0.61. The speed cell is 2 MNQ · one active ·
$750: a third of sets paid within 45 sessions (9 weeks), median 43, but paid-ever drops to 0.58 and two-thirds of funded
accounts blow. Two funded accounts active at once is worse than one on every column. **Choice: certainty = 1 MNQ · 1 active ·
$750 (79 % paid, ~13 weeks); speed = 2 MNQ · 1 active · $750 (58 % paid, ~9 weeks).** Neither beats a firm without a funded
buffer — that comparison is the next run.

### "First payout within one month, $300–400 budget, as many MNQ as possible" (founder, 2026-10-23; same exact LucidPro rules; rotation + profit lock both stages; NQ + cascade; DLL on; 72 OOS sets)
| N (fees) | eval | funded | P(paid ≤ 21) | ≤ 30 | ≤ 45 | paid-ever | median 1st payout | funded blown | net 1st payout |
|---|---|---|---|---|---|---|---|---|---|
| 8 ($400) | 4 MNQ $750 | 3 MNQ $750 | 0.07 | 0.19 | 0.38 | 0.57 | 37 | 67 % | $1,734 |
| **8 ($400)** | **6 MNQ $1,000** | **4 MNQ $1,000** | **0.17** | **0.33** | **0.50** | 0.56 | **28** | 67 % | $1,800 |
| 8 ($400) | 6 MNQ $1,000 | 2 MNQ $750 | 0.18 | 0.29 | 0.43 | 0.57 | 30 | 62 % | $1,501 |
| 12 ($600) | 6 MNQ $1,000 | 6 MNQ $1,000 | 0.14 | 0.35 | 0.61 | 0.75 | 32 | 67 % | $1,800 |
Read: **a first payout inside 21 sessions happens in about one set in six at best (17 %), inside 30 sessions one in three.**
The ceiling is Lucid Pro's funded buffer ($2,100 + $500 cycle, 40 % best-day), not size: pushing the eval to 6 MNQ moves the
median first payout from 44 → 28 sessions, pushing the funded side to 4–6 MNQ gains nothing further and blows two-thirds of
funded accounts. Within the budget the best cell is **8 × 50K · eval 6 MNQ lock $1,000 · funded 4 MNQ lock $1,000**: median 28
sessions (~6 weeks), 33 % paid within a month-and-a-bit, 50 % within 9 weeks, 56 % ever, first payout ≈ $1,800 net at the
cap. Six accounts ($300) was not run; it will sit a little below the 8-account row on certainty and about the same on speed.
**One month is not reachable with this book on this firm's funded rules; a firm without a buffer is the only route to it.**

### $300 budget = 6 × 50K at $50 (founder: "lets say 300 we can invest"; same exact LucidPro rules, rotation + lock both stages, NQ + cascade, DLL on, 72 OOS sets)
| eval | funded | P(≤ 21) | ≤ 30 | ≤ 45 | ≤ 60 | ever | median 1st | paid accts/set | net 1st payout | E[total 1st payouts] | fee/paid set |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 3 MNQ $750 | 1 MNQ $750 | 0.03 | 0.06 | 0.19 | 0.36 | **0.68** | 60 | 1.17 | $1,234 | $1,440 | **$441** |
| **6 MNQ $1,000** | **1 MNQ $750** | 0.06 | 0.11 | 0.25 | 0.47 | **0.68** | 48 | **1.33** | $1,303 | **$1,738** | **$441** |
| 6 MNQ $1,000 | 2 MNQ $750 | **0.12** | **0.29** | **0.42** | 0.46 | 0.49 | **28** | 0.88 | $1,556 | $1,361 | $617 |
| 6 MNQ $1,000 | 3 MNQ $1,000 | 0.12 | 0.31 | 0.42 | 0.47 | 0.49 | **25** | 0.78 | $1,732 | $1,347 | $617 |
| 4 MNQ $1,000 | 2 MNQ $750 | 0.11 | 0.17 | 0.38 | 0.44 | 0.50 | 34 | 0.99 | $1,456 | $1,436 | $600 |
Read: with six accounts the same trade-off, one notch lower than eight. **Eval at 6 MNQ is right either way** (it is the eval
speed that moves the clock). The funded size is the choice: **1 MNQ = 68 % of sets paid, $1,740 expected in first payouts
for $300, median ~10 weeks; 2–3 MNQ = half the sets paid, median 5–6 weeks, $1,350 expected.** A payout inside 21 sessions
is 12 % at best, inside 30 sessions 29–31 %. Expected return on the $300 is 4.5–5.8× from the first payouts alone (then the
accounts keep paying on later cycles, not modelled here).

### 25K vs 50K Pro on ~$300 (founder: "can be 25k … but try 50k if its faster"; 25K funded rules assumed from the pattern — buffer +$1,100, min cycle $250, 40 %, cap $1,000; 72 OOS sets)
| plan | N | fees | eval | funded | P(≤ 21) | ≤ 30 | ≤ 45 | ever | median 1st | paid/set | net 1st payout | E[total 1st payouts] |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 25K | 4 | $259 | 6 MNQ $750 | 2 MNQ $500 | **0.19** | 0.28 | 0.29 | 0.31 | **17** | 0.42 | $891 | $371 |
| 25K | 5 | $324 | 10 MNQ $750 | 3 MNQ $750 | 0.18 | **0.35** | 0.38 | 0.42 | 24 | 0.72 | $890 | $642 |
| 25K | 5 | $324 | 3 MNQ $500 | 1 MNQ $500 | 0.10 | 0.24 | 0.38 | 0.49 | 31 | 0.86 | $837 | $721 |
| **50K** | **6** | **$300** | **6 MNQ $1,000** | **3 MNQ $1,000** | 0.12 | 0.31 | **0.42** | 0.49 | 25 | 0.78 | **$1,732** | **$1,347** |
| 50K | 6 | $300 | 6 MNQ $1,000 | 1 MNQ $750 | 0.06 | 0.11 | 0.25 | **0.68** | 48 | **1.33** | $1,303 | **$1,738** |
Read: the 25K is a little faster to the FIRST payout (median 17–24 sessions vs 25–28; inside 21 sessions 19 % vs 12 %) but pays
HALF per payout ($850–900 vs $1,500–1,700, its cap), blows more funded accounts (its $1,000 room), and the expected total of
first payouts for the same ~$300 is $370–720 vs $1,350–1,740. The speed gain is one week; the dollar loss is more than half.
**50K it is**: 6 × 50K, eval 6 MNQ lock $1,000, funded 3 MNQ lock $1,000 for speed (median 25 sessions, 42 % paid within 9
weeks) or funded 1 MNQ lock $750 for certainty (68 % of sets paid). The 25K is worth a look only if a coupon makes it ~$30.

### Firm comparison on Rithmic-connected plans (2026-10-23; 6 accounts, rotation + lock both stages, NQ + cascade, 72 OOS sets, 160 sessions)
Rithmic connectivity (third-party directories, 2026-10-23): native/eligible — Apex, Bulenox, Take Profit Trader, BluSky, OneUp;
Rithmic as an add-on route — Lucid, Tradeify, Earn2Trade, Legends, TradeDay, Phidias; Topstep is TopstepX-only (no Rithmic).
| plan | eval / funded | P(≥1 funded) | P(paid ≤ 30) | ≤ 45 | ever | median 1st payout | paid accts/set | net per payout | E[total 1st payouts] |
|---|---|---|---|---|---|---|---|---|---|
| LucidPro 50K (exact) | 6 MNQ / 3 MNQ | 0.96 | 0.31 | 0.42 | 0.49 | 25 | 0.78 | $1,732 | $1,347 |
| LucidPro 50K (exact) | 6 MNQ / 1 MNQ | 0.96 | 0.11 | 0.25 | 0.68 | 48 | 1.33 | $1,303 | $1,738 |
| LucidFlex 50K (rulebook 10-20) | 3 MNQ / 1 MNQ | 0.76 | 0.08 | 0.29 | 0.49 | 42 | 0.85 | $862 | $731 |
| LucidFlex 50K | 6 MNQ / any | 0.44 | ≤ 0.12 | ≤ 0.29 | ≤ 0.33 | 28–38 | ≤ 0.46 | | ≤ $349 |
| **Apex 50K (ASSUMED 2026 rules)** | **6 MNQ / 2 MNQ** | 0.94 | **0.31** | **0.56** | 0.62 | **31** | 1.24 | **$2,000** | **$2,472** |
| **Apex 50K (ASSUMED)** | **6 MNQ / 1 MNQ** | 0.94 | 0.18 | 0.49 | **0.78** | 42 | **1.79** | $1,954 | **$3,501** |
Read: (1) **Lucid Flex is worse than Lucid Pro for this book** — its eval consistency (40 %, 3 days) kills the fast 6-MNQ eval
(P(≥1 funded) 0.96 → 0.44) and its funded stage pays only 50 % of profit, halving every payout. (2) **Apex-style funded rules
(5 × $200 days, no buffer, 100 % of first payouts) are the best found**: same speed as Lucid Pro's fastest cell (median ~31
sessions) with twice the dollars ($2,000 payouts, $2,500–3,500 expected per $300 set) and 62–79 % of sets paid. **These Apex
rules are from 2026 reviews, not the help centre, and reviews conflict (30 % vs 50 % consistency; $200 qualifying day; a
"safety net" is mentioned without a figure) — verify before buying.** (3) Lucid Pro remains the fastest EVAL on Rithmic.

### Apex 50K with the PUBLISHED rules (Aug-2026 rules article: safety net = start + $2,000 + $100 → +$2,100; 5 qualifying days ≥ $250; best day ≤ 50 %; min $500; first cap $1,500; 100 % to the trader; eval $3,000 / $2,500 trailing / 1-day pass assumed)
| eval / funded | P(≥1 funded) | P(≤ 30) | ≤ 45 | ≤ 60 | ever | median 1st | paid/set | net 1st | E[total] |
|---|---|---|---|---|---|---|---|---|---|
| 6 MNQ / 2 MNQ | 0.94 | 0.32 | 0.53 | 0.53 | 0.54 | 28 | 0.99 | $1,256 | $1,239 |
| 6 MNQ / 1 MNQ | 0.94 | 0.10 | 0.38 | 0.53 | 0.71 | 45 | 1.47 | $1,049 | $1,545 |
| 3 MNQ / 1 MNQ | 0.94 | 0.06 | 0.18 | 0.38 | 0.69 | 58 | 1.32 | $1,056 | $1,393 |
**The "no-buffer Apex" row above is withdrawn**: Apex has the same +$2,100 safety net as Lucid Pro. With real rules Apex ≈
Lucid Pro on speed (median 28 vs 25–28) and certainty (0.54–0.71 vs 0.49–0.68) and pays LESS per payout ($1,050–1,480 at a
$1,500 cap and 100 %, vs Lucid's $2,000 cap × 90 % = $1,800), so Lucid Pro edges it on dollars. **Every Rithmic-connected firm
checked carries a funded buffer** (Lucid Pro/Flex-style buffer or 50 % share, Apex safety net, Tradeify Select $2,100 buffer
per memory, Take Profit Trader PRO "buffer = drawdown" per reviews); the no-buffer funded stage is Topstep's, and Topstep is
TopstepX-only — not Rithmic. **Decision 2026-10-23: Lucid Pro 50K on Rithmic (add-on route; confirm its cost/availability), with
Apex as the alternative if a native Rithmic login matters more than ~$500 per payout.** A first payout inside one month stays a
~1-in-6 to 1-in-3 outcome on either firm; median ~6 weeks at 6 MNQ eval / 2–3 MNQ funded.

### FINAL PIPELINE, 8 months (research/programs/out/final_pipeline.txt; Lucid Pro 50K exact rules with the 40 % rule on CYCLE profit; 6 × 50K = $300; rotation one-at-a-time + lock in both stages; NQ flags + cascade; DLL on; 143 start dates 2020-26; later payouts cap $2,500)
| eval / funded | P(≥1 funded) | 1st funded (median) | P(paid ≤ 30) | ≤ 45 | ≤ 60 | ≤ 90 | ever | 1st payout median | payouts per set | 8-month $: median / mean / p90 | P(net > fees) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 6 MNQ $1,000 / 1 MNQ $750 | 0.94 | 5 | 0.14 | 0.33 | 0.44 | 0.60 | **0.69** | 48 | 2.0 | **$3,005** / $3,100 / $6,911 | **0.69** |
| **6 MNQ $1,000 / 2 MNQ $750** | 0.94 | 5 | **0.27** | 0.43 | 0.50 | 0.55 | 0.55 | **31** | 2.0 | $1,800 / $3,760 / $11,511 | 0.55 |
| 6 MNQ $1,000 / 3 MNQ $1,000 | 0.94 | 5 | 0.27 | 0.40 | 0.51 | 0.52 | 0.52 | 29 | 1.9 | $1,167 / $3,734 / $12,150 | 0.52 |
Read: eval funds in a median 5 sessions. Fast funded (2 MNQ): first payout median 31 sessions, 27 % of sets paid inside 30
sessions, 55 % ever; over 8 months a median set collects $1,800 and the mean $3,760, but 45 % of sets collect nothing. Safe
funded (1 MNQ): 69 % paid, median 48 sessions, 8-month median $3,005. Lucid Pro evals are a ONE-TIME fee (no monthly), so speed
is about cash flow, not fee bleed. Fees $300 vs mean 8-month payouts $3,100–3,760 (10–12×); the loss case is the $300.

### FULL MATRIX (2026-10-23; research/programs/out/matrix_full.txt, matrix_valid.txt, matrix.json): plan {25K, 50K} × N {6, 8} × eval size {3, 6, 10, max} × eval lock {750, 1000, none} × funded size {1, 2, 3} × funded lock {750, 1000}; both stages in rotation, NQ flags + cascade, DLL on; 72 start dates 2020-26, 8 months; halves = starts 2020-22 / 2023-26
**Validity cut, recorded:** the model caps a DAY's loss at the DLL after the trade has finished; when the flag stop at the eval size
exceeds the DLL (50K at 40 MNQ: $1,920 > $1,200; 25K at 20 MNQ: $960 > $600) the firm would flatten mid-trade at 15 pts and most
winners never happen, so those 72 cells are too kind and are EXCLUDED (they had topped every ranking). 216 valid designs.
| objective | best valid design | ≤ 45 | ever | median 1st | 8-mo mean / median | fee per paid set | h1 / h2 ever |
|---|---|---|---|---|---|---|---|
| speed | 25K N8 ($518) · eval 3 MNQ $1,000 · funded 3 MNQ $1,000 | 0.61 | 0.69 | 31 | $4,272 / $3,938 | $746 | 0.76 / 0.62 |
| certainty | 50K N8 ($400) · eval 3 MNQ no lock · funded 1 MNQ $1,000 | 0.15 | **0.86** | 66 | $3,829 / $3,661 | $465 | 0.92 / 0.79 |
| mean money | 50K N8 ($400) · eval 10 MNQ $750 · funded 3 MNQ $750 | 0.54 | 0.64 | 26 | **$5,851** / $3,290 | $626 | 0.68 / 0.59 |
| median money | 50K N8 ($400) · eval 6 MNQ $750 · funded 1 MNQ $750 | 0.32 | 0.83 | 52 | $4,416 / **$5,114** | $480 | 0.87 / 0.79 |
| **robust (rank-sum, stable halves)** | **50K N8 ($400) · eval 10 MNQ $750 · funded 2 MNQ $750** | 0.49 | 0.68 | 27 | $5,774 / $3,600 | $588 | 0.74 / 0.62 |
| ≤ $300 speed | 50K N6 · eval 10 MNQ $750 · funded 3 MNQ $750 | 0.47 | 0.54 | 24 | $4,124 / $1,800 | $554 | 0.61 / 0.47 |
| ≤ $300 certainty | 50K N6 · eval 6 MNQ no lock · funded 1 MNQ $1,000 | 0.36 | 0.74 | 46 | $3,179 / $3,414 | $408 | 0.74 / 0.74 |
Knob effects (medians over valid designs): plan 50K earns 60 % more and costs 25 % less per paid set than 25K; 8 accounts beat 6
on every column but cost; **eval size 10 MNQ ≥ 6 ≥ 3** on speed and money (the eval stop at 10 MNQ is $480 = 40 % of the DLL —
the upper edge of validity); eval lock $750 ≈ $1,000 ≫ none; **funded size is the speed/certainty dial** (1 MNQ: 69 % paid,
slow; 3 MNQ: 54 % paid, fast, most dollars); funded lock $750 ≈ $1,000. No single design is best on everything; the frontier
is: funded 1 MNQ for certainty, 2 MNQ as the compromise, 3 MNQ for speed and mean dollars.
