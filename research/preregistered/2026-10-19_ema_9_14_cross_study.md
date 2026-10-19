# EMA 9 / 14 cross — what NQ does after it (written 2026-10-19, BEFORE any run)

Founder: "react on price action, for example when 9-14 cross, or get enough data to see the behaviour and create a strategy around it."
Program `research/programs/ema_cross_study.cpp`. Event = a TF bar close where EMA 9 crosses EMA 14 of TF closes (EMAs run continuously
across sessions, as on a chart); TF ∈ {1, 5, 15} min; sessions Asia 18:00–03:00, London 03:00–09:30, NY 09:30–16:00 (NY events up to 15:00).
Direction = the cross. Measured from the cross bar's close, in the day's ATR: signed move at +5 / +15 / +30 / +60 / +120 min, best and worst
point within 60 min, share positive at 30 / 60 min; net = minus 1 NQ pt. Baseline per TF × session: the same measures signed by a RANDOM
direction and by the last bar's own direction (momentum), and up-crosses vs down-crosses (NQ drifts up). Context splits (NY and all
sessions): time of day, cross agrees with the session's move so far, cross on the VWAP side, straight path (≥ 0.55), choppy (≥ 3 crosses
in the last 60 min), EMA gap at the cross. Eras 2010–18 / 2019–22 / 2023–26; "consistent" = same sign in all three eras and |t| ≥ 2.
A pocket becomes a strategy candidate only if its net 30–60 min move is consistent; it is then written as an edge_lab spec and must pass
every gate (screen, walk-forward, controls 99 %, robustness) and the one-look holdout like any idea.

## Results (2026-10-19, research/programs/out/ema_cross_study.txt)
The 9/14 cross carries no direction at all: signed forward moves at +5 … +120 min are −0.008 … +0.007 ATR (≈ 0–2 NQ pts) for every TF
(1 / 5 / 15 min), session (Asia / London / NY) and context (with / against the session's move, VWAP side, straight path, choppy / clean,
EMA slope, time of day); random-direction and last-bar baselines are the same ≈ 0. After 1 pt cost every cell is negative — the
"CONSISTENT" flags in the output are consistently NEGATIVE (the cost), never a positive pocket. MFE ≈ |MAE| within 60 min (0.07 Asia,
0.12 London, 0.18 NY ATR). → **No strategy candidate. The 9/14 cross is noise on NQ.**

## Follow-up, pre-registered 2026-10-19 BEFORE running (founder: "check 5m for entries but enter on the 1m … opportunities inside that 5m … or after 5m is closed")
Program `research/programs/mtf_entry_study.cpp`. NY 09:35–15:00 (and Asia / London for S1–S3 all-entries rows). 5-min SETUPS at the 5-min
close: S1 EMA 9/14 cross; S2 strong bar (body ≥ 0.08 ATR, close in the outer 25 % of the bar); S3 close beyond the previous 5-min bar's
high / low; S4 = S3 while the session path is straight (≥ 0.55) and the break is with the session's move. 1-min ENTRIES within the next
10 min: E0 market at the 5-min close; E1 limit at the 5-min bar's midpoint; E2 limit 0.02 ATR better than the close; E3 1-min dip-then-
resume (a 1-min bar closing against the setup, then a 1-min close beyond that bar's extreme). Trade: stop one tick beyond the 5-min
bar's far extreme, target 2R, exit at 60 min; net R after 1 pt. Baseline: E0 in a random direction. Inside the 5-min bar: does the
direction of its first 1 / 2 minutes predict its close (share, and the move from minute 2 to the close)? A setup × entry becomes an
edge_lab candidate only if net R > 0 in all three eras with |t| ≥ 2 and it beats the random baseline; then the usual gates + holdout.

## Results of the 5-min setup / 1-min entry study (2026-10-19, research/programs/out/mtf_entry_study*.txt)
Net R after 1 pt: every setup × entry × session negative. GROSS (no cost) the NY setups carry a small, era-stable edge: S4 (5-min close
beyond the prior bar, straight session, with its move) E0 +0.046 / +0.042 / +0.052 R (t 6.3), E3 1-min dip-then-resume +0.046 / +0.050 /
+0.064 (t 5.6); S2 strong bar +0.029 / +0.021 / +0.034 (E0, t 4.4); the 9/14 cross ≈ 0; London / Asia ≈ 0 or negative. At 2 pts × ATR/320
(today's cost-to-range) the best cell is exactly breakeven (S4 E3 +0.000). Limit entries at the bar's midpoint are worse than market (they
fill on the losers). Inside a 5-min bar, minutes 3–5 continue the first 2 minutes' direction only 46.9 % (move ≈ 0).
(The random-direction baseline is biased low by tight stops on bars against the random side — compare setups with each other and with
zero gross, not with that row.)

## Follow-up, pre-registered 2026-10-19 BEFORE running: NY 5-min trend continuation with a larger R
The edge is real but smaller than the cost at a 5-min-bar stop. Give each trade more room to pay: `ny5_cont` — NY 09:35–14:30, 5-min scan:
the 5-min close is beyond the previous 5-min bar's high (low), the session path so far is straight (|px − open| / (hod − lod) ≥ 0.55) and
price is on that side of the 09:30 open; enter at the 5-min close; stop one tick beyond the 5-min bar's far extreme (max(px − lo, hi − px)
+ 0.25); target rr ∈ {2, 3, 4} R; max_bars 60 (`ny5_cont`) or hold to 15:55 (`ny5_cont_close`) → 2 specs × 3 = 6 variants; max 3 trades a day, one at a time. edge_lab gates
(controls 99 %), 2010-07 → 2024-12; then the one-look holdout; then, only if both pass, the book-F add test.
Result (2026-10-19, edge_lab, loaded to the DB): `ny5_cont` (60-min hold) **FAIL at WALK-FORWARD** — −0.12 pts/trade on 2,825, every rr ≈ 0;
`ny5_cont_close` (hold to 15:55) **FAIL at CONTROLS** — +1.39 pts/trade on 2,646 (PF 1.07, t 1.17), beats random direction 97 % (need 99 %)
but random TIME on the same days only 67 % and random day 76 %; eras +0.80 / +2.63 / +0.16; ES −375 pts; prop break-even fee $72.
**Reading:** what the 5-min continuation earns is the straight session's direction itself; WHEN inside that session you enter (on a new 5-min
high, on a 1-min dip-and-resume, or at a random minute) adds nothing measurable. Same conclusion as the 1 / 5 / 15-min entry study
(2026-10-19_entry_timeframes) and the cascade pyramid (2026-10-16). Closed.
