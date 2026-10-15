> **CORRECTION 2026-10-11 (night) — the payout-simulator numbers in this document are INVALID for the stack.** The stack was
> exported as one row per unit, so `payout_sim` tested each unit's adverse excursion separately instead of the position's.
> With the position's MAE (median $220, p90 $1,542, p99 $3,882) the 4-unit stack breaches a $2,000 trailing drawdown ~12×/yr
> on Tradeify Select Daily 50K; 1 MNQ flat ~2.8×/yr. The edge itself (buy weakness, VIX-gated, 14/16 years) stands; the
> "payout every 2 weeks on a prop account" conclusion does not. Ledger rows P3 (correction) and P2 (FTMO / Phidias venues:
> best fit is FTMO 100k Swing at minimum size, ~$700/month, a payout every ~7 weeks). Under Tradeify's flat-by-close rule the
> trade is research-only anyway; the legal intraday form is in `docs/intraday_search_2026-10-11.md`.

# The weakness × VIX book — best strategy so far (2026-10-11)

Founder goal: pass a Tradeify evaluation and get paid consistently (target: a payout every two weeks).
This is the strongest construction the data supports after the 2026-10-11 search (16.5k fleet variants, a 69-feature
walk-forward model, ~30 direct tests, a 7-family daily sweep, VIX/ETF/FOMC context). Ledger: `research_tests` (D*, V*, I*, P*),
CHANGES.md 2026-10-11 entries, `~/starnet-work` is NOT told about it (desk paused).

## The trade
**Signal, decided at the 16:00 ET RTH close — "any weakness":** close in the bottom 30 % of the day's range (IBS < 0.3),
OR 2-day Wilder RSI of RTH closes < 20, OR two consecutive lower closes, OR a fear day (close ≤ open − 1 × prior ATR14).

**Gate — "the market is afraid" (prior VIX close, CBOE):** VIX ≥ 18, OR VIX up > 10 % over 5 sessions, OR VIX in the top
tercile of its trailing 252 closes. Without fear the trade is worth ≈ 0 (VIX < 15: +$0.4/MNQ); with it 3–8× the average.

**Entry:** long at the 18:00 ET Globex reopen, market order. **Exit:** 15:55 ET the next afternoon, market order. One session;
no stop beyond the account's own limits (every tested stop cut the edge; 1.5×ATR keeps most of it if a stop is required).
Strictly intraday fallback: buy the 09:30 open instead (≈ ⅔ of the edge, VIX ≥ 20 days only).

**Size — conviction stack on a Select Daily 50K (4 minis allowed):** 1 MNQ on any weakness, +1 if the close was in the
bottom 30 %, +1 if the VIX gate is on, +1 if both → 1–4 MNQ.

## Evidence (NQ 2010-07 … 2026-10, $ per MNQ after $4.82 round trip; ES confirms every row)
| variant | trades/yr | avg | PF | years + | worst year | ES PF |
|---|---|---|---|---|---|---|
| any weakness, no gate | 60 | +$31 | 1.39 | 14/17 | −$437 | 1.25 |
| any weakness & gate | 42 | +$45 | 1.51 | 15/17 | −$173 | 1.40 |
| any weakness & VIX ≥ 20 | 23 | +$74 | 1.72 | 13/16 | −$224 | 1.49 |
| rsi2 < 20 & VIX ≥ 20 | 14 | +$105 | 2.09 | 14/16 | −$399 | 1.87 |
| fear day & VIX rising | 10 | +$95 | 2.41 | 12/17 | −$844 | 2.22 |
| day-only (09:30 → 15:55), any weakness & VIX ≥ 20 | 25 | +$50 | 1.54 | 13/16 | −$616 | 1.36 |
Monotonic in both the weakness measure and the VIX level; positive in both halves (2010–18 / 2019–26); robust to the
formulation (IBS 0.1/0.2/0.3, holds 1–3, exit on strength). 2022 is the drawdown year (ungated: ~$3–5k per MNQ).

## Payout simulator (official Tradeify rules, members chosen on 2010–19, scored on 2020–26, 250-day accounts)
| book on Select Daily 50K | eval pass / fail | payouts / month | 2-wk windows paid | blown / yr (before 1st payout) | $ / month |
|---|---|---|---|---|---|
| 1 unit | 41 % / 59 % | 1.0 | 25 % | 89 % (57 %) | $194 |
| 2 units (+ gate) | 79 % / 21 % | 1.2 | 29 % | 56 % (13 %) | $634 |
| 3 units (+ weak close) | 90 % / 10 % | 1.6 | 47 % | 32 % (9 %) | $1,272 |
| **4 units** | **92 % / 8 %** (median 58 days) | **1.8** | **53 %** | **28 % (7 %)** | **$1,590** |
| day-only fallback, 2 units | 47 % / 22 % | 1.2 | 40 % | 36 % (16 %) | $430 |
Bigger stacks are safer: the eval is a race between +$3,000 and the −$2,000 EOD trail, and the extra units fire only on
the strongest days. 25K accounts do not work (trail too tight). Flex / Growth (5 winning days per payout) cannot pay
biweekly for any low-frequency book; staggered accounts do not raise cadence (the same days pay).

