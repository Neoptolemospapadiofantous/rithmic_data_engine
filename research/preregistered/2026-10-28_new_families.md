# New strategy families — pre-registered 2026-10-28

Founder: "can we explore other families?" → "test all now". Written BEFORE any run. Tool: `build-exp/edge_lab run` on
`data/edge_lab/cache` (read-only; cache exported 2026-10-16, NQ/ES 1m 2010-07 → 2026-10-15, NQ book 2023-03 → 2026-03,
BTCUSDT 2020-01 →, macro.csv, releases.csv), reports to the session scratchpad (`nf/out`), nothing written under
`data/edge_lab/` and no Postgres load. Specs: `research/ideas/nf_*.json`. In-sample = 2010-07 → 2024-12 (`"to"`);
an idea that PASSES all five edge_lab gates gets ONE look at the sealed holdout 2025-01 → 2026-10 (the same spec with
`to` removed, read from the per-year table: ≥ 30 trades and net > 0 = holdout PASS). Costs are edge_lab's real round trip
(NQ 1 pt, ES 0.35, BTC 10 bp) and every gate as in `src/research/edge_lab.hpp`. Multiple testing: 22 specs, so expect
~1 PASS by luck at the screen's 5 % — a family counts only if its PASS survives controls, robustness AND the holdout, and
the SKEPTIC question "is it the volatility bet again" is answered in the results section.

Why these and not more parameters: the 2026-10-16 sweep closed every indicator / pattern family and the 10-13 → 10-23 flow
batches closed LETF, CTA, vol-target, quarter-end, GEX, opex, earnings, absorption, Gao momentum. Each family below uses
INFORMATION no PASS idea uses today (a second market as the trigger, the resting book, the release clock, the macro tape,
the overnight path, the vol term structure, the holiday calendar).

## F1 — Cross-market lead/lag (pair ES, pair BTCUSDT)
- `nf_xm_es_lead`: at 09:50 / 10:30, ES has moved ≥ k × its ATR from its open (`|xs| ≥ k`) while NQ has not (`|px−open| < k/2 × ATR`)
  → NQ follows ES (dir sign(xs)); k ∈ {0.2, 0.3}, stop {0.25, 0.35} ATR, 2R, flat 15:55. Hypothesis: broad-index flow leads.
- `nf_xm_rs_follow` / `nf_xm_rs_fade`: at 10:00, NQ's move from the open minus ES's (both in own ATR) ≥ z → follow NQ's excess /
  fade it; z ∈ {0.15, 0.25, 0.35}, stop {0.25, 0.35}, target {0, 2}. The fade is the single-leg proxy for the NQ–ES spread
  mean-reversion family (F7): a two-leg program is not an edge_lab idea; the proxy decides whether it is worth building.
- `nf_xm_btc_catchup`: at 09:30, BTC's return since NQ's prior RTH close ≥ z while NQ's own gap is small (`|gap| < 0.15`) →
  NQ catches up (dir sign(xr)); z ∈ {1.5 %, 2.5 %}, stop {0.35, 0.5}, no target, from 2020-01-06, min 60 trades.
  (`btc_weekend_monday` tested the Monday-only version; this is every day with the NQ-has-not-moved condition.)
PASS = five gates + holdout.

## F2 — Resting-book state as a volatility (not direction) signal (NQ book 2023-03 →, train 6 months, min 60)
- `nf_bkv_thin_so`: strong-open follow at 09:50 / 10:30 (k ∈ {0.2, 0.25}) only when the resting depth within 10 bp over the last
  15 minutes is ≤ d × the last 90 minutes' (d ∈ {0.7, 0.85}) — a thin book lets the move extend.
- `nf_bkv_cxl_so`: the same with cancel-to-add over 15 min ≥ c × its 90-min mean (c ∈ {1.15, 1.3}) — a fragile book.
- `nf_bkv_thin_gor`: gap_or_retest's scan with the thin-depth gate at the retest bar (d ∈ {0.7, 0.85}, stop 0.35, 2R).
PASS = five gates + holdout (holdout here is 2025-01 → 2026-03, the end of the book data).

**Amendment 2026-10-28 15:50 EET, before any full run was read:** a quick-look smoke test showed the F2 specs firing on fewer than 60
days because the 90-minute depth / cancel baseline at 09:50 straddles the pre-market, where the book is always thinner than in
RTH — "thin vs baseline" could not fire. Changed before any result: checks at 10:30 / 11:00 (k2 ∈ {0.25, 0.3}) against a
60-minute baseline that lies inside RTH (09:30 →), and the gap_or_retest scan from 10:35. Thresholds d / c unchanged.

