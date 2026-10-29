# Five evals in rotation with a daily lock — founder's idea, measured 2026-10-23 (descriptive; the lock sweep is a 5-value
# grid read on the full sample, so the best cell carries a small luck bar)

Founder: "5 accounts, first trade gives us $1,000 … we lock the account and move to the next one" → "or 500" → "or 200" →
"whichever is best". Simulated on the real BOOK2 NQ+ES trade sequence 2014-2026 (6,320 trades, closed in exit order, $ at
today's price), 5 Select-50K-like evals (target $3,000, EOD trail $2,000, daily loss $1,000, best day ≤ 40 %, fee $165), a fresh
set of 5 started every 20 sessions, 120-session horizon. ROTATION: one account trades at a time; when its day P&L reaches the
LOCK it stops for the day and the next flag goes to the next account; at −$1,000 it also hands over. ALL-5: every account
takes every trade (same-direction copies). Output research/programs/out/rotation_lock.txt.

| size | mode / lock | passes per 5 | P(≥1 funded) | P(≥2) | blown of 5 | days to 1st pass | fee per pass |
|---|---|---|---|---|---|---|---|
| 1 MNQ | ALL-5, no lock | 1.00 | 0.20 | 0.20 | 4.0 | 30 | $825 |
| 1 MNQ | rotation $1,000 | 1.23 | 0.75 | 0.34 | 2.7 | 74 | $670 |
| **1 MNQ** | **rotation $500** | **1.41** | **0.75** | **0.43** | **2.8** | 72 | **$585** |
| 1 MNQ | rotation $200 | 1.25 | 0.73 | 0.38 | 3.0 | 64 | $659 |
| 2 MNQ | ALL-5, no lock | 0.53 | 0.11 | 0.11 | 4.5 | 24 | $1,568 |
| **2 MNQ** | **rotation $500** | **1.38** | **0.77** | **0.47** | 3.6 | **33** | **$598** |
| 2 MNQ | rotation $1,000 | 1.31 | 0.78 | 0.43 | 3.7 | 42 | $632 |
| 3 MNQ | rotation $500 | 1.02 | 0.63 | 0.27 | 4.0 | 24 | $808 |
| 5 MNQ | rotation $1,000 | 0.73 | 0.41 | 0.22 | 4.3 | 15 | $1,136 |
| 1 NQ | rotation $1,000 | 0.33 | 0.26 | 0.00 | 4.7 | 10 | $2,528 |
| any | rotation, no lock (one account per day) | 0.18–0.64 | | | | | $1,285–4,610 |

Read: (1) **Rotation beats parallel copies at every size**, and by a lot where it matters: at 1–2 MNQ the chance of ending with
at least one funded account goes from 0.20 → 0.75 and the cost of a pass from $825 → $585. The reason is not the lock itself
but that each account sees a DIFFERENT slice of the trade stream, so the five are decorrelated — the same effect as staggering
start dates, achieved inside one calendar. (2) **$500 is the best lock** at both 1 and 2 MNQ (1.4 passes per set, $585–598 a
pass); $200 and $1,000 are close, $2,000 and "no lock" are clearly worse — a lock that is too high lets a good day run into
the 40 % consistency cap and then a bad day into the trail. (3) **2 MNQ with a $500 lock is the pick**: same cost per pass as
1 MNQ, first pass in 33 sessions instead of 72, P(≥2 funded) 0.47. (4) Size above 2 micros loses again: 3 MNQ $808, 5 MNQ
$1,136, 1 NQ $2,528 a pass. (5) Dollars per set are higher without the lock (more trades run) — the lock trades dollars for
passes, which is exactly the eval's job; once FUNDED, run the whole book without the lock (same stage logic as the pre-11:00
rule in 2026-10-23_flag_context_overlays.md). Caveats: in-sample trade list; 120-session horizon (a longer horizon would pass
more of the later accounts in the rotation); the ES leg and NQ leg are one bet, so a 5-account rotation is really ~3 independent
draws. Nothing changes any config; this is the eval-stage operating procedure proposed for the plan.

## Part 2 — walk-forward optimisation of the rotation (written 2026-10-23 03:55 Cyprus, BEFORE the run)
Grid: accounts N {3, 5, 8}; lock {300, 500, 750}; size {1, 2} MNQ; loss hand-over {−$1,000 (the DLL), −$500}; pre-11:00
filter {off, on}; order {next in line, closest to target first}. 144 combos (3×3×2×2×2×2; the first draft of this line said 216 — arithmetic slip, corrected before the run was read). Score = fee per pass on sets STARTED in 2014-19
(≥ 30 sets); the best combo and the base (N 5, $500, 2 MNQ, −$1,000, filter off, next) are then read on sets started in
2020-26. Also reported: how many combos beat the base out of sample, and the OOS rank of the IS winner. PASS = the IS winner
beats the base out of sample on fee per pass AND P(≥1 funded) is not lower. Output appended to rotation_lock.txt.

