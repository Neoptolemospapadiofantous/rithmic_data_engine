# Pipeline and data validation (2026-10-16)

Founder: "we want to make sure that everything is validated and the way those data interact between them is correct".

## 1. Data integrity (edge_lab cache, scratch check over every bar)
| market | bars | full RTH days | 09:30 bar on full days (DST months) | RTH minutes missing / day | duplicates / out-of-order / bad OHLC | gaps > 3 ATR at the open |
|---|---|---|---|---|---|---|
| NQ | 5,218,889 | 3,842 | 3,842 / 3,842 (2,425 / 2,425) | 0.13 | 0 / 0 / 0 | 3 — 2015-08-24, 2016-06-24, 2020-02-24 (real events) |
| ES | 5,464,074 | 3,867 | 3,867 / 3,867 (2,447 / 2,447) | 0.03 | 0 / 0 / 0 | 5 (none in a roll window) |
| BTC (log) | 3,534,759 | 2,453 | 2,453 / 2,453 | 0.02 | 0 / 6 / 0 | 29 |
NQ/ES are clean; contract rolls show no artificial jumps. BTC's 6 out-of-order bars are the November fall-back hour (ET
wall clock repeats; futures are closed then, crypto is not) — harmless for the crypto tests run.

## 2. Look-ahead audit (every function that reads prices)
Two holes found and closed (tests added, 545 checks): `lag(k, …)` accepted a NEGATIVE k (reads the future) — no spec ever
used one; the pair price `xs` and the meta model's ES input took the NEXT bar when the current minute was missing (≤ 2
minutes ahead). Re-running the affected results gave identical numbers (ES has a bar every minute), so no verdict changes.
Checked and correct: `cl/op/hi/lo` (k ≥ 1, completed bars), `at()` (only once opened), `hi_since/lo_since`, `rhi/rlo`,
`dwin/vwin/whi/wlo` (only after the window), `bk` (minutes before the bar), day facts (all from earlier days), macro (last
observation before the date), `xr` (pair close strictly before the bar), thesis exits (decided on the closed bar, filled
at the next open). Known optimism, covered by a robustness check: a decision on the bar's open fills at that same open —
every PASS also had to survive "entry 1 bar late".

## 3. Null markets (can the pipeline manufacture an edge?)
Two synthetic NQ histories: inside every day the 1-minute moves are shuffled and each whole day's direction is flipped at
random (volatility, ranges, volume kept; timing, persistence, day-to-day relations destroyed). 20 ideas (every PASS and
near-miss incl. the 2,800-variant cascade grid) × 2 nulls = 40 runs: **0 PASS**. The cascade nets −2 pts/trade there
(≈ its cost) — its real-data result is not an artifact of the pipeline.

## 4. Parity — edge_lab (1-minute bars) vs the paper engine (ticks), cascade on the recorded sessions
On the four days both traded (2026-10-12 … 15): same direction, same entry time, entry prices within 0.25–0.75 pt.
edge_lab starts only on 10-12 (it needs 14 full days after the 2026-03…09 data hole); the paper engine's ATR uses a
shorter window. **Mismatch found:** the paper engine and the executor sized stops with `PaperDb::session_atr14_before`,
which returned YESTERDAY's stored `atr14_pts` — the mean of the 14 sessions before yesterday, one day stale — while every
research result uses the 14 sessions before today (e.g. 2026-10-15: paper 289.70, research 307.45). **Fixed**: it now
averages the 14 most recent sessions before today (same staleness guard); verified to equal the research ATR on 10-12 …
10-15. Takes effect for paper at tonight's deploy (golden snapshot re-frozen with the diff kept in
data/logs/golden_diff_20261016.txt); the dry executors pick it up at their next restart.