## F3 — Scheduled-release reaction (CPI / PPI / NFP, 08:30 ET; `releases.csv`, 200 of each)
- `nf_rel_follow`: at 08:40 on a release day, |px − 08:30 open| ≥ k × ATR → follow the 10-minute reaction to 15:55; k ∈ {0.1, 0.2, 0.3},
  stop {0.25, 0.35}, no target.
- `nf_rel_fade`: the mirror, flat 10:30 (the knee-jerk reverses into the cash open).
- `nf_rel_open_follow`: at 09:35 on a release day, |gap| ≥ k (k ∈ {0.15, 0.3}) → follow the gap (release-driven repositioning
  continues); stop {0.25, 0.35}, target {0, 2}. (Earlier work only tested SKIPPING these days.)
PASS = five gates + holdout; min 60 trades.

## F4 — Macro-tape re-hedge (`y10c` = yesterday's 10-year change in bp, `usdc` = broad dollar daily change %, macro.csv)
- `nf_macro_y10_rehedge`: at 10:30, |y10c| ≥ b (b ∈ {7, 10} bp) and the cash session is already moving against the yield shock
  (sign(px−open) = −sign(y10c)) → follow that direction to 15:55 (vol-target / risk-parity de-risking after a rates shock);
  stop {0.25, 0.35}, target {0, 2}.
- `nf_macro_usd_rehedge`: the same with the dollar (|usdc| ≥ u, u ∈ {0.4, 0.6} %).
- `nf_macro_y10_open`: at 09:35, |y10c| ≥ b → −sign(y10c) with no intraday condition (the plain next-day effect). min 60.
The desk proposed `macro_shock_follow` on 10-19 and never judged it; this is that test.

## F5 — Overnight gap decomposition (which session built the gap; `at(3,0)` = the 03:00 ET bar, London's first hour)
- `nf_gapd_london`: at 09:35, |gap| ≥ 0.15 ATR and the London part (open − 03:00) carries ≥ f of the gap in its direction
  (f ∈ {0.6, 0.8}) → follow the gap; stop {0.25, 0.35}, target {0, 2}.
- `nf_gapd_asia`: the complement — the Asia part (03:00 − prior close) carries ≥ f of the gap. The pair says whose gap continues.
- `nf_gapd_london_gor`: gap_or_retest's scan with the London-built filter (f ∈ {0.6, 0.8}, stop 0.35, 2R).
PASS = five gates + holdout. Expected: at most one of london / asia passes; both passing = it is just the gap (known).

## F6 — Volatility term structure as the regime switch (`vts` = VIX / VIX3M prior close, `vvix`)
- `nf_vts_weak_day`: weak close (pibs ≤ 0.3) and backwardation vts ≥ v (v ∈ {0.9, 0.95, 1.0, 1.05}) → buy 09:30, sell 15:55, no stop.
- `nf_vvix_weak_day`: the same gated on VVIX ≥ w (w ∈ {90, 100, 110}).
- `nf_vts_cascade`: the cascade plateau (k1 0.25 / k2 0.3 / stop 0.25 / 3R) only when vts ≥ v (v ∈ {0.9, 1.0}).
PASS = five gates + holdout AND — the SKEPTIC test — the vts-gated weak day must beat `weak_day_long` (v = 20 VIX) on the same
years by ≥ 2 pts/trade, or it is the known volatility bet in new clothes and is NOT adopted.

## F7 — NQ–ES spread mean reversion
Not an edge_lab idea (one position at a time). Decided by `nf_xm_rs_fade` (F1): if the single-leg fade of NQ's excess move
passes, a two-leg program and a prop-rule check (same-account spread vs Tradeify FTA §6.7) follow; if it fails, the family is closed.

## F8 — Calendar leftovers
- `nf_seas_prehol`: the session before an exchange holiday (`prehol`) → long 09:30 → 15:55, stop {0, 0.5 ATR}.
- `nf_seas_yearturn`: last two sessions of December / first two of January → long, stop {0, 0.5}; min 40.
- `nf_seas_month_first`: the first trading day of the month (`tdom ≤ 1`) → long, stop {0, 0.5}; min 60.
PASS = five gates + holdout.

## Not run (closed earlier, listed so they are not re-proposed)
Opex / triple witching direction (none, 10-13), turn-of-month VWAP fade (desk FAIL), LETF close (twice), pre-FOMC (N16), FOMC /
CPI / NFP SKIPS (10-23 FAIL), book imbalance as a trigger (09-25), absorption (×7), Gao momentum, box fades, every indicator family.

## Results (run 2026-10-28 15:55–16:20 EET, after the text above; reports in the session scratchpad `nf/out`, `nf/out_ho`, `nf/diag`)
Costs: NQ 1.0 pt round trip in every number; the measured LIVE round trip is ≈ 1.7 pts (ORB entries 4.7 ticks + exits 1.8 + $1.82),
which edge_lab's cost × 1.5 robustness check (1.5 pts) nearly covers. **21 of 22 FAIL, 1 PASS, and the PASS does not survive its control.**

