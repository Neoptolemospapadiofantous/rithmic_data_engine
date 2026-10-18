# Pre-registration — a high-win-rate version of the every-day book (written 2026-10-18, BEFORE any run)

Founder: "test more and find ways to trade a day. 1-2-3 trades high win rate". The every-day book (2026-10-18_daily_book: the
confirmed setups, 1 NQ, one position at a time) wins ~40 % of trades at 2R. A high win rate comes from where the target sits
against the stop; this tests whether an exit with a high win rate keeps the edge. Entries unchanged (no new entry rules).

Grid (one edge_lab idea per max-trades setting, 1 and 3 a day): target tg ∈ {0.5, 0.75, 1, 1.5, 2} R · stop sk ∈ {0.25, 0.35,
0.5} ATR for the 10:00 / cascade / retest setups · weakness long ws ∈ {0 = no stop (as now), 1.5} ATR, same target in R when it
has a stop. 30 variants each. Reported per variant: trades, win rate (net > 0 at 1.0 pt), net/trade, and pts/session at the
measured 2.0 pt cost; per period 2010-07 → 2018, 2019–24, 2025–26 (screen).

Selection (fixed now): among variants with win rate ≥ 60 % in BOTH 2010–18 and 2019–24 and pts/session > 0 at 2.0 pt in both,
the one with the highest 2010–18 pts/session. It is then judged on 2019–24 and 2025–26 against the current book (2R / 0.25 /
no stop): adopted as the "high win rate" form only if its 2019–24 pts/session is ≥ 75 % of the current book's (≈ +10.6 at 2 pt).
Per-day statistics (days ≥ +20, ≤ −20, drawdown, worst day) for the pick via `--trades`. No pick qualifying = state it.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick`, new `win` column in variants.csv; sessions 2010–18 2,057 · 2019–24 1,472 ·
2025–26 314; pts/session at 2.0 pt = trades × (net/trade − 1) / sessions).

Selected rows, max 1 trade/day (win % · pts/session: 2010–18 | 2019–24 | 2025–26):
tg 0.5 sk 0.35: 66 % −0.6 | 69 % +5.1 | 68 % +17.6 · tg 0.75 sk 0.35: 58 % −0.4 | 60 % +4.9 | 61 % +20.2 ·
tg 1 sk 0.5: 52 % −0.2 | 55 % +6.1 | 53 % +18.6 · tg 2 sk 0.35: 45 % +0.4 | 49 % +8.9 | 48 % +21.3 (all ws 0).
Max 3/day is within ±1 pt/session of max 1. A 1.5 ATR stop on the weakness long (ws 1.5) lowers pts/session in every row.

**Selection: NO variant qualifies** — every variant with win rate ≥ 60 % nets < 0 per session at the 2 pt cost in 2010–18. Not
adopted. The win rate is set by the target: 0.5R ≈ 67 %, 0.75R ≈ 59 %, 1R ≈ 54 %, 2R ≈ 46 %; the points fall as the win rate rises.

Per-day (`--trades`, 2019-01 → 2026-10, 2 pt cost, weakness long without a stop):
| form | trades | win | green days | red days | days ≥ +20 | days ≤ −20 | pts/session | max drawdown | longest red streak |
|---|---|---|---|---|---|---|---|---|---|
| high win: 0.5R, 0.35 ATR, ≤ 3/day | 2,110 | 68 % | 51 % | 33 % | 44 % | 28 % | +8.5 | 1,373 | 6 |
| middle: 1R, 0.5 ATR, 1/day | 1,503 | 54 % | 46 % | 38 % | 41 % | 33 % | +8.3 | 1,728 | 8 |
| current: 2R, 0.35 ATR, 1/day | 1,503 | 49 % | 41 % | 43 % | 36 % | 39 % | +11.1 | 1,279 | 13 |
Worst day −550 in all three (the weakness long without a stop). For 2019–26 the high-win form trades more evenly (half the
sessions green, red streaks ≤ 6) for ~25 % fewer points; it fails the 2010–18 condition, so it stays a founder's choice, not a
tested edge improvement.
