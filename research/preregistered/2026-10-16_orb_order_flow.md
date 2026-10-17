# Pre-registration — ORB with order flow, round 2 (written 2026-10-16, BEFORE any of these runs)

Founder: "test all. remember we dont want to overfit." Every test below is fixed here; nothing is added, dropped or
re-tuned after seeing a result. A failure is reported as a failure.

## Family and pass bar
8 tests (listed below). All edge_lab gates as usual, PLUS: every control the test relies on must be beaten in ≥ 99 % of
seeds (not 95 %) — 0.05 / 8 ≈ 0.6 %, so 99 % keeps the family's false-pass chance near 5 %.
Grids are capped at 8 variants per test; settings are round numbers chosen before the run.

## Data split
- Development: order flow 2023-03 … 2026-03-19 (`mbo_1m` ends there); everything is judged on this.
- Holdout (reported, never used to choose): the collector's own sessions 2026-09-18 … 2026-10-16 (~20 sessions).
  Too short to pass or fail anything; it is a sanity check only.

## Tests
1. **Flow-flip exit** (edge_lab exits, new rule `flow`): close when a completed 5-minute bar's aggressor delta is
   against the trade by ≥ 20 % of that bar's volume. Hard stop and target kept. Run on `orb5_of`, `orb15_of`,
   `so_cascade` (3 tests). Timing control: path-blind twin, ≥ 99 % (unchanged).
2. **Range-window flow** (`orb5_rangeflow`, `orb15_rangeflow`; 2 tests): ORB break taken only when the net aggressor
   delta INSIDE the opening range window (09:30 → 09:30 + m) agrees with the break by ≥ f × that window's volume,
   f ∈ {0, 0.05}; stop k × ATR, k ∈ {0.15, 0.25}; target R ∈ {2, 3} (8 variants).
3. **Liquidity pulled before the break** (`orb5_liqpull`; 1 test): ORB 5 break (flow agrees, f = 0) where the resting
   size on the far side (ask top-10bps for a long, bid for a short) averaged over the 5 minutes before the break is
   ≥ p below its average over the 5 minutes before that, p ∈ {0.1, 0.2}; k ∈ {0.15, 0.25}; R ∈ {2} (4 variants).
4. **Absorption at the range edge** (`range_absorption`; 1 test): 09:45–11:30 on completed 5-minute bars: the bar
   traded within 0.05 ATR of the 15-minute range low (high), its aggressor delta is ≤ −a (≥ +a) × its volume, and it
   closed back inside the range → trade AGAINST the aggressors (long at the low, short at the high), a ∈ {0.1, 0.2},
   k ∈ {0.15, 0.25}, R ∈ {2} (4 variants).
5. **Meta-label with flow/book inputs** (1 test): the cascade's take/skip model (meta.hpp) with three added inputs
   known at the signal — aggressor delta of the last 15 minutes / volume, book qty imbalance (top 10 bps) averaged
   over the last 5 minutes, cancel-to-add ratio over the last 5 minutes (NaN before 2023-03). Compared with the
   existing meta result (+8.30 pts/trade); passes only if it beats it AND clears the meta gates at 99 %.

## Not tested (and why)
- More history (Databento ES / pre-2023 NQ order book): needs a purchase — price only, buy only on the founder's yes.
- New thresholds or stop/target grids for `orb5_of` / `orb15_of`: that is the overfitting route.
- `ofo_ny5*` paper ids: already running forward; read, not changed.

## Results (appended after the runs — tests unchanged)
| # | test | net pts/trade (OOS) | verdict | why |
|---|---|---|---|---|
| 1 | flow exit on `orb5_of` | +6.52 vs +5.49 | FAIL | fired on 21 trades only; timing control 99.0 %; one era of data (the exit gate needs 2 of 3) |
| 1 | flow exit on `orb15_of` | +5.99 vs +4.09 | FAIL | fired on 19 trades; timing 100 %; one era |
| 1 | flow exit on `so_cascade` | +4.87 vs +4.80 | FAIL | timing 94.5 %, better in 1 of 3 eras |
| 2 | `orb5_rangeflow` | +7.28 (404 tr) | FAIL | controls: direction 96 %, time 96 %, day 82 % (need 99 %); 2024 +1,860 / 2025 +177 / 2026 +904 |
| 2 | `orb15_rangeflow` | +6.37 (350 tr) | FAIL | controls 86 / 85 / 76 % |
| 3 | `orb5_liqpull` | +6.52 (400 tr) | FAIL | controls: direction 96 %, time 58 %, day 70 %; positive EVERY year (2023 +500, 2024 +524, 2025 +1,077, 2026 +507) — the only one not carried by 2024. Not tradeable live: the feed has no depth (Rithmic L2 not entitled) |
| 4 | `range_absorption` | −2.34 (69 tr) | FAIL | screen |
| 5 | meta + flow/book inputs | +7.98 vs plain meta +8.22 (same data) | FAIL | shuffled labels 92 %; the new inputs add nothing |

Holdout (collector 2026-09-18 … 10-16): 3–4 trades per range-flow variant — no information, as expected. The collector's
aggressor data matches the history (buy share 0.505 vs 0.496, |delta|/volume 0.067 vs 0.066 per minute, 09:30–09:35).

Verdict: nothing in the family passes. Every flow filter lifts the ORB's average trade by 4–8 pts, but none beats a random
entry time / random day in the same month often enough, and three years of order flow cannot separate that from luck.
