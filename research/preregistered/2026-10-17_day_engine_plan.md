# The regime day engine — what the data says, and what to test next (2026-10-17)

Founder's design: open with an ORB + order flow (1–3 trades, then stop); then read the regime — trend, mean reversion, chop — with
filters; trend → enter on the 14 EMA or a close, and on a pullback build a liquidity box and trade its break; mean reversion → only if it
improves the P&L, else skip; chop → stop. Every step mapped to what NQ 1-minute 2010-26 (+ order flow 2023-26) already showed.

## 1. Each step, with its evidence (walk-forward pts/trade after cost; edge_lab verdict)

| Step | Closest tested version | Result |
|---|---|---|
| ORB at the open, any side | orb / orb5 / orb15 | +1.5 / +1.5 / +2.5 — FAIL at controls |
| ORB at the open with order flow (2023-26) | orb5_of / orb15_of / orb5_rangeflow / orb15_rangeflow | +5.5 / +4.1 / +7.3 / +6.4 — all FAIL at controls (random time does as well) |
| The open's direction, traded simply | cascade (so_cascade) / dir_market | +4.8 / +5.6 — PASS |
| The open's direction + order flow agreeing (2023-26) | casc_flow | +18.5 vs +15.0 without — PASS (one era) |
| "Read the regime" in advance | regime study (rest-of-day continue / reverse / chop vs every reading) | 26 / 22 / 52 % whatever the reading — direction not predictable |
| The 1-minute tape regime | switch / filter / trail / fleet / 1-min price action | real (persists 57–64 %) but never tradable |
| A model choosing which cascade signals to take (context features) | so_cascade__meta | +8.3 vs +4.8 — PASS (with book features: FAIL) |
| Trend day → EMA pullback entry | dir_ema_pb (EMA20 after direction) / rg_ema (EMA9/21, trending tape) | +4.4 FAIL / −0.1 FAIL — a market entry (+5.6) beats it |
| Break → box → go | bb_or / bb_pd / bb_on (any side) | +0.7 / +0.3 / +2.9 — FAIL |
| Break → box → go on the gap side | bb_on_gap / gap_or_retest | +7.3 / +9.0 — PASS |
| Mean reversion when there is no direction | nd_range / nd_vwap / nd_open | −2.0 / −4.3 / −6.5 — FAIL (every era negative) |
| Chop → stop | "no signal = no trade" | right: fades and forced trades (12:00 check) lose |
| Stop for the day at a profit | daily lock $100–500 | cuts the big winners — worse |

Summary: the parts that hold up are the direction (the open, the gap), order flow agreeing with it, a model that skips weak cascade
signals, and break → box/retest → go only on the gap's side. Not supported: the open ORB itself, reading the regime in advance, EMA
entries, mean reversion.

## 2. Tests not yet run (pre-registered here, before any run)

- T1 cascade + order flow + meta model: so_cascade__meta's take/skip model on casc_flow's trades (2023-26). Does flow + model beat each alone?
- T2 EMA 14 after the direction is set: dir_ema14 = dir_ema_pb with EMA(14) on 5-min bars (the founder's setting). Must beat dir_market.
- T3 Second-leg continuation on a trend day: after the cascade direction is set, price pulls back ≥ 0.15 ATR from the day's extreme, a
  5-min box forms (range ≤ 0.75 × the previous bar's), and a bar closes beyond the box in the trend direction → enter; stop the other side
  of the box, 2R. Must PASS edge_lab and beat dir_market per trade.
- T4 Day trade cap: the plan (cascade + gap retest + box_go) with at most 1 / 2 / 3 trades a day and a stop after the first / second loss,
  simulated on the 25K plans. Founder: "1–3 trades then stop".
- T5 The full day engine, assembled from the parts that PASS (open direction ± flow, gap retest, box_go, meta skip), simulated on the
  25K plans against today's plan: trades/day, winning days, eval pass/blown, first payout, paid/yr.
Each runs on 2010-07 → 2024-12 (2023-03 → for order flow); the 2025+ holdout is judged once for any PASS.

## Results — pointer (catalogue clean-up 2026-10-19 — appended note; the rules above are unchanged)
T2 (dir_ema14): **FAIL at CONTROLS** (data/edge_lab/reports/dir_ema14.md). T1, T3, T4, T5 were never run — no report exists; the questions were
taken up by later files (meta/flow: 2026-10-18_feature_study, 2026-10-18_rithmic_direction; trade cap / engine: 2026-10-18_multi_trade_day book F).
