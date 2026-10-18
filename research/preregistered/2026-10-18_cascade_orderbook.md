# Pre-registration — the order book as a filter on the cascade (written 2026-10-18, BEFORE any run)

Founder: "more tests". The one order-book area never tested on the book's main trade. Order-book minutes (edge_lab `bk(field, n)`,
NQ market-by-order 2023-03 → 2026-03): `qimb` = (bid − ask) / (bid + ask) resting size within 10 bps of price. Earlier book work:
orb5_liqpull (resting size pulled before an ORB break) FAIL at controls; casc_flow (aggressor delta agrees) PASS, on paper.

Base: the book's cascade — checks 09:50 (> 0.25 ATR from the open), 10:30 / 11:00 / 11:30 (> 0.30 ATR), first that fires, the open's
side; stop 0.35 ATR, 2R, flat 15:55, one trade. Filter q (at the check bar; D = the cascade side):
0 none · 1 book agrees: D × bk(qimb, 20) > 0 (more resting size on the side the trade leans on) · 2 book against: D × bk(qimb, 20)
< 0 (size stacked against the move — the paper fleet's "fade the stacked side" gate) · 3 aggressor delta agrees (D × cdelta > 0) ·
4 delta agrees AND book agrees · 5 delta agrees AND book against. 6 variants.
Discovery 2023-03-01 → 2024-08-31 (screen); pick (fixed now): the q ≥ 1 cell with the highest net/trade with ≥ 60 trades that is
≥ 1.25 × the unfiltered cascade (q 0) on the same days AND positive in both halves of the discovery period (to 2023-11-30 / after).
Confirmation 2024-09-01 → 2026-03-19: the fixed cell through the full gates (`control_pct` 0.95, 400 seeds, `min_trades` 50)
AND ≥ 1.25 × q 0 over the same period. No pick = state it.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab; a filtered-out check lets the next check fire, so the cells overlap). Net/trade at 1 pt (n):
| q | discovery 2023-03 → 2024-08 | halves (→ 2023-11 / after) | confirmation-period screen 2024-09 → 2026-03 |
|---|---|---|---|
| 0 none | +7.70 (243) | −0.69 / +16.02 | +18.85 (267) |
| 1 book agrees | +8.79 (138) | −1.65 / +18.92 | +21.10 (156) |
| 2 book against | +4.19 (203) | −1.21 / +10.10 | +4.96 (231) |
| 3 delta agrees | +6.82 (225) | −1.52 / +14.80 | +21.52 (246) |
| 4 delta + book agree | **+10.96 (120)** | +0.22 / +20.36 | +21.17 (140) |
| 5 delta agrees + book against | +2.34 (184) | −2.67 / +7.58 | +9.27 (213) |
Pick: q 4 (1.42 × q 0, both halves > 0). **Confirmation `casc_book_confirm`: FAIL at WALK-FORWARD** — only 50 out-of-sample
trades in the 19-month window (+1.58/trade, t 0.10), controls 59 / 34 / 58 %; on the confirmation screen q 4 is 1.12 × q 0 (< 1.25).
Not adopted. One pattern holds in BOTH periods and was not the pre-registered pick, so it is a hypothesis only: cascades that run
INTO resting size stacked against them (q 2) earn far less (+4.19 vs +8.79 agree; +4.96 vs +21.10) — a candidate skip rule for a
future pre-registration once more book data exists (the MBO archive ends 2026-03).
