# Pre-registration — path and direction on the weakness long (written 2026-10-18, BEFORE any run)

Founder: "do more tests". The weakness long (prior IBS ≤ 0.3 and VIX ≥ 20 → buy the 09:30 open, no stop, flat 15:55) is the book's
biggest earner in 2025–26 and the source of its −550-pt days; today's path / direction reads were never applied to it.
Variants (same days — W days only):
- W0 (current): buy 09:30 open, hold to 15:55.
- A1 confirm: buy at the 10:00 candle close only if price > the 09:30 open; hold to 15:55, no stop.
- A2 confirm + path: as A1 and the path since 09:30 ≥ 0.55 (straight up).
- B early exit: buy the 09:30 open; at the 10:00 candle close, if price < open AND path ≥ 0.55 (straight down), exit there; else hold.
Measured per variant (2 pt cost, per W day; `--trades`): net/trade and total in 2010–18 / 2019–24 / 2025–26, worst day, max
drawdown 2010–26. Adopt a variant only if its TOTAL (not per trade — A1/A2 trade fewer days) is ≥ W0's in BOTH 2010–18 and 2019–24
and its worst day is not worse. Else W0 stays.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--trades`, 2 pt cost; B = W0 with the 09:30 → 10:00 result on the 59 straight-down-at-10:00 W days).
| variant | 2010–18 total (trades, per trade) | 2019–24 total | 2025–26 total | worst day | max DD 2010–26 |
|---|---|---|---|---|---|
| W0 current | +1,103 (135, +8.17) | +4,587 (219, +20.94) | +5,710 (28) | −550 | 1,306 |
| A1 confirm above the open | +609 (69, +8.83) | +2,744 (118, +23.25) | +2,646 (20) | −591 | 1,353 |
| A2 confirm + straight path | −187 (29, −6.45) | +2,664 (47, +56.68) | +2,350 (10) | −447 | 802 |
| B exit at 10:00 if straight down | +532 (135, +3.94) | +4,473 (219, +20.43) | +5,442 (28) | −550 | 1,399 |
**Nothing adopted — W0 stays** (every variant's total is lower in both periods). Waiting for confirmation skips the weakness days
that rise from the open; exiting the straight-down mornings at 10:00 loses because those days recover by the close (2010–18
+1,103 held vs +532 cut). For this trade the path read at 10:00 is not a reason to act.
