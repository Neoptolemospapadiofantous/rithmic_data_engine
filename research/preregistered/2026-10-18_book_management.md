# Pre-registration — trade management and the daily path on the current book (written 2026-10-18, BEFORE any run)

Founder: "more tests" (after asking for a 90 % win-rate one-trade strategy — tiny targets lose: 2026-10-18 chat, 90 % at 10/100
points nets −0.93/trade at 2 pt). Book = the ablation result (2026-10-18_book_ablation): weakness long 09:30 (no stop) · cascade
09:50 / 10:30 / 11:00 / 11:30 · gap retest; 0.35 ATR stop, 2R, 1 trade a day.

Variants (one axis at a time against the book):
1. break-even: `be_r` b ∈ {0.5, 1} R (edge_lab managed exit: the stop moves to entry once the trade is b × R in favour).
2. earlier flat: 12:00 and 13:30 instead of 15:55 (all trades, the weakness long included).
3. daily path on the cascade/retest: trade them only when the 10-session daily path is NOT straight (e10 < 0.429), weakness long
   unchanged; and the complement (straight fortnights only) for reference.
Measured (`--trades`, 2 pt cost): pts/session 2010–18 / 2019–24 / 2025–26, win rate, green days and max drawdown 2019–24.
Adopt a variant only if pts/session ≥ the book's in BOTH 2010–18 and 2019–24 and its 2019–24 max drawdown is not higher.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--trades`, 2 pt cost; win = net > 0 after cost, so a break-even scratch counts as a loss).
| variant | pts/session 2010–18 / 2019–24 / 2025–26 | win | green days | max DD 2019–24 |
|---|---|---|---|---|
| book | +0.41 / +9.23 / +21.35 | 49 % | 41 % | 1,481 |
| be 0.5R | +0.44 / +9.11 / +16.87 | 35 % | 29 % | 1,680 |
| be 1R | +0.42 / +8.50 / +19.42 | 44 % | 37 % | 1,750 |
| flat 12:00 | −0.61 / +5.55 / +13.06 | 52 % | 43 % | 1,849 |
| flat 13:30 | −0.33 / +4.93 / +17.53 | 49 % | 41 % | 2,556 |
| cascade / retest only after a NOT-straight fortnight | +1.14 / +7.25 / +23.88 | 50 % | 31 % | 1,402 |
| … only after a straight fortnight (reference) | −0.19 / +5.10 / +15.66 | 50 % | 19 % | 1,540 |
**Nothing adopted.** Break-even stops turn would-be winners into scratches (likely: trend days often pull back to the entry before they run) and raise the
drawdown; flattening early cuts the afternoon part of the trend days and loses in 2010–18; the daily-path filter helps 2010–18
and 2025–26 but costs 2 pts/session in 2019–24, so it fails the both-periods rule. The book stays: 0.35 ATR stop, 2R, flat 15:55.
