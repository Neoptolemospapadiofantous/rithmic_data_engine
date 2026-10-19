# Pre-registration — the book without expiry weeks; the cascade on big-tech earnings days (written 2026-10-19, BEFORE any run)

1. DESK open lead (4): book F (cascade + gap retest + 10:00 gap rule, side by side, 0.35 ATR, 2R) with vs without `rollwin` days
   (roll Thursday −3 … +6, where NQ.c.0 runs on the thinning expiring contract). Adopt the skip only if F without them has higher
   pts/session in BOTH 2010–18 and 2019–24 and pts per point of max DD 2019–24 not lower; 2025–26 reported; Vest $50 pass reported.
2. Earnings (2026-10-19 news table: opening move continued 62–67 % on big-tech earnings days 2019–26, not 2010–18): the cascade's
   net/trade on `earn` days vs other days by era. SIZE-UP rule as for VIX: ≥ 1.25 × the base in BOTH 2010–18 and 2019–24; SKIP if
   < 0 in both; else nothing.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (rebuilt book from the three specs, `--quick --trades`; 2 pt cost; the rebuild differs slightly from late/F.csv, so
both arms of each comparison use the rebuild).
1. Expiry weeks: with +0.31 / +16.11 / +12.05 pts/session, max DD 1,304, pts/DD 0.0123, Vest $50 44.3 % → without rollwin days
   +0.30 / +16.16 / +11.61, DD 1,268, 0.0127, 46.1 %. 2010–18 lower by 0.01 → **rule not met; no change** (the expiry week neither
   helps nor hurts the book).
2. Earnings: cascade net/trade earn vs other — +2.55 (135) vs +0.61 / +11.66 (102) vs +8.00 / 2025–26 +53.56 (17) vs +10.25. Base
   +0.80 / +8.38 → **SIZE-UP met by the letter**; ES (book settings) earn > other in every period (+0.14 vs +0.01, +2.57 vs +0.74,
   +8.58 vs +4.30). BUT the difference is not significant (2010–24 t 0.60, 237 earn trades), and a 2nd unit on earn-day cascades
   lowers pts/DD (0.0123 → 0.0121) and the Vest $50 pass rate (44.3 → 38.6 %). **Not adopted** — noted as a weak tilt only.
