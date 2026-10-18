# Pre-registration — 1-minute momentum, scanned all session for 1–3 trades a day (written 2026-10-18, BEFORE any run)

Founder: "I want 1-3 trades a day, at least 1 a day, momentum."
Momentum at a 5-minute close: er(1,N) ≥ e, direction = sign of the N-minute move, price on that side of VWAP and the 09:30 open.
Scan every 5-minute close 09:45–13:55, NQ, stop 0.25/0.35 ATR, 2R, flat 15:55, one position at a time.
Grid N ∈ {30, 45}, e ∈ {0.2, 0.3}, stopk ∈ {0.25, 0.35}. 2010-07 → 2024-12 (walk-forward 12 months), ES confirm, 1.0 pt.
Tests (family of 3, controls 99 %): `mom_scan_1` (max 1 trade/day), `mom_scan_3` (max 3), `mom_scan_3_pb` (max 3, entry only
on a 5-minute bar that touches VWAP and closes back — the tested pullback). Report trades/day and share of days with a trade.
Pass = every gate. Daily frequency alone is not a pass.

## Results (appended 2026-10-18 — tests unchanged)
| test | walk-forward net/trade (trades/yr) | verdict |
|---|---|---|
| mom_scan_1 | +2.82 (156/yr) | FAIL controls (direction 95 %) |
| mom_scan_3 | +2.32 (232/yr, 7/14 years +) | FAIL controls (time/day pass, direction 99 % borderline) |
| **mom_scan_3_pb** (VWAP pullback entry) | **+3.09 (128/yr, win 44 %, PF 1.15, 8/14 years +)** | **PASS** (controls 99/100/99 %, robustness 7/7) |
2025+ at the frozen setting (N 45, er ≥ 0.2, stop 0.35 ATR, 2R): +23.33 pts/trade on 201 trades, t 2.5 (holdout no longer clean).
Scanning all session gets ~0.5 trades a day, not 1+: momentum is absent on most days. Per trade it is far below the single
10:00 check (+13.7, 22/yr) — frequency is bought with edge per trade.

## Follow-up, pre-registered 2026-10-18 BEFORE running (founder: "ema not vwap")
`mom_scan_3_ema` — identical to mom_scan_3_pb (scan 09:45–13:55, ≤ 3/day, momentum N ∈ {30, 45}, er ≥ {0.2, 0.3}, stop 0.35 ATR,
2R) but the entry bar touches the 5-minute EMA(E) (within 0.05 ATR) and closes back on the momentum's side, E ∈ {9, 20};
stop fixed at 0.35 ATR (the VWAP version's choice) so the grid stays at 8. Same gates (controls 99 %). Compared with
mom_scan_3_pb per trade.

## Result of the EMA follow-up
mom_scan_3_ema: +2.09 pts/trade (155/yr, 6/14 years +), FAIL controls (direction 97 %, day 86 %). The VWAP pullback entry is better.

## Follow-up 2, pre-registered 2026-10-18 BEFORE running (founder: "long above vwap and short below vwap")
`vwap_side_ema` — direction = side of VWAP only (no momentum condition): above → longs only, below → shorts only. Entry: a
completed 5-minute bar touches the 5-minute EMA(E) (within 0.05 ATR) and closes back on the VWAP side, and the close is still on
that side of VWAP. Scan 09:45–13:55, ≤ 3/day, 2R, flat 15:55. Grid E ∈ {9, 20}, stopk ∈ {0.25, 0.35}, o ∈ {0, 1} (o = 1 also
requires price on the same side of the 09:30 open). Same gates (controls 99 %).
Result: vwap_side_ema +2.24 pts/trade walk-forward (154/yr), whole-period best variant only +1.70 (t 2.1 < 2.73 needed for 8
variants); FAIL controls (time 83 %, day 86 %); 2020 −1,138, profit concentrated 2022–24. VWAP side as the only direction is
not enough; the 1-minute momentum condition is what carries mom_scan_3_pb.

## Follow-up 3, pre-registered 2026-10-18 BEFORE running (founder: "momentum ema pullback not vwap. 14 ema")
`mom_scan_3_ema14` — mom_scan_3_ema with E fixed at 14 (founder's EMA). Grid N ∈ {30, 45}, e ∈ {0.2, 0.3}, stopk ∈ {0.25, 0.35}
(8 variants). Same gates. Note: chosen after EMA 9/20 failed — a third look at the same idea, so a pass needs to be read with that.
Result: mom_scan_3_ema14 +1.61 pts/trade (137/yr), FAIL controls (direction 96 %, day 82 %) and robustness (2023–24 negative). EMA 9 / 14 / 20 all fail as the momentum entry.
