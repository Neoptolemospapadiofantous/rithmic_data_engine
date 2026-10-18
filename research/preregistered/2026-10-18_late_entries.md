# Pre-registration — are the late cascade checks and late retests worth taking? (written 2026-10-18, BEFORE any run)

Founder: "more tests". In the side-by-side book F (cascade + gap retest + 10:00 gap rule, overlap allowed; 2026-10-18_multi_trade_day)
the cascade checks at 09:50 / 10:30 / 11:00 / 11:30 and the retest scans to 13:55. Late entries have less of the day left to run.
Q1 cascade checks: C4 all four (current) · C1 09:50 only · C2 09:50 + 10:30 · C3 09:50 + 10:30 + 11:00.
Q2 retest window end: 13:55 (current) · 12:00 · 11:00.
Each variant replaces only its own leg inside F (others unchanged); 0.35 ATR / 2R / flat 15:55; 2 pt cost.
Rule per question (fixed now): a shorter window is ADOPTED only if F with it has pts/session ≥ the current F's in 2010–18 AND
≥ 0.9 × the current F's in 2019–24 AND higher 2019–24 pts per point of max drawdown. If several pass, the shortest. 2025–26 reported.
Also reported: the Vest $5K eval at $50/trade.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick --trades`; each variant's leg swapped inside F; 2 pt cost; Vest at $50).
| F with | pts/session | trades/session | ≥ +20 | max DD 2019–24 | pts/DD | Vest |
|---|---|---|---|---|---|---|
| current (C4, retest to 13:55) | +0.15 / +15.05 / +13.86 | 1.26 | 35 % | 1,247 | 0.0121 | 62.9 %, 24 days |
| C1 09:50 only | +0.23 / +12.85 / +9.60 | 0.90 | 27 % | 1,662 | 0.0077 | 71.3 %, 33 days |
| C2 09:50 + 10:30 | −0.18 / +14.07 / +10.42 | 1.11 | 31 % | 1,123 | 0.0125 | 63.6 %, 28 days |
| C3 + 11:00 | −0.01 / +14.57 / +10.57 | 1.20 | 34 % | 1,123 | 0.0130 | 64.0 %, 26 days |
| retest to 12:00 | +0.13 / +14.45 / +13.32 | 1.24 | 35 % | 1,530 | 0.0094 | 62.1 %, 24 days |
| retest to 11:00 | +0.23 / +13.84 / +12.44 | 1.19 | 33 % | 1,494 | 0.0093 | 59.1 %, 23 days |
**Nothing adopted — the late entries stay.** C1 fails 2019–24 (12.85 < 13.55); C2/C3 fail 2010–18; the retest cut-offs fail
2010–18 (12:00) or points per drawdown (11:00). The late cascade checks and the afternoon retests each add points in every period
and, by spreading entries over the day, LOWER the drawdown — cutting them makes the book lumpier, not steadier.