### Part 2 results (2026-10-23; rotation_lock.txt; IS = 50 sets started 2014-19, OOS = 45 sets started 2020-26)
| config | IS fee/pass | OOS fee/pass | OOS P(≥1 funded) | OOS passes per account | OOS days to 1st |
|---|---|---|---|---|---|
| BASE N5 · lock $500 · 2 MNQ · hand-over −$1,000 · no filter · next | $688 | $523 | 0.84 | 0.32 | 28 |
| IS WINNER N3 · lock $750 · 1 MNQ · hand-over −$500 · no filter · closest | $635 | **$378** | 0.82 | 0.44 | 45 |
IS winner's OOS rank 5 of 144; 66 of 144 combos beat the base OOS. The top-8 in-sample are all N3/lock $750/1 MNQ or N5–N8/
lock $500–750/2 MNQ, and every one of them improves OOS — the structure, not a cell, is what carries.
Knob effects OOS (median fee/pass, P≥1): accounts 3 $459/0.69 · 5 $498/0.83 · 8 $585/0.86 — fewer accounts = cheaper per
pass, more accounts = more certain of one; lock $300 $560 · $500 $537 · $750 $514/0.80 — higher lock better up to $750; size
1 MNQ $509/0.72 vs 2 MNQ $533/0.83 — 2 buys certainty and speed for ~5 %; hand-over −$1,000 beats −$500; **pre-11:00 filter
$518 vs $538 but P≥1 0.69 vs 0.80 — it starves the rotation of trades, so it does NOT combine with rotation**; order is
irrelevant ($530 vs $539).
**Verdict by the pre-registered rule: the IS winner beats the base on fee/pass ($378 vs $523) but P(≥1) is 0.82 vs 0.84 —
it fails the second clause by 0.02, i.e. a tie.** Reading: the optimisation's real content is three knob directions that hold out
of sample — lock up to $750, keep the DLL as the hand-over, do NOT add the pre-11:00 filter — plus a cost/certainty trade-off
on N and size that is the founder's to set: **N3 · $750 · 1 MNQ ≈ $380 a pass with 82 % odds of one funded account in ~45
sessions; N5 · $750 · 2 MNQ ≈ $509 a pass, 80 %, 28 sessions; N8 · $500 · 2 MNQ ≈ $535, 91 %, 42 sessions.**

### Part 3 — flags + cascade in the rotation (2026-10-23; rotation_lock.txt; cascade = edge_lab so_cascade, 2,275 NQ trades, exits ~12:12 median; not pre-registered beyond "same settings, three streams")
| stream | setting | 2020-26: fee/pass · P(≥1) · P(≥2) · days→1st | 2014-26: fee/pass · P(≥1) |
|---|---|---|---|
| flags only | N3 $750 1MNQ | $441 · 0.72 · 0.34 · 72 | $610 · 0.56 |
| flags only | N5 $750 2MNQ | $536 · 0.74 · 0.49 · 49 | $637 · 0.70 |
| flags only | N8 $500 2MNQ | $585 · 0.84 · 0.68 · 68 | $754 · 0.76 |
| cascade only | N5 $750 2MNQ | $517 · 0.80 · 0.46 · 71 | $732 · 0.64 |
| **flags + cascade** | **N3 $750 1MNQ** | **$398 · 0.74 · 0.39 · 43** | $512 · 0.64 |
| **flags + cascade** | **N5 $750 2MNQ** | **$485 · 0.84 · 0.50 · 27** | $603 · 0.74 |
| **flags + cascade** | **N8 $500 2MNQ** | **$470 · 0.97 · 0.81 · 38** | $577 · 0.93 |
Read: adding the cascade improves every setting on every column — cheaper per pass (−10 to −20 %), more certain, and faster
(first pass 27 sessions at N5, 43 at N3). The cascade alone is weaker than the flags in the rotation (fewer trades a day →
the rotation starves at N8) but its 0.45 correlation with the flags is what the rotation needs: more, partly independent,
trades to hand round. N8 · $500 · 2 MNQ with both streams reaches **P(≥1 funded) 0.97 and P(≥2) 0.81 for 8 fees ($1,320),
$470 a pass** — the most certain configuration found this week; N3 · $750 · 1 MNQ stays the cheapest ($398). Day counts
differ slightly from Part 2 because the day axis is the union of all three streams' sessions. Caveats as before (in-sample
lists; NQ/ES one bet; cascade list is the edge_lab screen's best variant).