| idea | verdict | walk-forward (pts/trade, n, PF) | why it fails |
|---|---|---|---|
| nf_xm_es_lead | FAIL at CONTROLS | +5.23, 104, 1.22 | random day 58 %, era 2010-18 negative |
| **nf_xm_rs_follow** | **PASS** (5/5 gates; holdout 2025-26: 68 trades, +745 pts → PASS) | +9.69, 706, 1.40, t 3.0 | see the control below |
| nf_xm_rs_fade (F7 proxy) | FAIL at SCREEN | −7.13, 353, 0.63 | fading NQ's excess loses outright → the NQ–ES spread family is CLOSED |
| nf_xm_btc_catchup | FAIL at WALK-FORWARD | −4.73, 76 | — |
| nf_bkv_thin_so / cxl_so / thin_gor | FAIL at SCREEN (0 trades) | — | see the F2 diagnosis |
| nf_rel_follow | FAIL at SCREEN | −6.18, 86, 0.73 | the 08:30 reaction does NOT continue |
| nf_rel_fade | FAIL at WALK-FORWARD | +0.64, 132, 1.03 | nor does it reverse |
| nf_rel_open_follow | FAIL at WALK-FORWARD | +0.44, 183, 1.02 | a release-day gap is an ordinary gap |
| nf_macro_y10_rehedge / usd_rehedge | FAIL at WALK-FORWARD | +1.32 / −0.64 | — |
| nf_macro_y10_open | FAIL at CONTROLS | +8.18, 275, 1.20 | random time 100 % but random DIRECTION only 64 % — the yield sign carries no direction |
| nf_gapd_london | FAIL at CONTROLS | +2.25, 590, 1.09 | — |
| nf_gapd_asia | FAIL at CONTROLS | +3.29, 619, 1.13 | — |
| nf_gapd_london_gor | FAIL at CONTROLS | +8.64, 422, 1.36 | random day 97 %, random time 16 %: it is gap_or_retest (+9.0) minus trades — the London filter adds nothing |
| nf_vts_weak_day | FAIL at CONTROLS | +4.33, 276, 1.11 | WORSE than weak_day_long on VIX ≥ 20 (+13.2): the term structure is a weaker fear gauge |
| nf_vvix_weak_day | FAIL at CONTROLS | +6.65, 378, 1.16 | same |
| nf_vts_cascade | FAIL at CONTROLS | +4.57, 726, 1.20 | ≈ so_cascade (+4.8): the gate removes nothing |
| nf_seas_prehol | FAIL at CONTROLS | +8.15, 123, 1.29, 7/7 robustness | random day 81 %: pre-holiday sessions are not better than other sessions |
| nf_seas_yearturn | FAIL at SCREEN | −23.3, 22 | — |
| nf_seas_month_first | FAIL at WALK-FORWARD | −13.1, 82 | — |

**The one PASS, read against its pre-registered control.** `nf_xm_rs_ctl` (the same grid with NO ES — follow NQ's own move
from the open at 10:00) also PASSES 5/5, 7/7 robustness, all three eras positive: +5.28 pts/trade on 1,383 walk-forward trades =
**+7,298 pts total vs the pair version's +6,843 on 706 trades**. Trade by trade (walk-forward files): on the 408 days both trade
they agree in direction on 367 and the control earns MORE (+3,829 vs +2,838); on the 41 days they disagree the pair loses −372
while the control makes +149; the 975 days only the control trades earn +3,468. So the ES term is a FILTER that halves the
trades and raises the per-trade figure (which is what lifts the prop fit, $1,571 vs $941 break-even) while REMOVING total
edge, and it makes the 2010-18 era negative (−0.28 vs +1.83). The per-trade gap (4.4 pts) clears the amendment's 2-pt line
on its face, but the total and the common-day comparison say what the family is: **the known 10:00 momentum
(`m1_10am_all`, strong_open) with a selective filter — not a cross-market edge. NOT adopted as a new family.** If anything
is taken forward it is the CONTROL (`nf_xm_rs_ctl`: 10:00, |NQ move| ≥ 0.15 ATR, stop 0.25 ATR, no target, flat 15:55),
which is the plainest statement yet of the 10:00 momentum edge and would be a candidate to register on paper beside
m1_10am_all — multiple-testing caveat: it was run as a control, not pre-registered as an idea.

