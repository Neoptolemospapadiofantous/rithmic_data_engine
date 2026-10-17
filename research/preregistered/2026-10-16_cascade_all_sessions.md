# Pre-registration — the cascade in the first 2 hours of every session (written 2026-10-16, BEFORE any run)

Founder: "test the cascade on all sessions first 2 hours". The cascade rule unchanged in shape: 20 minutes after the
session opens, if |price − session open| > k1 × ATR follow it; otherwise re-check every 30 minutes (k2 × ATR) until the
2-hour window ends; one trade a day; stop k × ATR, target 2R. ATR = the usual prior-14-day RTH ATR (overnight sessions
move less, so their grid uses smaller multiples). NQ 2010–26, 1.0 pt cost.

## Family: 7 tests, controls at 99.3 % (0.05 / 7), 400 seeds
| id | session open | checks | flat | grid |
|---|---|---|---|---|
| `cascade_asia_2h` | 18:00 | 18:20 (k1), 18:50, 19:20, 19:50 (k2) | 20:00 | k1 {0.1, 0.15, 0.2} × k2 {0.15, 0.2} × stop {0.1, 0.15} |
| `cascade_asia_hold` | 18:00 | same | 03:00 | same |
| `cascade_tokyo_2h` | 20:00 | 20:20, 20:50, 21:20, 21:50 | 22:00 | same |
| `cascade_tokyo_hold` | 20:00 | same | 03:00 | same |
| `cascade_london_2h` | 03:00 | 03:20, 03:50, 04:20, 04:50 | 05:00 | same |
| `cascade_london_hold` | 03:00 | same | 09:25 | same |
| `cascade_ny_2h` | 09:30 | 09:50 (k1), 10:30, 11:00 (k2) | 11:30 | the NY cascade's own grid |
Anything that passes then faces the frozen-setting holdout (choose on 2010–18 → test 2019–22; on 2010–22 → 2023–26).

## Results (appended after the runs — tests unchanged)
| test | OOS trades | net pts/trade | verdict |
|---|---|---|---|
| cascade_asia_2h | 304 | +4.32 (PF 1.50) | PASS the gates — but NOT validated: whole-period best variant +0.39 (t 0.5), average variant negative, walk-forward stood aside 111 of 183 months, random-time control 0 % (random entries in the window did better); frozen holdout: chosen on 2010–18 → −1.47 there, −0.64 on 2019–22; chosen on 2010–22 → +2.94 on 2023–26 (t 1.5); ~18 trades/yr, prop ≈ nothing |
| cascade_asia_hold | 377 | +1.02 | FAIL controls |
| cascade_tokyo_2h / _hold | 492 / 626 | −0.74 / +0.21 | FAIL |
| cascade_london_2h / _hold | 723 / 659 | −0.18 / −1.29 | FAIL screen |
| cascade_ny_2h (exit 11:30) | 1,864 | +2.17 | FAIL controls — under half of the full-day cascade's +4.80: the afternoon carries much of the move |
Verdict: the cascade works in New York only, and it needs to be held into the afternoon.
