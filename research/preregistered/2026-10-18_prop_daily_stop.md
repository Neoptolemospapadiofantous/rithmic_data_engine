# Pre-registration — trade every day, take the day's P&L and stop; blown accounts allowed (written 2026-10-18, BEFORE any run)

Founder: "its okay if we blow up some accounts just need to keep trading at least 1-3 trades a day any session. just need to get
some pnl for the day and stop". So the measure is money per account bought, not the trade win rate: EV per account =
(payouts − blown accounts × reset fee) / accounts, over a 60-session account life, Tradeify plans (src/paper/payout_sim.hpp).

Series (trades from `edge_lab run … --trades`, 2019-01 → 2026-10; $ per MNQ = gross pts × 2 − 1.8 pt slippage × 2 − $1.82
commission, i.e. the measured live fill cost):
- `book_2r` — the every-day book, 2R / 0.35 ATR, 1 trade a day (2026-10-18_high_winrate_book "current").
- `book_hiwin` — the same entries, 0.5R / 0.35 ATR, up to 3 a day.
- `asia_cascade` — cascade_asia_2h (PASS 2026-10-17: the 18:00 Globex open's cascade, checks 18:20–19:50), its screen-best
  variant over 2019–26 (an in-sample pick — stated), the evening session.
`payout_sim` (unchanged code): every daily rule (profit stop × loss stop × max trades, its rule_grid) chosen on the first 60 % of
the calendar, scored on the last 40 % (≈ 2023-06 → 2026-10); single series at its sizes and greedy portfolios of up to 3 series;
`--max-blown 1.0` (blow-ups allowed, each charged the `--reset-cost 150`). Reported: the best EV per account on the TEST part per
plan, its daily rule, payouts per account, blown share.
No adoption here — this answers "which daily routine pays per account"; a live book still needs the paper forward record.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/payout_sim; 3 series, 1,786 days; train 2019-01-02 … 2023-05-10 = 506 account starts, test
2023-05-11 … 2026-10-16 = 318 starts; report + csv kept in data/payout/daily_stop_20261018/). Series sizes: book_2r 1,503
trades, book_hiwin 2,110, asia_cascade 515.

Best TEST EV per account per plan (all: series `book_2r`, chosen daily rule in brackets):
| plan | size | rule (+profit / −loss / max trades) | test EV / account | paid ≤ 20 d | eval pass | blown in 60 d |
|---|---|---|---|---|---|---|
| select_daily_50k | 3 MNQ | none | **$1,801** | 35 % | 36 % | 88 % |
| select_flex_50k | 3 MNQ | none | $1,806 | 56 % | 36 % | 87 % |
| topstep_50k | 5 MNQ | none | $2,237 | 36 % | 30 % | 89 % |
| mff_rapid_eod_50k | 2 NQ | none | $2,085 | 29 % | 8 % | 97 % |
| lucid_flex_50k | 3 MNQ | none | $1,707 | 53 % | 37 % | 85 % |
| apex_eod_50k | 3 MNQ | none | $1,323 | 31 % | 37 % | 81 % |
| select_daily_25k / flex_25k | 3 / 2 MNQ | none | $772 / $734 | 32 % / 37 % | 22 % / 29 % | 95 % / 89 % |
| growth_50k / 25k | 2 / 1 MNQ | none / −300 | $856 / $276 | 15 % / 12 % | 31 % / 29 % | 67 % / 62 % |
`book_hiwin` (0.5R, ≤ 3/day) best $1,032 (select_flex_50k, 1 NQ, max 1 trade) — below `book_2r` on every plan; its daily profit
stop (+150) appears only on the smaller plans and never beats the plain 2R book. `asia_cascade` $0–1,199 per account (best mff_rapid_eod_50k $1,199, select_daily_50k $741) — below `book_2r` on every plan. No
portfolio was formed (a second member never improved the train EV).
**Answer:** "get some P&L for the day and stop" does not add money per account — the chosen daily rule is "none" on every top
row. The routine that pays per account is the plain 2R book (one trade a day from the confirmed NY setups), 3 MNQ on a 50K plan
(or 5 MNQ Topstep); about a third of evaluations pass and most accounts blow within 60 sessions — after their payouts. The
test period overlaps the 2019–24 confirmation and the looked-at 2025–26 data, so the dollar figures are optimistic.

## Addendum (2026-10-18, description) — VestMarkets Silver 5K and several accounts at once
Founder's account: VestMarkets Silver — $5,000, max drawdown $300 (static), max daily loss 3 % ($150), evaluation goal +$500, 80 %
split, $38 fee, NQ perps (fractional size), withdrawals any day. Simulated on the 2019–26 trade lists, a start every session,
risk-based size ($R per 0.35 ATR stop), 2 pt cost, breach checked at each trade's MAE.
- Evaluation (book without the weakness long — its stop-less days break the $150 daily limit): $25 risk 78 % pass / 6 % fail
  (~190 trades) · $50 53 / 45 % · $75 49 / 50 % · $100 47 / 52 % (~15 trades). Full book with the weakness long: 33–39 % at $50–100.
- Funded, withdrawing daily above a cushion: $100 risk + $150–300 cushion ≈ $300–310 expected per $38 evaluation, first
  withdrawal a median 3–6 sessions after passing; $25 risk + $150 cushion ≈ $990 per evaluation but over ~2 years.
- Coin-flip direction on the same trades: ~13 % pass (vs 47 %) — the edge triples the pass rate; one attempt is still near a toss.
- Two accounts at once (pass / both-fail / at-least-one-pass): same strategy same day 48 / 52 / 48 % (identical — no benefit); 5
  sessions apart 58 % at least one; 20 apart 68 %; $100 vs $50 risk 62 %; **cascade-only vs gap-retest-only accounts 51 % and
  60 % pass, both fail 25 % (independent would be 20 %), at least one 75 %** — splitting setups across accounts makes them nearly
  independent, and each single-setup account passes more often than the combined book.
- One VALIDATED strategy per account (VestMarkets 5K, $100 risk; pass % · median time to pass): cascade-only 51 % · ~1 month (154
  trades/yr) · gap-retest-only 60 % · ~1.3 months (102/yr) · 10:00 gap rule 81 % · ~5 months (34/yr) · Asia cascade 75 % · ~23
  months (66/yr — too slow at this size). Pairs are close to independent (both fail vs independent: cascade + Asia 13.1 vs 12.2 %,
  cascade + 10:00 11.7 vs 9.1 %, retest + Asia 11.4 vs 9.5 %, gap10 + Asia 10.5 vs 4.5 %). The fleet's other strategies are not
  candidates unless they pass edge_lab — a zero-edge strategy passes this evaluation ~13 % of the time (the coin-flip control).

## Correction (catalogue clean-up 2026-10-19 — appended note; the rules above are unchanged)
The "One VALIDATED strategy per account" line lists the Asia cascade as validated. It is not: `2026-10-16_cascade_all_sessions.md` — cascade_asia_2h
passes the gates but its holdout is negative (−1.47 / −0.64), and every later overnight-direction study failed (2026-10-19_behavior_per_session).
