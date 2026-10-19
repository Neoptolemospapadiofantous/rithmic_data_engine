# Momentum family add-ons: order flow, VIX sizing, scaling out, cascade agreement (pre-registered 2026-10-17, before any run)

Founder: "do all of them" (the list after 2026-10-17_momentum_entries.md). Trades: two rules, unchanged —
A = mom_dir_45_vwap (45-min efficiency ≥ 0.3, 5-min VWAP pullback within 0.05 ATR, stop 0.25 ATR, BE +1R, hold) and
B = mom_dir_30_ema20 (the original). NQ New York 2010-07 → 2024-12, cost 1.6 pts, scored in R.
1. ORDER FLOW (2023-03 → 2024-12 only, the months with aggressor data): RTH cumulative delta (2·buy − volume since 09:30) agrees
   with the trade vs disagrees; and the last 15 minutes' delta agrees vs disagrees. Helps only if "agrees" beats "disagrees" in R per
   trade for BOTH rules (one era only → a candidate at most).
2. VIX SIZING (prior VIX close): R per trade by VIX < 16 / 16–22 / ≥ 22; policy 2 units when VIX ≥ 20, else 1, vs flat 1 unit —
   total R, worst peak-to-trough drawdown in R, R per unit of risk. Adopt only if return / drawdown improves in all three eras.
3. SCALING OUT: half the position at +1R, the rest break-even and hold — vs all held (same entries).
4. CASCADE AGREEMENT: the plan's direction (first cascade check / gap retest) already set and agreeing at entry → 2 units, else
   1 (and: agreeing only). Same adoption test as 2.

## Results — pointer (catalogue clean-up 2026-10-19 — appended note; the rules above are unchanged)
Numbers in `research/programs/out/momadd.txt` (the program `momadd`, reproduced 2026-10-19): VWAP rule when the plan direction AGREES +0.249 R/trade
(298), DISAGREES +0.103 R (81), no plan direction yet −0.001 R (321) → adopted on paper as mom_dir_45_vwap_plan. RTH-delta and VIX / scaling add-ons
not adopted (see momadd.txt).
