# BOOK2 account: regime filters on top of the daily loss stop, and size × stop (written 2026-10-22, BEFORE the run)

Founder: "yes" (to testing the regime filters and the size × stop grid on the ~2-a-day flag book's account EV).
BOOK2 = NQ + ES flags, 4-5m charts, pole >= 3× the average bar, pullback 5-38 %, A or B grade, both sides, up to 2 open per market
(flag_sortino.cpp's pick). Account EV = payouts per eval bought (payout_sim.hpp run_eval <= 60 days → run_account 120 days), starts every 5th
trading day 2014-01 → 2025-04, $ at today's ATR, worst open loss from the 1m bars (flag_prop_ev.cpp). Plans: select_flex_50k, select_daily_50k
(1 micro each market), apex_eod_50k (2 micros each); base daily loss stop $150 (trade closed at the stop level, no more trades that day).

## A. Regime filters (judged)
DT200 = take a flag only when yesterday's close is on the trade's side of the 200-day average; NOFOMC = no trades on FOMC days; BOTH.
A filter is adopted only if, on ALL THREE plans: EV per eval >= 10 % higher than the base, AND higher in both start halves (2014-2019,
2020-2025), AND still higher with +1 NQ point (ES a third) extra cost per trade.

## B. Size × daily stop (reported, the choice made afterwards is flagged as in-sample)
1 / 2 / 3 micros on each market × daily loss stop $100 / $150 / $200 / $300 / $400, per plan, plus start halves.

## Results (2026-10-22; flag_prop_ev.cpp `… regime`, out/flag_prop_ev_regime.txt)
**A. No regime filter is adopted.** DT200 cuts the EV on every plan (flex 50K $1,973 → $562, daily 50K $2,058 → $552, apex $2,681 → $1,911;
2020-25 starts hit hardest) — with fewer flags the account takes longer to reach the target and the daily stop already removes the bad days.
NOFOMC is neutral (flex $1,891, daily $2,128, apex $2,638). DT200 + NOFOMC like DT200.
**B. Size × stop (in-sample choice):** 2 micros with a $150-200 stop is the best area on every plan and holds in both halves — flex 50K
$3,428-3,821 (2014-19 $3,337-3,663 / 2020-25 $3,527-3,993), daily 50K $4,577-4,738 (and 3 micros / $150 $5,363), apex 50K 2 micros $2,640-3,161.
At 1 micro the tightest stop is best; at 2-3 micros a $150-300 stop. A $400 stop is worst at every size.

## Part 2 — position sizing (written 2026-10-22 after Part 1's results, BEFORE its own run)
Founder: "yes" (to testing smarter sizing). Same BOOK2, plans (select_flex_50k, select_daily_50k, apex_eod_50k), starts and simulation; daily loss
stop $150 and $200 (both reported). Baseline FIXED2 = 2 micros on each market (Part 1's best region). Methods, fixed before the run:
- RISK$: contracts per trade = round(R ÷ the trade's stop risk at 1 micro), clamped to 1-4, for R = $40 / $60 / $80 (same $ risk per trade).
- CUSHION: 1 micro per market until the account is >= +$X above its start (eval) or above its post-payout balance's start (funded), then 2;
  X = $500 / $1,000.
- GRADE: A-list flags 2 micros, B flags 1.
A method is adopted only if, at BOTH stops, on ALL THREE plans: EV >= 10 % above FIXED2, higher in both start halves (2014-19, 2020-25), and
higher with +1 NQ point extra cost.

### Part 2 results (2026-10-22; flag_prop_ev.cpp `… sizing`, out/flag_prop_ev_sizing.txt)
The copied simulation reproduces payout_sim exactly (1 micro, $150, flex 50K: $1,973). **No sizing method beats FIXED2** at either stop on any
plan: flex 50K $150 FIXED2 $3,428 vs RISK$ $40 / 60 / 80 $1,973 / 2,210 / 2,770, CUSHION $2,125 / 1,965, GRADE $2,977; daily 50K $4,577 vs best
other $4,047 (GRADE); apex 50K $2,681 vs $2,475 (GRADE). FIXED2 is also best with +1 pt cost and in both halves.
Flaw in the plan, stated: BOOK2's average stop risk at 1 micro is $68, so RISK$ $40-80 mostly rounds to 1-1 contract — smaller on average than
FIXED2 — and the test mostly says "2 micros beats 1". A size-matched RISK$ (about $135 a trade) was not in the plan and is not run here.
CUSHION starts at 1 micro and loses the early-pass advantage; GRADE (A 2 / B 1) is ~10-15 % below FIXED2 everywhere.

## Part 3 — cutting the drawdown (written 2026-10-22 after the drawdown check, BEFORE its own run)
Founder: "well obviusly 5277 will blow the account". BOOK2, 2 micros each market, daily stop $150 (base: worst drawdown $5,277). The book's
SHADOW record = every day's result with the daily stop, traded or not (known at the close, used only from the next day). Rules:
- WEEK300 / WEEK500: no more trades for the rest of the week once the week's live result reaches −$300 / −$500.
- EQ10 / EQ20: trade a day only if yesterday's shadow equity is at or above its 10- / 20-session average.
- STREAK3: after 3 shadow losing days in a row, stand aside until the shadow record has a winning day again.
Judged: worst drawdown (continuous account, end of day) cut by >= 40 % AND $ per eval bought not lower than the base on at least 2 of the 3
plans (select_flex_50k, select_daily_50k at 2 micros; apex_eod_50k at 2 micros) AND the cut holds in both halves (2014-19, 2020-26 worst
drawdowns both lower than the base's).

### Part 3 results (2026-10-22; flag_prop_ev.cpp `… ddfix`, out/flag_prop_ev_ddfix.txt)
**Nothing passes.** Base worst drawdown $5,277. WEEK300 $4,460 (−15 %, EV flex $3,428 → $2,232), WEEK500 $4,827 (EV about the same), EQ10
$4,398 (EV $922-1,347 — it misses the rebounds), EQ20 $7,332 (worse), STREAK3 $5,798 (worse). The drawdowns are long grinds of small capped
losing days (2024-26), not a few bad days, so day / week / streak switches cannot remove them without removing the recoveries too.
Reported, not judged: bigger accounts give more room — select_flex_100k 3 micros (stop $225) $4,861 per eval, 150K $5,497; select_daily_100k
3 micros $5,476, 150K $6,931 (vs 50K 2 micros $3,428 / $4,577) — compare against their higher fees.