**F2 diagnosis (book state).** `bk()` inputs are present (`bk(bidq,15) > 0` fires on every flow day: diag specs bk1-3 give identical
trade sets, +7.26 pts/trade, t 1.27). The ratio gates never fired because resting depth within 10 bp RISES through the morning
on almost every day: at 10:30, depth-15m ≤ 1.0 × depth-60m happens 7 times a year, ≤ 1.2 × 39 times a year. "Thin vs the
day's own earlier book" is therefore not a usable definition; a proper test needs depth normalised by the same minute on
prior days (a cross-day book baseline), which the spec language cannot express. **F2 = NOT TESTED, needs code** (a
`bkrel(field, n)` = today's n-minute mean ÷ the 20-day mean at this minute), not a FAIL.

**Multiple testing.** 22 pre-registered specs + 1 control + 6 diagnostics; one PASS at the screen's 5 % is what luck predicts,
and the one PASS is explained by an existing family. Nothing here is a new family; the two leads that reached CONTROLS with
7/7 robustness (nf_seas_prehol, nf_macro_y10_open, nf_vts_cascade) all failed the random-day or random-direction control,
i.e. they are the market's drift / the cascade, not the stated mechanism.

**Amendment 2026-10-28 16:05 EET, after the 22 verdicts and before the holdout:** `nf_xm_rs_follow` is the only PASS. Before it is
read as a family, a CONTROL twin runs with the same grid and no ES at all (`nf_xm_rs_ctl`: at 10:00, |NQ move from its open| ≥ z
in ATR → follow NQ) — if the twin's walk-forward is within 2 pts/trade of the pair version, the ES information adds nothing and
the PASS is the known 10:00 momentum (m1_10am_all), not a cross-market edge. Then ONE holdout look (2025-01 → 2026-10).
Also three diagnostic specs (`nf_diag_bk*`, scratch only) to find out why the F2 book specs fired 0 trades — a data/plumbing
question, not a new test; F2's verdict stays FAIL at SCREEN unless the diagnosis shows the `bk()` inputs were absent.

## F2 v2 — book state with a cross-day baseline (pre-registered 2026-10-28 17:10 EET, before any run)
`bkrel(field, n)` added to edge_lab (src/research/edge_lab.hpp): bk(field, n) at the bar ÷ the mean of the same n-minute window
at the same wall-clock minute over the previous 20 book days (NaN under 5 days). Specs `nf_bkv_thin_so_v2` / `nf_bkv_cxl_so_v2`
(checks 09:50 / 10:30, k1 ∈ {0.2, 0.25}, k2 ∈ {0.25, 0.3}; thin = mean of bid/ask relative depth ≤ d ∈ {0.7, 0.85}; fragile =
relative cancel-to-add ≥ c ∈ {1.15, 1.3}; stop {0.25, 0.35} ATR, 2R) and `nf_bkv_thin_gor_v2` (gap_or_retest scan from 09:35 with
the thin gate, d ∈ {0.7, 0.85}). Same PASS rule as F2 (five gates, holdout 2025-01 → 2026-03). Sanity rule written first: a v2
spec must fire on ≥ 60 days in-sample or the family stays NOT TESTED (then the definition, not the market, is at fault).

**F2 v2 result (17:25 EET):** `nf_bkv_thin_so_v2` 25 trades (7/yr), `nf_bkv_cxl_so_v2` 0, `nf_bkv_thin_gor_v2` 17 — all under the 60-day
sanity floor → definitions at fault again: relative cancel-to-add sits in a tight band around 1.0 (≥ 1.05 never happens) and relative
depth ≤ 0.85 is a 7-day-a-year event. Threshold-sizing diagnostics (8 scratch specs at 10:30, viewed before writing v3 — this IS a
threshold search and is counted as such): depth ≤ 0.9 / 0.95 / 1.0 / 1.05 fire 8.6 / 13.4 / 18.9 / 24.0 days a year; cancel ≥ 0.95 /
1.0 / 1.05 fire 38.9 / 17.4 / 0 — against 39.4 unconditioned flow days. Per-trade nets on those slices are 2–4× the unconditioned
+7.9 but on 15–45 trades each.

## F2 v3 — the median split (pre-registered 17:30 EET, before its run)
The least-tuned choice, not the best-looking one: thin = relative depth ≤ 1.0 (the lower half of days), fragile = relative
cancel-to-add ≥ 1.0 (the upper half). `nf_bkv_thin_so_v3` / `nf_bkv_cxl_so_v3`: checks 09:50 / 10:30, k1 ∈ {0.2, 0.25}, k2 ∈ {0.25, 0.3},
stop {0.25, 0.35}, 2R, from 2023-03-01, min 60 trades. PASS = five gates + holdout 2025-01 → 2026-03, AND the discount for having
looked at 8 thresholds: a PASS here is "suggestive, re-test when the collector's own book history reaches 3 years", never a
paper registration on its own.

**F2 v3 result (17:45 EET), reports in scratchpad `nf/f3`, `nf/f2diag`:** both FAIL at CONTROLS — narrowly, and the shape is worth
recording. `nf_bkv_thin_so_v3`: screen pass (t 2.86), walk-forward 94 trades, **+32.7 pts/trade, PF 1.92, t 2.8**, robustness 4/5,
controls random direction 96 % / time 100 % / day 97 % (need 99 %). `nf_bkv_cxl_so_v3`: WF 72 trades, +35.5, PF 2.00, t 2.6,
controls 96 / 98 / 96 %. The complements, run as diagnostics: thick-book days 97 trades **−1.1 pts/trade** (PF 0.97); calm-cancel
days 125 trades +9.5 (PF 1.27). So the strong-open follow earns ~+33 on thin / fragile-book days and ~0 on thick days — the
split carries information — but on 1.8 years of book data (2023-03 → 2024-12) the samples are 72–94 trades, eight thresholds were
viewed, and the 96 % control is 3 points short of the gate. **Verdict: F2 = SUGGESTIVE, NOT PASSED. No holdout look (the rule
allows one only for a PASS). Re-run `nf_bkv_thin_so_v3` unchanged when the cache's book history reaches 3 years** (the collector
has recorded the book since 2026-09-19; export via `scripts/edge_lab.sh export` and the MBO months) — a PASS then, with the
holdout, would be the first new family since the cascade. `bkrel()` stays in edge_lab (src/research/edge_lab.hpp).

