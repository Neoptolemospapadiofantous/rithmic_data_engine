# Pre-registration — 1 trade a day, 1 NQ, 20–30 point target (written 2026-10-18, BEFORE any run)

Founder: "take 1 trade a day 1 NQ 20-30 points".
Points scaled to ATR so every year is comparable (today ATR ≈ 356 pts): target t ∈ {0.06, 0.08} ATR (≈ 21 / 28 pts),
stop s ∈ {0.06, 0.10, 0.15} ATR (≈ 21 / 36 / 53 pts). Flat 15:55, one trade a day, NQ 2010-07 → 2024-12, 1.0 pt cost
(on a 25-pt target cost is ~4 % of the win — the robustness gate's cost × 1.5 matters here). Family of 3, controls 99 %.
1. `scalp_daily_1005` — every day at 10:05, with the move since 09:30.
2. `scalp_mom_vwap` — first momentum + VWAP pullback of the day (mom_scan_3_pb's entry, N 45, er ≥ 0.2), 09:45–13:55.
3. `scalp_m1_1000` — 10:00 momentum (rdir_m1 rule), fewer days.

## Results (appended 2026-10-18 — tests unchanged)
| test | walk-forward | verdict |
|---|---|---|
| scalp_daily_1005 (every day) | −0.18 pts/trade, win 67 %; every variant negative whole-period (−0.83 … −1.11) | FAIL screen |
| scalp_mom_vwap (first momentum pullback) | −1.00, win 62 %; every variant negative | FAIL screen |
| **scalp_m1_1000** (10:00 momentum only) | **+2.46 (312, 19/yr, win 77 %, PF 1.47, 10/13 years +)** | **PASS** (controls 100 %, robustness 6/7) — best stop 0.15 ATR, target 0.08 ATR |
Points are historical NQ points (ATR ≈ 30 in 2012, ≈ 356 now); at today's ATR the frozen setting is ≈ 28-pt target / 53-pt stop.
A 20–30 point target every day loses; it works only behind the 10:00 momentum condition.

## Follow-up, pre-registered 2026-10-18 BEFORE running (founder: "could be more if we have more set ups but 1 is fine a day")
The FIRST tested setup of the day, whichever fires: cascade (09:50 ≥ 0.25 ATR; 10:30/11:00/11:30 ≥ 0.30 ATR from the open),
gap retest (|gap| ≥ 0.1 ATR, 5-min close beyond the 15-min range on the gap's side), 10:00 momentum (er ≥ 0.3), momentum +
VWAP pullback (er ≥ 0.2). Priority on the same bar: cascade > gap retest > 10:00 momentum > momentum pullback. Scan 09:50–13:55.
1. `union_2r` — max 1/day, stop stopk ∈ {0.25, 0.35} ATR, 2R (the setups' own exit).
2. `union_scalp` — max 1/day, stop 0.15 ATR, target t ∈ {0.06, 0.08} ATR (≈ 21 / 28 pts today).
3. `union_scalp_3` — as 2, up to 3 a day (later setups after the first trade closes).
Family of 3, controls 99 %. Reported: trades/yr and share of days with a trade.
Follow-up results:
| test | walk-forward | verdict |
|---|---|---|
| **union_2r** (first tested setup, 2R, stop 0.35 ATR) | **+4.01 (2,481; ≈ 80 % of days have a trade; win 43 %; 12/14 years +)** | **PASS** (controls 100 %, robustness 7/7, prop fee $1,112) |
| union_scalp (same entries, 0.08 ATR target / 0.15 stop) | −0.24, win 66 %; both variants negative whole-period | FAIL screen |
| union_scalp_3 | −0.13, win 68 % | FAIL screen |
union_2r 2025+ frozen (stop 0.35 ATR): +5.46 pts/trade on 249 trades, PF 1.10, t 0.7 — positive, thin (≈ 0.02 ATR a trade).
Verdict: one trade a day is possible with the tested setups taken in priority order and their own 2R exit; the 20–30 pt scalp
target turns the same entries into a loser.

## Validation (2026-10-18)
- `scripts/edge_lab_validate.sh --fast`: 15 of 15 as expected (noise rejected, look-ahead traps make no trades, drift caught).
- The combined rule with only its cascade part switched on reproduces `so_cascade` at the same setting exactly
  (2,072 trades, +3.58 whole-period; 1,633 walk-forward, +4.13) — the priority expression and the 5-minute scan are correct.
- `union_2r` re-run: identical (2,481 walk-forward trades, +4.01).
- The 10:00 momentum all-years spec is saved as research/ideas/m1_10am_all.json (was a scratch file) and re-run.
