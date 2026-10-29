# Full-port plan — the flag book across many prop accounts (2026-10-23)

Founder decisions this week: "we are going full port", "max contracts", "day one we need to pass", "we can time the entry with
the flags", "so basically hold". The research below settles what each of those means in numbers; this plan is the shape that
survives them. Nothing in it is live until the founder says so, instance by instance.

## 1. What the data decided (all pre-registered, outputs in research/programs/out/)

| Question | Result | Source |
|---|---|---|
| Is the flag book a real edge or a selection artefact? | Real: base book DSR 0.996; the day's variant sweep is noise (PBO 0.86) | cscv_pbo.txt |
| How much evidence do 6,320 trades carry? | ≈ 2,415 independent observations (design effect 2.6); NQ and ES are one bet (daily corr 0.61, same side 96 %) | flag_dependence.txt |
| Does size help? | No. Value per eval: 1 MNQ $893–982 → 10 MNQ $230–274; 4 NQ: 0 passes in 555 attempts, account dead in 2 sessions | full_port_evalsize_mtm.txt, day1 sims |
| Can a day-one pass at 4 NQ be timed? | Only the 09:50 strong open lifts it, 21 % → 25 %. Flags, gaps, prior day, 10:00 / 10:30 paths: geometry. Flag entries = their random-minute twins (25 % vs 25 %) | day1_race.txt, day1_race_flags.txt |
| Long + short in two accounts? | Exactly one passes on ~49 % of days, never both; same cost per pass as one account on the strong open; and it is the hedging Tradeify FTA §6.7 / Vest ban | pair sims |
| Where does the edge live? | In HOLDING: stop beyond the flag (~24 pts), 3R target, run to 15:55. Forced to a 12.5-pt stop by a $1,000 daily limit it vanishes | day1_race_flags.txt |
| Cash profile at 1 MNQ + 1 MES, today's price | $595/month average, median $175, 47 % losing months, worst month −$4,777, worst drawdown $9,385; 2020-26 $932/month, 39 % losing | book_cash_profile |
| Best firm for the book | Topstep 50K $1,520 per eval, pass 42 %, 1st payout ~46 days; Lucid Flex 50K $1,164 / 41 % / 45 days; Select Daily 50K $1,221 / 34 % / 47 days; 25K and 150K plans are worse | candidate_by_firm.txt |

## 2. The plan

