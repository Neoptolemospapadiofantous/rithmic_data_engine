# Pre-registration — the book's stop and target (written 2026-10-18, BEFORE any run)

Founder: "do more tests". The book (weakness long 09:30 no stop · cascade · gap retest, one trade a day) uses 0.35 ATR / 2R, carried
over from the single setups. Grid on the cascade + retest trades: stop s ∈ {0.25, 0.35, 0.5} ATR × target r ∈ {1.5, 2, 2.5, 3, 4} R
(15 cells; the weakness long unchanged). Measured (`--trades`, 2 pt cost): pts/session 2010–18 / 2019–24 / 2025–26, max drawdown
2019–24 in points.
Selection (fixed now): the cell with the highest 2010–18 pts/session; ADOPTED only if it beats the current cell (0.35 / 2) in BOTH
2010–18 and 2019–24 AND its 2019–24 max drawdown is ≤ 1.2 × the current one. A drawdown in points that grows with the stop is the
cost of the bigger target; the 1.2 × cap keeps a prop account's risk comparable.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--trades`, 2 pt cost). pts/session 2010–18 / 2019–24 / 2025–26, max DD 2019–24:
| stop \ target | 1.5R | 2R | 2.5R | 3R | 4R |
|---|---|---|---|---|---|
| 0.25 ATR | −0.19 / +5.56 / +20.72, 1,443 | −0.07 / +7.57 / +21.35, 1,267 | +0.08 / +8.18 / +23.64, 1,323 | +0.33 / +9.72 / +23.52, 1,323 | +0.45 / +9.54 / +23.44, 1,323 |
| 0.35 ATR | +0.01 / +7.50 / +20.23, 1,483 | **+0.41 / +9.23 / +21.35, 1,481 (current)** | +0.44 / +9.20 / +22.95, 1,424 | +0.62 / +9.02 / +21.80, 1,379 | +0.87 / +8.57 / +21.21, 1,379 |
| 0.5 ATR | +0.04 / +8.52 / +20.68, 2,117 | +0.18 / +8.11 / +17.68, 2,066 | +0.40 / +7.31 / +16.69, 2,024 | +0.43 / +7.25 / +17.76, 1,973 | +0.45 / +7.17 / +17.05, 1,914 |
Pick (highest 2010–18): 0.35 / 4R (+0.87) — lower than the current cell in 2019–24 (+8.57 vs +9.23) → **not adopted; 0.35 ATR / 2R
stays.** The 0.25–0.35 ATR × 2–4R block is a plateau (+7.6 … +9.7 in 2019–24); a 0.5 ATR stop earns less with ~40 % more drawdown.