**Cost sensitivity (same day, scratchpad `nf/cost`):** the five book families re-run at 1.2 and 1.7 pts (the measured live round
trip): all five still PASS; walk-forward totals fall 2–17 % (gap_or_retest 11,017 → 10,408 pts; so_cascade 11,146 → 9,208; weak_day
10,877 → 10,470; monday 3,486 → 3,615; cascade_gap_agree 7,705 → 7,065). The book survives live cost; cutting it is worth ~10 %.

**Paper registration (same day):** `mom1000_k15` (= `nf_xm_rs_ctl`, opening_drive 30 min / 0.15 ATR / 10:00–10:05 / stop 0.25 ATR /
no target) added to config/paper_fleet.json and as a working-fleet candidate; paper-engine-local restarts 23:10 EET via the transient
timer `paper-engine-restart-20261028` (refuses inside RTH). pd_gap_1000 / pd_mom_1000 were found already running (engine built
2026-10-27, path_dir mode active) with no signal yet — not a gap.

**Paper registrations, evening (founder: "we need more trades"):** `es_gap_or_retest`, `es_monday_long`, `es_mom1000_k15` on the ES
fleet (ATR-relative knobs unchanged, sl placeholder × 0.144) and `cascade_asia_2h` on NQ (the 18:00-reopen cascade, corr −0.08 with
the book) — all `candidate` tier in config/working_fleet.json, judged by its ≥ 40-trade rule. Engines restart 23:10 / 23:12 EET
(transient timers, refuse inside RTH). Expected book rate after this: NQ ~1.3 + ES ~0.9 ≈ 2.2 signals/day at 0.2–0.5 correlation.

## Rule arbitrage — the same book through every modelled prop plan (run 2026-10-28 evening, scratchpad `nf/plans`)
Founder: "we must find more ways to increase it". Not a strategy test: edge_lab's prop chain (walk-forward trades → eval → funded
until breach → new eval, MNQ at 1.5 pts cost, MES 0.6) for each family at 1 / 2 / 4 / 6 micros on all 12 plans in `payout_sim.hpp`.
"paid $/yr" is gross of eval fees; "be fee" = the eval fee at which the plan nets zero. Apex / Topstep / Lucid rules are the
third-party summaries in payout_sim.hpp (prop_pipeline.cpp holds the official Lucid / Topstep reads) — a ranking, not a quote.

