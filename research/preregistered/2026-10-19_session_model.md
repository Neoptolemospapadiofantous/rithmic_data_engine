# Pre-registration — a model of NQ session behaviour (written 2026-10-19, BEFORE any run)

Founder: "lets create models to analyse the behaviour of NQ" … "globex and asia" … "more tests to see how it behaves so our strategy is
the best". Single rules keep landing on coin flips; the question is whether the information known BEFORE a session, combined, predicts
how that session behaves.

Program: `research/programs/session_model.cpp` (reads data/edge_lab/cache + data/regime, read-only).

**Sessions (ET, trading day 18:00 → 17:00):** Asia 18:00 → 03:00, London 03:00 → 09:30, NY 09:30 → 16:00. Days with a full RTH session,
roll windows excluded.

**Targets (per session):** move = (close − open) / ATR (ATR = the day's 14-session RTH ATR); up = move > 0; trend = |close − open| ≥
0.5 × the session's range.

**Features (all known at the session's open):** the prior NY session's move, range, close location; gap and overnight move / range;
where the open sits against yesterday's NY high / low and the overnight high / low; the sessions earlier the same trading day (for London:
Asia; for NY: Asia + London) move and range; 5- and 20-session path and direction (e5, r5, e20, r20, p20); distance to the 50-day average;
VIX, its 1-day change, VIX/VIX3M; weekday; FOMC / CPI / NFP / earnings day; week so far; ES's same pre-session move.

**Walk-forward:** for each year Y = 2014 … 2024, fit on every earlier year, predict Y. 2025–26 is reported once, at the end (holdout).
Models: boosted depth-2 trees (lab Gbm, 150 rounds, min leaf 60) as primary, ridge as the cross-check.

**Measured, out of sample:** (1) direction hit rate vs the base rate (share of up sessions in the training years) and vs "always up";
(2) correlation of predicted vs actual move; (3) trend: share of trend sessions in the top vs bottom fifth of predicted P(trend);
(4) the trade: go with the predicted direction at the session open when |prediction| is in the top half of the training predictions,
hold to the session close, cost 1 pt (NQ), net pts/trade by era; (5) feature gain.

**Pass (per session, per target):** better than the base out of sample in BOTH 2014–18 and 2019–24, with the ridge agreeing in sign, and
for (4) net > 0 in both eras with t ≥ 2 overall. Anything that passes is then judged as a filter / size rule on the Vest operation plan
(2026-10-19_vest_funded) the same way as every add today. 2025–26 decides nothing.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (`research/programs/out/session_model.txt`, predictions `out/session_model_{0,1,2}.csv`; 9,924 session rows).
| session | era | direction hit (ridge, always-up) | corr pred/actual | trend share top vs bottom fifth | trade top-half (ATR/trade, t, before cost) |
|---|---|---|---|---|---|
| Asia | 2014–18 | 57.1 % (55.1, 56.9) | +0.036 | 46.4 vs 42.5 % | +0.038, t 2.40 |
| Asia | 2019–24 | 52.0 % (52.9, 54.2) | +0.032 | 46.7 vs 44.0 % | +0.019, t 1.45 |
| London | 2014–18 | 50.1 % (50.8, 51.4) | −0.034 | 47.8 vs 40.1 % | −0.002 |
| London | 2019–24 | 49.4 % (50.8, 51.4) | −0.013 | 44.7 vs 44.0 % | −0.006 |
| NY | 2014–18 | 49.2 % (53.9, 54.5) | +0.015 | 45.4 vs 44.4 % | +0.001 |
| NY | 2019–24 | 51.7 % (53.6, 54.9) | +0.028 | 55.6 vs 45.1 % | +0.011 |
Holdout 2025–26 (one look, decides nothing): Asia 45.6 % / corr −0.12; London 50.2 % / −0.15; NY 56.0 % / +0.155, trade +0.108 (t 1.76).
Asia every session in the model's direction after 1 pt: +0.0019 (2014–18), +0.0099 (2019–24) ATR — vs ALWAYS LONG +0.0012 / +0.0179.
**Verdict: nothing passes.** No session's direction is predicted better than "always up" in both eras (the boosted trees are below it in
5 of 6 cells); correlations are 0.01–0.04; the Asia trade is the upward drift, not the model; the NY trend model separates only in
2019–24. What drives the predictions (feature gain) is volatility and path context (prior NY range / move, VIX/VIX3M, week so far,
e20 / r20) — they predict SIZE of the session a little, not its direction. Together with the session, level, news, weekday and
timeframe studies today: at the session level NQ's direction is not forecastable from what is known at the open; the only direction
information is the move after the open (the cascade / gap rules), which the plan already trades.