## Which firm (payout_sim, 4-unit book, 250-day accounts, test 2020–26)
| plan | eval pass / fail | payouts / month | 2-wk windows | blown / yr | note |
|---|---|---|---|---|---|
| **Tradeify Select Daily 50K** | 92 % / 8 % | 1.8 | **53 %** | 28 % | no winning-day count, EOD trail, min $250 |
| MFF Rapid 50K (official checkout page) | 92 % / 8 % | 1.5 | 43 % | 53 % | real-time trail hurts a hold; daily payouts > $2,100 buffer, min $500 |
| Apex 50K (third-party rules) | 95 % / 5 % (31 d) | 0.3 | 19 % | 22 % | easiest eval; 5 qualifying days per payout |
| Tradeify Flex / Growth | 92 % / 78 % | 0.4 / 0.2 | 23 % / 15 % | 16 % / 42 % | 5 winning days per payout |
| Topstep 50K Express (third-party) | 78 % | 0.3 | 6 % | 100 % | $1K DLL + 5 winning days — unlivable for this book |

## Rules that bind (help.tradeify.co, 2026-10-11)
Flat by 16:45 ET daily, "no overnight holds", trading day 18:00 → 17:00 — **confirm with support that an 18:00 → 15:55
position is a single-day position before any funded account; if not, run the day-only book.** Select eval $3,000 target,
$2,000 EOD trailing enforced live, 40 % consistency, ≥ 3 days, no time limit. Select Daily: buffer $2,100, payouts any day
the cycle is positive, 2× cycle profit, $250 min, $1,250 cap, DLL $1,000 (modelled soft).

## In the engine and the fleet
`hold` mode: `hold_prev_ibs_max 0.3`, `hold_rsi2_max 20`, `hold_down_streak 2`, `hold_prev_body_max -1`, `hold_weak_any`,
`hold_vix_min 18 / hold_vix_rise_min 0.10 / hold_vix_pct_min 0.66 / hold_vix_any`, entry 1800, window 1800–1555.
VIX from `scripts/vix_fetch.sh` (cron 16:45 ET) via `src/paper/vix_regime.hpp`; paper runner and executor call `set_vix`.
Fleet ids (NQ, `es_` twins): `anyweak_h1`, `anyweak_h1__vixany`, `anyweak_h1__vix20`, `wc_ibs30_h1__vixany`,
`wc_ibs30_h1__notcalm`, `rsi2_20_h1__vix20`, `rsi2_20_h1__vixany`, `fear18_h1__vixrise`, day-only `anyweak_day__vix20`,
`anyweak_day__vixany`, `rsi2_20_day__vix20`, `wc_ibs30_day__vix20`, plus `fomc_drift_h1`. Forward test began 2026-10-11
(label `tradeify` / `es`); verification replay 09-18 … 10-09 reproduced the research's signal days exactly. The conviction
stack is four of these ids running side by side (`anyweak_h1` + `anyweak_h1__vixany` + `wc_ibs30_h1` + `wc_ibs30_h1__vixany`).

## Go-live checklist
1. Tradeify support: 18:00 → 15:55 = intraday? Select Daily DLL soft? (parked by the founder for now)
2. ≥ 20 forward sessions agreeing with the replay (VIX is ~15 now — the gated units will mostly wait; that is the design).
3. Buy one Select 50K eval; run the 4-unit stack (two-stage sizing is optional: 2× until +$1,500 helped `ema_pb_20_50__lv_conf`
   but hurt others; for this book the plain stack already passes 92 %).
4. Choose Select Daily at funding; request a payout on any positive-cycle day above the buffer.
5. Expect: ~2 months to funded, ~$1,600/month, a payout in about half of all two-week windows, ~1-in-4 chance per year of a reset.

## What not to do
Intraday indicator/overlay variants, ML on minute bars, ORB tuning, passive fills for momentum entries, multi-session holds
on Tradeify, 25K accounts, Flex/Growth for this book. All measured (CHANGES.md 2026-10-11).