| plan (one account per family, best size each) | paid $/yr | payouts/yr | evals/yr | pass % |
|---|---|---|---|---|
| **topstep_50k** | **10,572** | 5.6 | 12.3 | 35 |
| **lucid_flex_50k** | **10,527** | 6.4 | 14.5 | 36 |
| apex_eod_50k | 9,203 | 4.7 | 6.3 | 36 |
| select_flex_50k | 7,852 | 4.5 | 14.0 | 31 |
| growth_50k | 7,445 | 2.9 | 7.2 | 39 |
| select_daily_50k | 6,839 | 8.0 | 19.0 | 26 |
| mff_rapid / rapid_eod 50k | 6,417 / 6,668 | 6.4 / 5.7 | 12.8 / 16.9 | 35 / 21 |
| select_daily_25k | 5,376 | 10.2 | 27.1 | 21 |
| select_flex_25k | 4,699 | 5.0 | 22.0 | 24 |
| **growth_25k (the live account's plan)** | **2,801** | 2.9 | 12.5 | 35 |

One account, the whole book as "first setup of the day, 2R" (`union_2r`): topstep 5,498 / lucid_flex 4,719 / apex_eod 3,321 /
select_flex 2,506 / select_daily_50k 2,848 / select_daily_25k 1,727 / growth_25k 1,143 $/yr. Best single earner: gap_or_retest on
Topstep at 6 MNQ, $5,224/yr, 2.6 payouts/yr, be fee $2,123. ES families on their own account add $2–3k/yr each on Topstep / Lucid.
**Read:** the firm's rule set is worth 2–4× — Growth 25K is the worst plan modelled for this book, Topstep / Lucid Flex 50K the
best; the best size under re-buyable evals is the largest tested (6 MNQ) because a breach costs one eval fee, which is the
opposite of the funded-account answer (size 1) — eval big, funded small, as 2026-10-15 found. Even at the best rules a $2–3k/month
target is ~5–6 accounts; one account tops out near $5.5k/yr gross.

## Eval policy — pass the eval on the book's best days, at eval size (pre-registered 2026-10-28 evening, before the code or any run)
Founder: "we need more". The prop chain gains an eval-phase policy (src/research/edge_lab.hpp `prop_chain`): (a) `eval_size`
separate from the funded `size` (eval big, funded small — the 2026-10-15 finding, now testable per plan); (b) `eval_days` — during
the EVAL only, trade on a named day class known before the open: `fear` (prior VIX ≥ 20), `weak` (prior IBS ≤ 0.3), `fear_or_weak`,
`straight` (e5 ≥ 0.55, a straight prior week), `any`; the funded account trades every day as before; (c) `eval_atr_frac` f > 0 — eval
size = round(f × eval_target ÷ (prior ATR × $/pt per micro)), clamped to [1, eval_size], i.e. sized so that f of one ATR pays the
target, instead of a fixed size. Specs: gap_or_retest, so_cascade, union_2r × plans topstep_50k, lucid_flex_50k × policies
{baseline eval 6 / funded 2, eval_days fear / weak / fear_or_weak / straight at eval 6 / funded 2, eval_atr_frac 0.5 / 1.0 at
funded 2}. Score = paid $/yr net of a $150 eval fee and evals/yr; a policy counts only if it beats the baseline on BOTH plans
on BOTH gap_or_retest and so_cascade (union_2r is the one-account check). Expected: `fear`/`weak` cut evals burned more than they
cut passes (the edge is 3–8× on those days) — if they do not, the eval is a coin flip and the policy is dropped.

**Eval policy results (run 2026-10-28 evening, scratchpad `nf/pol`; net $/yr after a $150 eval fee, funded 2 MNQ):**
- **Day filters on the eval FAIL everywhere.** Trading the eval only on fear / weak / fear-or-weak / straight days lowers net on
  every family × plan (gap_or_retest Topstep: base $1,684 → fear $1,411, weak $361, straight $1,395; so_cascade Topstep $1,747 →
  $748 / $1,076 / $1,221; union_2r Lucid $1,442 → $329 / $1,074 / $1,187). Fewer eval days = slower passes and the same breaches;
  the high-edge-day premium does not survive the eval's clock. Dropped.
- **Eval big / funded small confirmed**: eval 6 / funded 2 beats flat 2 by +$400–750/yr on gap_or_retest, so_cascade and union_2r
  on both plans; the exception is weak_day_long (rare, large trades), where flat 2 is better by +$100–600.
- **ATR-sized eval (size = f × target ÷ ATR$)**: f = 1.0 +$3…+$134/yr on 5 of 6 direction cells, −$132 on so_cascade/Lucid; f = 0.5
  mixed. Does not meet the pre-registered "beats base on both plans on both families" rule → not adopted; a wash, not a lever.
- Scale check: at a realistic funded size (2 MNQ) and $150 evals, one family-account nets **$1.4–1.8k/yr** on Topstep / Lucid Flex;
  the earlier "paid $/yr" column (funded 6 MNQ, free re-buys) is gross and overstates what a funded account keeps.
**Net of the evening:** the only account-side levers that hold are the FIRM (Topstep / Lucid Flex ≈ 2–4× Growth 25K) and
eval-big / funded-small. There is no eval-day trick, no sizing formula, and no new family.

## Four accounts, no re-buys — same-day vs staggered vs rotation-on-wins (run 2026-10-28 night; sim in the session scratchpad, mirrors payout_sim's eval/funded rules for Topstep 50K and Lucid Flex 50K; the book's 2019-26 walk-forward trades, MAE-checked floors, 2,000 random start days, horizon 126 sessions, $150-eval budget of four, NO re-buys)
Founder: "how long is the blowout … if I buy 4 accounts will at least one get paid … test rotation on wins".
- **Blowout length.** The book itself spends a median 5 sessions under its prior equity high (p75 15, p90 40, max 158; 22 stretches > 21 sessions, 5 > 63 in 7.8 years). An ACCOUNT's time to breach is set by size: eval 6 MNQ breaches in a median **5 sessions** (95 % of single accounts gone inside 126), eval 3 in 10, eval 2 in 12–17, eval 1 in ~25.
- **Same-day starts are one bet taken four times**: P(≥ 1 of 4 paid within 126 sessions) 17–37 % and 3.4–3.8 of 4 blown at eval ≥ 2. **Staggering 10 sessions apart** lifts P(≥ 1 paid) to 40–59 % at the same cost. **Rotation on wins** (one account trades per day, move to the next after a winning day) is WORSE than staggering on every column at eval ≤ 3 (it starves each account of trades: first payout 89–98 sessions vs 45–55) and only matches it at eval 6, where both are a lottery.
- **The eval-size answer flips without re-buys**: with four evals and no replacement, **eval 2 / funded 1, staggered** is the best cell on both firms — P(≥ 1 paid) **39 % by 63 sessions, 58–59 % by 126**, 1.25–1.34 accounts paid, $2.1–2.2k paid, 2.7–2.8 blown, first payout median 53 sessions. Eval 6 (the re-buy optimum) blows 3.8 of 4.
- So the honest answer to "will at least one get paid": **about 6 in 10 by month 6, about 4 in 10 by month 3, under 1 in 5 by month 1** — with the stagger; same-day purchase roughly halves that.

## Arsenal additions via engine modes (2026-10-28 night) — pre-registered before the replay
Two edge_lab PASS ideas were blocked only by the paper engine's vocabulary; the engine now expresses them (CHANGES.md 2026-10-28):
`gap_or_retest_eff` (gr_min_eff 0.55) and `m1_10am_all` (tod_momentum with tod_min_er 0.3, VWAP + open agreement, stop 0.25 ATR, 2R).
Built and unit-tested in build-exp only. BEFORE they join the fleet: a replay of 2026-09-22 → 2026-10-27 under label `exp_newmodes`
(after the close) is compared with edge_lab's walk-forward trades on the overlapping dates (cache to 2026-10-15): PASS = ≥ 80 % of
edge_lab's trade days are matched by a same-direction engine entry within 5 minutes and the engine takes no trade on ≥ 80 % of the
days edge_lab takes none (the 2026-10-17 parity standard); then the binary swap + ids at a 23:10 restart, candidate tier, ≥ 40 forward
trades before any verdict. A parity FAIL = fix the mode, not the standard.

## Founder's EMA 9 × session VWAP, 5-minute, 11:00–15:00 Cyprus (pre-registered 2026-10-28 20:30 EET, before the run)
Pine "Session VWAP + Volume Profile + EMA9 Signals": PUTS when the session VWAP crosses above EMA 9 (= EMA drops through VWAP),
CALLS on the reverse; each cross reverses. Window 11:00–15:00 Cyprus = 05:00–09:00 ET (the 7-hour offset holds except ~3 weeks a
year); edge_lab `vwap` is anchored at the 18:00 reopen before 09:30, matching Pine's "Session" on CME. Specs (NQ, confirm ES,
2010-07 → 2024-12 in-sample): `nf_ema9vwap_ldn` — scan tf 5, 05:00–09:00, long when `ema(5,9) > vwap` and it was ≤ one bar
earlier, short mirrored; stop {0.25, 0.35} ATR, target {0 = hold, 2}, flat 09:00 (the window end); `nf_ema9vwap_ldn_hold` — the
same entries held to 15:55 (the cross as a day-direction call). Up to 3 trades a day (reversals). PASS = five gates + holdout.
The volume-profile half of the script draws levels only (POC / VAH / VAL) — levels have failed every test here (lv_*, npoc, vabox)
and are not re-tested. Prior related results: `ema_cross_vwap` (EMA 9/21 cross with VWAP agreement, 10:00–15:00 NY) PASSED +6.8;
`vwap_side_ema`, `ts_vwap30_confirm`, `london_fade`, `london_open_drive` FAILED.
**Result (20:40 EET, scratchpad `nf/ema9`):** `nf_ema9vwap_ldn` FAIL at SCREEN — walk-forward 651 trades, **−0.22 pts/trade, PF 0.98**
(win 46 %, 39 trades/yr; eras −1.5 / +1.4 / −3.4; controls 57 / 28 / 24 %). `nf_ema9vwap_ldn_hold` FAIL at SCREEN — 791 trades,
**−3.05 pts/trade, PF 0.89** (win 32 %; every era negative; 2024 −893 pts). A twin in the NY window (`nf_ema9vwap_ny`, added after
these two were read — counted as a third look) is recorded below. Read: in 11:00–15:00 Cyprus the EMA 9 / session-VWAP cross is a
coin flip before costs and a loser after them; held to the close it is a reliable loser (the cross is late to every move and
early to every chop). The script's reversal logic cannot rescue a per-trade expectancy of −0.2 to −3 pts. CLOSED.
`nf_ema9vwap_ny` (09:35–15:00 ET, flat 15:55): FAIL at WALK-FORWARD — 2,440 trades, −0.69 pts/trade, PF 0.97, win 35 %, 148
trades/yr; eras −1.0 / +0.5 / −2.1; controls 23 / 2 / 22 %. Same shape in NY: gross ≈ 0, net < 0. The cross itself carries no
information at any window tested; `ema_cross_vwap` passed only because it waits for a 9/21 cross AND requires price on the VWAP's
side AND the open's side — the agreement filters, not the cross, are what earned +6.8.