**Principle.** Size is fixed by the rules (1–2 MNQ eval, 1 MNQ funded, hold the flag's own stop). Aggression goes into the
NUMBER of accounts, all same-direction, staggered in time. Copies multiply dollars, not timing (flex-pipeline study).

**Accounts.** Start with 5 × Topstep 50K Combines (≈ $95/month each while in the Combine; no activation fee), staggered one
week apart. Expected 2.1 funded (42 %), P(none funded) 6.6 % staggered vs 58 % if all five start the same day. Add Lucid
Flex 50K as the second firm once the first Topstep account is funded (different firm = different payout clock, same
direction everywhere; no opposite positions anywhere, per §6.7). Tradeify Select stays for the dry books only until its eval
consistency rule is confirmed in writing.

**Sizing.** Eval: 2 MNQ (passes in ~22 days vs 33 at 1 MNQ, value $918 vs $982 — the small price for speed). Funded: 1 MNQ,
no exceptions; the trailing drawdown is $2,000 and the book's worst month at 1 micro is −$4,777, so a funded account WILL be
lost sometimes — that is why there are several and why they are staggered.

**Signals.** BOOK2 as it stands: 4-5m flags, pole ≥ 3×, pullback 5–38 %, grade A or B, flag-mid limit, stop beyond the flag,
3R target, flat 15:55, ≤ 2 open per market. No variant (T1/T4 passes are paper candidates only — PBO says the sweep is noise).
NQ and ES both, knowing they are one bet; ES adds trades, not diversification. Optional overlay with evidence: 2 MNQ instead of
1 when the open sits in the overnight top third / beyond yesterday (context_sizing PASS), never a third micro.

**Execution path (the project's own rules).**
1. Paper: register BOOK2 (+ the open-location sizing twin) as paper ids on the fleet; 20 trades / 3 sessions forward before
   any account is bought. The paper fleet and golden replay are the regression net (golden now pins its ATR input).
2. Dry: one `nq-executor-local@<label>` per account, `dry_run: true`, the same config the account will run; pre-RTH check
   09:00 ET, execution-watch, execution-audit 16:10 ET must be green for 3 sessions — and the audit timers must be repointed
   from `tradeify` to the live labels (they have been reporting a stopped instance since 2026-10-11).
3. Live: founder flips `dry_run` per instance; `--check-config`, restart, `[BROKER]` line verified before 09:30 ET.
4. Review weekly against the paper twin on the Leaderboard tab; a funded account that breaches is replaced from the eval
   pipeline, not re-sized.

**Budget and timeline.** Fees: 5 × ~$150 ≈ $750 for the first wave; first payout median ~46 trading days from purchase on
Topstep; expected value of the wave ≈ $7,600 over the funded life at 1 micro (model figure, in-sample marking). Worst realistic
outcome: wave fails (6.6 %), $750 lost, start the next wave.

## 3. What the plan refuses, and why
- 4 NQ / "max contracts": 0 passes in 555 attempts; daily limit = 12.5 pts = under one 1-minute bar.
- Long-short pairs: no better than one account on the strong open, and a contract breach.
- Hedge page / hedge_coordinator: stays unused (0 pairs, unit inactive) for the same reason.
- Variant hunting on the flag book: CSCV says stop; forward paper evidence is the only thing left that can move the book.

## 4. Open research that would change the plan
1. CSCV at account level (mtm daily $) — owed, unlikely to flip. 2. A second stream in different hours or a different driver
(the only route to lower account variance). 3. E2 absorption and E4 second detector (pre-registered, unrun). 4. Confirm each
firm's eval consistency rule, max accounts per household (Tradeify: 5), and copy-trading permission in writing.

## 5. Addendum 2026-10-23 (later the same day) — the eval-stage procedure, from two new tests
- **Rotation with a $500 daily lock replaces "stagger a week apart" as the eval procedure** (2026-10-23_rotation_lock.md):
  one account trades at a time; when its day reaches +$500 it stops and the next flag goes to the next account (at −$1,000 it
  hands over too). On the real trade sequence this turns five parallel copies (P(≥1 funded) 0.20, $825 a pass) into
  P(≥1 funded) 0.75, 1.4 passes per set of five, **$585–598 a pass**, because each account sees a different slice of the
  trades. **Size 2 MNQ**: same cost as 1 MNQ, first pass in ~33 sessions instead of ~72. Above 2 MNQ it degrades again.
- **In the eval, trade only flags filled before 11:00** (2026-10-23_flag_context_overlays.md): +0.32R vs +0.10R after 11:00
  in 2020-26, pass 33 % vs 23 %, $499 vs $723 a pass for a single account. The two eval rules are untested in combination —
  combine on paper first.
- **Once funded: the whole book, no lock, 1 MNQ.** Both eval rules trade dollars for passes; the funded account wants the dollars.
- Everything else in §2 stands (Topstep first, same direction everywhere, paper → dry → live on the founder's word).

## 6. Addendum, later 2026-10-23 — rotation optimised walk-forward (2026-10-23_rotation_lock.md Part 2)
- Lock **$750** rather than $500 (better at every size out of sample); hand-over at the −$1,000 daily limit, not earlier.
- **Do NOT combine the pre-11:00 filter with the rotation**: it starves the accounts of trades (P(≥1 funded) 0.80 → 0.69).
  Pre-11:00 is a single-account eval rule only.
- Three settings, founder's choice on cost vs certainty vs speed (all read out of sample, sets started 2020-26):
  N3 · $750 · 1 MNQ ≈ $380 a pass, 82 % odds of ≥1 funded, ~45 sessions · N5 · $750 · 2 MNQ ≈ $509, 80 %, ~28 sessions ·
  N8 · $500 · 2 MNQ ≈ $535, 91 %, ~42 sessions. Rotation order (next vs closest-to-target) does not matter.
- **Add the cascade to the rotation's trade stream** (2026-10-23_rotation_lock.md Part 3): it improves every setting out of
  sample. With both streams: N3 · $750 · 1 MNQ ≈ $398 a pass (74 % ≥1 funded, 43 sessions); N5 · $750 · 2 MNQ ≈ $485 (84 %,
  27 sessions); **N8 · $500 · 2 MNQ ≈ $470 a pass, 97 % odds of ≥1 funded and 81 % of ≥2, first pass ~38 sessions, $1,320 in fees.**

## 7. Addendum 2026-10-23 — Pro Eval rules and the payout clock (2026-10-23_payout_speed.md)
The Pro plans (1-day pass, no consistency cap, EOD drawdown, optional DLL, ~$50–65 a fee with coupons) change the shape:
- **Eval: 3 MNQ, rotation one-at-a-time, lock $750** → first pass in a median 3–4 sessions (25K) or 12–14 (50K), P(≥1 funded)
  0.9–0.99 with 5–8 accounts. More accounts active at once does NOT help (correlation).
- **Funded: 1 MNQ, NQ flags + cascade, DROP ES, no lock** → 50K pays within 45 sessions 74 % of the time and ever 84 %, median
  first payout 26 sessions (~5 weeks), ~$1,500 first payout per paid account, 29 % of funded accounts blown before paying.
- **50K over 25K**: the 25K funds in 4 sessions but its $1,000 room blows ~half the funded accounts before three winning days;
  payout-ever caps at ~0.6–0.7. 50K funded room is what carries the payout.
- Fees per PAID set with the coupon: ~$520 for 8 × 50K. Never reset ($120) — buy a fresh eval ($50).
- Open: the Pro FUNDED rules (buffer, min day, cap, split) are assumed — read them before buying.
- **Correction, same day, after reading LucidPro's funded rules**: the funded buffer ($2,100 non-withdrawable + $500 cycle
  minimum + 40 % best-day rule) puts the median first payout at **~60 sessions** with paid-ever 0.60 — not 26. The Pro eval is
  fast; the Pro funded stage is slow. Firm choice is now the open item: run the end-to-end clock on Topstep 50K and Lucid Flex
  50K exact rules (no buffer / 5-day rule) before buying anything.
- **Founder 2026-10-23: rotation with a profit lock applies to the FUNDED accounts too** (2026-10-23_payout_speed.md, funded
  rotation table). Funded stage = one funded account active at a time, $750 daily lock, NQ flags + cascade, DLL on, hand the
  next trade to the next funded account. Measured on LucidPro 50K exact rules: **1 MNQ → 79 % of sets paid (was 60 %),
  $505 per paid set, median first payout ~67 sessions; 2 MNQ → a third of sets paid within 45 sessions, median 43, 58 % paid
  ever.** Two funded accounts active at once is worse on every column. The eval stage is unchanged (3 MNQ, one active, $750).
- **Founder targets 2026-10-23: first payout within ~1 month, budget $300–400, as many MNQ as possible, rotation + profit
  lock both stages, flags + cascade.** Measured on LucidPro 50K exact rules: best within budget = 8 × 50K ($400), eval 6 MNQ
  lock $1,000, funded 4 MNQ lock $1,000, NQ + cascade, DLL on → P(paid ≤ 21 sessions) 0.17, ≤ 30 0.33, ≤ 45 0.50, ever 0.56,
  median 28 sessions, first payout ≈ $1,800 net. One month is a one-in-six outcome here because of Lucid's funded buffer;
  the firm search (no-buffer funded stage) is what can move it.
- **$300 version (6 × 50K):** eval 6 MNQ lock $1,000 in rotation; funded choice — 1 MNQ lock $750 for certainty (68 % of
  sets paid, E[first payouts] ≈ $1,740, median ~48 sessions) or 2–3 MNQ for speed (49 % paid, median 25–28 sessions,
  E ≈ $1,350). NQ + cascade, DLL on, one account active at a time in both stages.
- **25K vs 50K on $300 (2026-10-23):** the 25K is ~1 week faster to a first payout but pays half and blows more; expected first
  payouts $370–720 vs $1,350–1,740 for the same money. **Decision: 50K.** 6 × 50K, eval 6 MNQ lock $1,000; funded 3 MNQ lock
  $1,000 (speed, median ~25 sessions) or 1 MNQ lock $750 (certainty, 68 % of sets paid).
- **Firm (2026-10-23):** Rithmic-connected and modelled on exact/published rules: Lucid Pro ≈ Apex on speed (median ~28
  sessions at 6 MNQ eval / 2–3 MNQ funded), Lucid Pro pays more per payout ($1,800 vs $1,050–1,480); Lucid Flex is worse for
  this book; Topstep (no buffer) is not on Rithmic. **Pick Lucid Pro 50K; Apex if native Rithmic matters.** All of them carry a
  ~$2,100 funded buffer, so the one-month payout stays a 1-in-6 to 1-in-3 outcome; plan on ~6 weeks median.
- **Founder 2026-10-23: Apex cannot be used.** Firm choice is Lucid Pro 50K (Rithmic add-on route — confirm cost/availability before buying).

## 8. The procedure (2026-10-23, founder: "just do the simulation and tell me how we do it and get payouts … as fast as possible")
1. Buy 6 × Lucid Pro 50K (one-time ~$50 each with the coupon; no monthly fee), Rithmic add-on route, DLL ON ($1,200).
2. EVAL: one account trades at a time. Take every NQ flag (BOOK2) and cascade signal at **6 MNQ**. The moment the day is
   **+$1,000** or **−$1,200** (the DLL), that account is done for the day and the NEXT account takes the next signal. A $3,000
   day passes an account at once (1-day pass). Median: first account funded in 5 sessions; 94 % of sets get ≥ 1 funded.
3. FUNDED: funded accounts form their own rotation, one active at a time, **2 MNQ** (speed) or **1 MNQ** (certainty), lock at
   **+$750**, hand over at the DLL, NQ flags + cascade only (no ES), no size-ups. Request the payout the day the balance is
   ≥ $52,600 with the best day ≤ 40 % of the cycle's profit (first cap $2,000, later $2,500, 90 %).
4. A blown account is replaced with a fresh $50 eval, never a $120 reset; the new one joins the eval rotation.
5. Expect (2 MNQ funded): first payout median ~31 sessions, 27 % within 30, 55 % of sets ever paid, mean ~$3,760 over 8
   months (median $1,800; 45 % of sets collect nothing). (1 MNQ funded): 69 % paid, median 48 sessions, median $3,005.
6. Before step 1: confirm Lucid's Rithmic add-on cost and the funded rules page yourself; then paper → dry → live per §2.
- **Full matrix (216 valid designs, 2026-10-23):** eval **10 MNQ, lock $750**, 8 accounts, is the robust eval setting (not 6). Funded
  size is the dial: 1 MNQ = 83–86 % of sets paid, median ~52–66 sessions, median 8-month take ~$5,100; 2 MNQ = 68 %, ~27 sessions,
  mean $5,800; 3 MNQ = 64 %, ~26 sessions, mean $5,850 but median $3,300. With $300 (6 accounts) take eval 6–10 MNQ and funded
  1 MNQ (74 % paid) or 3 MNQ (54 %, 24 sessions). Cells with the eval stop above the daily limit (40 MNQ on 50K) are invalid in
  the model and were excluded — do not read matrix_full.txt's top rows as real.