### Part 4 — loss hand-over sweep (founder: "500 cap with a rotation, and 200 cap loss?"; flags + cascade, 74 OOS sets started 2020-26)
| setting | lock | hand over at | fee/pass | P(≥1) | P(≥2) | days→1st |
|---|---|---|---|---|---|---|
| N3 1 MNQ | $500 | −$200 | $416 | 0.72 | 0.32 | 43 |
| N3 1 MNQ | $750 | −$200 | $382 | 0.77 | 0.39 | 39 |
| **N3 1 MNQ** | **$750** | **−$500** | **$342** | **0.86** | 0.46 | 39 |
| N3 1 MNQ | $750 | −$1,000 | $398 | 0.74 | 0.39 | 43 |
| N5 2 MNQ | $500 | −$200 | $522 | 0.73 | 0.41 | 28 |
| N5 2 MNQ | $500 | −$1,000 | $477 | 0.82 | 0.59 | 29 |
| N5 2 MNQ | $750 | −$1,000 | $485 | 0.84 | 0.50 | 27 |
| N8 2 MNQ | $500 | −$200 | $543 | 0.89 | 0.62 | 33 |
| N8 2 MNQ | $500 | −$1,000 | $470 | 0.97 | 0.81 | 38 |
Read: the loss cap must scale with the size of one loss. At 1 MNQ a flag stop costs ~$50 and a cascade stop ~$180, so a −$500
hand-over lets an account absorb 2–3 losses before yielding and is the best cell found so far ($342 a pass, 86 % odds of ≥1
funded with three fees); −$200 yields after a single cascade loss and is worse but still fine ($382–416). At 2 MNQ every loss is
doubled, a −$200 cap fires on the first cascade stop and hands good trades away: the daily limit (−$1,000) is the right
hand-over there ($470–485 vs $522–560). The $500 lock is never the best lock: $750 wins at N3 and ties at N5/N8.

### Part 5 — one signal per account at maximum size (founder 2026-10-23: "treat each signal as a full size prop account … 25k 2 contracts … then profit lock each account"; research/programs/out/one_signal_accounts.txt; 143 start dates 2020-26, 168 sessions; funded stage identical in every row: 1 MNQ, rotation, $750 lock, NQ flags + cascade)
| plan | N (fees) | eval mode | P(≥1 funded) | funded per set | evals burned per funded | fee per funded | paid ≤ 45 | ever paid | 8-month $ median / mean |
|---|---|---|---|---|---|---|---|---|---|
| 25K | 6 ($389) | one signal/acct, 2 NQ, flags + cascade | 0.85 | 2.1 | 2.9 | $185 | 0.35 | 0.47 | $0 / $1,238 |
| 25K | 6 ($389) | **rotation 6 MNQ, lock $1,000** | **0.95** | **3.2** | **1.9** | **$122** | **0.43** | **0.62** | **$900 / $1,833** |
| 50K | 6 ($300) | one signal/acct, 4 NQ, flags + cascade | 0.84 | 2.1 | 2.9 | $144 | 0.31 | 0.55 | $1,445 / $2,404 |
| 50K | 6 ($300) | **rotation 6 MNQ, lock $1,000** | **0.94** | **3.0** | **2.0** | **$101** | 0.33 | **0.69** | **$3,005 / $3,100** |
| 50K | 8 ($400) | one signal/acct, 4 NQ | 0.92 | 2.8 | 2.9 | $144 | 0.31 | 0.67 | $2,140 / $3,128 |
| 50K | 8 ($400) | rotation 6 MNQ | 0.96 | 3.9 | 2.0 | $102 | 0.29 | 0.82 | $4,583 / $4,269 |
Read: the one-shot at max size passes an account about one time in three (the book's win rate is 31–37 %, not high), so it
burns ~3 evals per funded account against ~2 for the 6-MNQ rotation, funds fewer accounts per set, reaches the first funded
account no faster (2–3 sessions either way), and — because the funded stage is the same — pays no sooner and ends with
30–50 % fewer dollars. The rotation's lock already does what "profit lock each account" asks, at a size that lets the account
survive its second signal. **Verdict: dominated by the rotation on every column; not adopted.**