## Quick tests 1–4 (pre-registered 2026-10-28 21:00 EET, before any run)
1. `edge_lab exits` + `meta` on `monday_long` (weak_day_long's were already FAIL: no exit rule passes / meta FAIL at random skip).
   PASS = edge_lab's own exit/meta rules (random-timing twin beaten, no skip-set artefact).
2. ES versions of `m1_10am_all`, `bdh_opt`, `ema_cross_vwap`, `nf_xm_rs_ctl` (symbol ES, confirm NQ, ES cost 0.35): five gates; a PASS
   goes to the ES fleet as a candidate.
3. NQ + ES mirrors on ONE prop account (the 4-account sim, Topstep / Lucid, eval 2 / funded 1, staggered): does the ES book on the same
   account raise P(≥ 1 paid) and E[$ paid] without raising E[blown] by more? Decision rule: adopt the combined account only if P(≥ 1
   paid) @126 rises by ≥ 5 points AND E[blown] rises by < 0.3.
4. Synthetic variance premium: short 1-month ES straddle proxy from VIX (premium ≈ 0.8 × σ_IV × √(21/252) × S) vs realised |21-session
   move|, 2010 → 2026, overlapping and monthly; by VIX regime, by VIX/VIX3M, after fear days; monthly correlation and worst-month
   co-movement with the four-family book. Descriptive — decides whether ES options data is worth buying, never a trade rule.
**Results 1–4 (21:30 EET, scratchpad `nf/q14`):**
1. `monday_long__exits`: FAIL — no exit rule passes (base +9.37; vwap +5.88, stall +9.90 but fails the timing control; trail_reg
   +13.45 in-sample is the page-regime rule and is not a pass). `monday_long__meta`: FAIL at IMPROVES (meta +18.01 vs base +18.16 on the
   2019+ trades). monday_long stays as it is.
2. ES versions: `es_bdh_opt` PASS in-sample (+0.97 ES pts, PF 1.21, 7/7 robustness, 71 trades/yr) but **holdout FAIL (2025: 103 trades
   −323 pts; 2026: +29)**; `es_m1_10am_all` FAIL at CONTROLS (97 % direction; +1.69, PF 1.33 — a near miss, era 2023-26 negative);
   `es_ema_cross_vwap` FAIL at CONTROLS (+0.40); `es_mom1000_k15` FAIL at CONTROLS (+0.46). Nothing joins the ES fleet.
3. NQ + ES on one account (Topstep / Lucid, 4 staggered, eval 2 / funded 1): P(≥ 1 paid) @63 **38 → 47 %** / 39 → 43 %, @126 59 → 61 % /
   59 → 59 %, E$ paid 2,020 → 3,268 / 2,150 → 2,491, E[blown] 2.74 → 3.03 / 2.75 → 3.24. Under the written rule (≥ +5 points @126 and
   blown < +0.3) it is NOT adopted; what it buys is speed (first payouts ~9 points more likely inside 3 months) and ~60 % more $ on
   Topstep at the cost of 0.3 more breaches. Founder's call; the ES mirrors run on paper either way.
4. Synthetic variance premium (short 1-month ES straddle ≈ 0.8σ√T, vs |21-session move|, 2010-07 → 2026-10): **mean +0.89 % of notional
   per month, 71 % of months positive, keeps ~21 % of premium**; 2019-26 +0.73 %, 2023-26 +0.55 %; regime: VIX < 15 +0.53 %, 15-20
   +0.87 %, 20-30 +1.07 %, > 30 +2.52 %, backwardation +1.28 %; non-overlapping months +0.76 %, 69 % positive; worst months 2020-03
   −11.3 %, 2026-03 −7.7 %, 2015-08 −6.0 %, 2018-10 −5.6 % (= −$42k … −$107k per 1 ES lot on a naked straddle). ≈ **$2–3k/month per
   lot with a once-in-years −$40–100k month** — i.e. the premium is real and persistent, and defined-risk structures (not naked) are
   the only sane form. Monthly correlation with the four-family book 2019-26: **+0.15**; in theta's six worst months the book made
   +729 / −259 / +174 / +549 / +751 / +287 pts — the book is NOT short-vol in the same months (it was long weakness in 2020-03). Verdict:
   worth the ES options data and a pricer; never a naked position.
