# Pre-registration — the cascade on big-tech earnings mornings (written 2026-10-16, BEFORE any run)

Founder: "test all". Data: SEC EDGAR 8-K filings with item 2.02 (results of operations = the earnings release) for NVDA,
AAPL, MSFT, AMZN, GOOGL (Google Inc. before 2015), META, AVGO (Avago before 2018) — 456 releases since 2010, free
official data. Reaction session = the first RTH session after the filing's acceptance time (after-close releases → next
trading day; pre-open releases → that day). CPI days were planned too but could not be tested: BLS and ALFRED refuse
scripted downloads and FRED's data API has no release dates.

## Tests (controls 97.5 % = 0.05 / 2, 400 seeds)
1. `cascade_earn` — the cascade unchanged, only on reaction mornings. Why: a megacap's earnings re-price the index
   overnight; index funds, hedgers and options dealers must reposition at the cash open, so the open's direction should
   carry more.
2. `cascade_noearn` — the complement (all other days).
Use: if `cascade_earn` passes every gate AND its frozen holdout is positive in both splits AND it beats the base per trade,
it becomes a SIZING candidate (more units on earnings mornings) for a forward test — never a filter (the other days keep
the cascade).

## Results (appended after the runs — tests unchanged)
Data: 456 releases → 344 reaction sessions (2010-07 … 2026-03; 26 releases fall before the data or across the 2026 hole).
| test | trades | net pts/trade | controls (dir / time / day) | verdict |
|---|---|---|---|---|
| cascade_earn | 217 | +7.04 (PF 1.29, t 1.2) | 74 / 33 / 63 % | FAIL — not distinguishable from luck; eras +2.8 / −1.6 / +24.9; years swing from −706 (2022) to +874 pts (2025) |
| cascade_noearn | 2,180 | +4.10 | — | FAIL controls (complement, reported for the comparison) |
Not a sizing candidate. The cascade stays unconditional. CPI days: not tested (no scriptable free source for release
dates — BLS "Access Denied", ALFRED / FRED release pages refuse the connection).
