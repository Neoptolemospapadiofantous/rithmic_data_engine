# Intraday search, 2026-10-11 (night) — what we have, measured, and what it means

**Constraint (founder):** prop-firm rules — flat by 16:45 ET every day, no overnight holds. Everything below is 09:30→15:55 or shorter.
**Verdict:** intraday NQ, ES and crypto price action — and NQ minute-level order flow — hold no edge large enough to pass a prop
eval and pay weekly. The one robust edge we own (buy weakness, VIX-gated) survives intraday in reduced form; it is a sleeve.

## What survives (per 1 MNQ, after $4.82 round trip)

| trade | avg | trades/yr | PF | years + | ledger |
|---|---|---|---|---|---|
| Buy the 09:30 open after a weak close when VIX ≥ 20, sell 15:55 | $53 | 33 | 1.60 | 14/16 | S1 |
| Short the day session on opex Friday / VIX 15–18 / post-earnings night (not weak) | ~$22 | ~50 | ~1.3 | 15/17 (2024–25 heavy) | S4 |
| Short 15:00→15:55 on days down ≥ 0.75 ATR | $4–8 | 26 | 1.25 | 7/17 | S5 |

Rules that hold: enter at market at the open, hold to the close — every dip entry, stop, target, earlier exit or intraday
mechanic (5,853 fleet strategies scored on the regime days, S3) subtracts. The unconditional day session is negative.

## Why none of it pays a prop account (S6–S8)

Edges are 0.05–0.15 ATR per trade against 0.3–0.5 ATR of adverse excursion. On Select Daily 50K ($2,000 trailing
drawdown, $1,000 daily loss) that caps size where a $3,000 eval takes about a year; sizing up blows the account 1–9×/yr.
The payout simulator (eval → funded → reset, 2019–26) puts every intraday sleeve, size and combination at **1–6% of
weeks with a payout**. Three accounts on different sleeves average ~1.2 payouts/week but in the same ~35% of weeks.
The weekly-consistency search over all 9,295 strategies (S6) finds nothing: every train leader is negative out of sample.

## Closed doors (do not re-run)

- Crypto sessions/hours (S9): raw drifts ≤ 11 bps on an 8-hour window; net ≈ 0 after 10 bps; 2020–23 effects reverse in 2024–26.
- Order-flow ML (S10): 45 MBO/book features, walk-forward by quarter — OOS IC ≈ 0, loses 8–9 of 10 quarters at 15/30/60 min.
- Earlier today: 69-feature bar ML (I1), day-type prediction (I2), pullback structure (I3), passive entries (I4), 33-instrument daily sweep (M1).

## Two instrument errors worth remembering

1. A short's P&L must be *charged* the round trip, not credited: the first read of the day-session shorts and the last-hour short
   both shrank by $9.64/trade when corrected (S4, S5).
2. Measuring "down day → short the close" at the 15:00 open vs the open_scan bucket gave the same sample and opposite-looking
   numbers because the cost sat on the long side. Always print the raw drift next to the net.

## The fleet as a matrix (S15)

All 8,207 NQ variants scored factorially (overlay paired to its own base, OOS): level overlays improve 77–85% of bases but
only from PF 0.40 to ~0.52 (lose less, not win); rvol 64%, trail-25 62%; regime presets ≈ chance; every book overlay hurts.
VIX ≥ 20 is the one regime that beats a random split (2.6×); prior-day trend-down 4×. The composite of the robust pieces
(15m momentum × level overlay × VIX ≥ 20) reaches pooled OOS PF 0.98 — breakeven after costs. The 12k cross-filter combo
grid is still unreplayed; nothing in the factor analysis suggests it will differ.

## Exits, re-simulated on every entry (S16)

1.15M real RTH entries of 419 base strategies, minute paths rebuilt, a 54-combo stop × target × trail × time-stop grid
applied to each, chosen per base on train and scored on test. The engine's own exits were losing value — median test PF
0.53 → 0.92 with the right exit, which is "hold to the close" or "a wide stop only" for 353 of 419 bases. But the test
ORACLE (hindsight exit) still tops out at 23% of bases PF > 1.2, median 1.09; train-chosen: 0 of 419 hold PF > 1.3 in both
halves. Exit optimisation stops the bleeding; the entries carry no edge for it to unlock. Walk-forward variant picking
(prior 3/6/12 months) is −$48k over 82 groups. **Fleet rule from this: no fixed target, no tight stop, no BE trigger on
NQ intraday entries — hold to the close or carry a wide stop.**

## Venue check for the overnight form (P2, P3)

Firms that allow the hold: FTMO Swing (US100 CFD) and Phidias Premium (CME futures). With the position's MAE accounted
correctly (P3 — the earlier Tradeify numbers were an export artefact), FTMO 100k at minimum size pays in 27% of fortnights,
~$700/month, 0.6 blow-ups/yr (2019–26); Phidias 8% / $240 / 2.4; any larger size dies on the 2022 drawdown. No venue makes
it a weekly payer.

## What is still open

- The intraday weakness long is live-runnable today (`anyweak_day__vix20`, `rsi2_20_day__vix20`, `wc_ibs30_day__vix20`
  in `config/paper_fleet.json`); the shorts need a calendar/VIX gate. Worth ~a payout per quarter, not per week.
- The full edge (18:00→15:55, `docs/strategy_weakness_vix_book.md`) needs a venue that allows the hold.
