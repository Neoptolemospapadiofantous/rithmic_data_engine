# Pre-registration — full risk only on the strongest trend days (written 2026-10-19, BEFORE any run)

Founder: "more tests" after the full-risk / full-target study (2026-10-19_full_risk_full_tp). Vest has no time limit and no
minimum days, so skipping a day is free: does trading full size ONLY on the strongest cascade days raise the pass rate?
Base: the cascade (09:50 > 0.25 ATR from the open, re-checks 10:30 / 11:00 / 11:30 at 0.30; 0.35 ATR stop; flat 15:55). Filters
at the check bar (one trade a day): A none · B straight path |px−open|/(hod−lod) ≥ 0.55 · C gap ≥ 0.1 ATR on the same side ·
D ES moved ≥ 0.25 of its ATR the same way (xs) · E = B and C · F = B and D · G shorts only after the prior VIX close fell + longs
under B. Targets 2R and 3.33R → 14 cells. Vest $5k sim, risk $145/trade, lenient daily-loss model, $40 fee.
Selection: the cell with the highest pass rate on accounts STARTED 2010-07 … 2018-12 (sim run on 2010–18 trades only) with ≥ 40
trades/yr; it is then reported once on 2019–26 starts, against base A/2R at $145 and the book at $50 / 2R (64.1 %).

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (build-exp/edge_lab --quick --trades; Vest sim lenient, $145, $40 fee). Pass rate on 2010–18 starts → 2019–26 starts:
A none 19.5 → 24.8 % (2R) / 19.9 → 26.0 % (3.33R) · B path 22.5 → 26.3 / 21.2 → 23.3 · C gap agrees 27.1 → 23.4 / 25.6 → 21.3 ·
D ES ≥ 0.25 27… 20.3 → 24.4 / 20.9 → 22.3 · E path + gap 31.4 → 25.6 / 28.8 → 22.0 · F path + ES 24.5 → 23.5 / 23.0 → 21.9 ·
G 22.2 → 24.3 / 21.2 → 23.9. Trades/yr 2019–26: A 172, B 155, C 74, D 137, E 65, F 122, G 125.
**Selected E / 2R (31.4 % in discovery, 55/yr). Confirmation 2019–26: 25.6 % ($156 per pass, median 4 trade days) vs base A
24.8 % — no gain.** Every filter lands at 21–26 % on 2019–26: at full risk the pass rate is set by the size (two stops and the
account is gone), not by which day is traded. Fewer, better days do not lift it.

## Extra (description, not pre-registered): risk-size sweep, Vest sim lenient, 2019–26 starts
First trade of the day, 2R — $40 61.2 % (55 trade days) · $50 53.6 % (40) · $60 49.7 % (27) · $75 48.5 % (19) · $90 47.4 % (15) ·
$100 47.2 % (13) · $120 44.5 % (9, $90/pass) · $145 24.5 % (5, $163/pass). The cliff is between $120 and $145: at $120 two stops
leave room for a third attempt inside the $300 DD and a stop never touches the $150 daily line.
All F trades (up to 3/day), 2R — $40 74.6 % (30) · $50 66.0 % (20) · $60 58.8 % (15) · $75 38.5 % (8): above $60 two same-day
stops breach the $150 daily limit.

## Extra (description): "just follow the session move" — one rule, all sessions (2026-10-19)
One hour in, if price is > 0.25 ATR from the session open, go that way; 0.35 ATR stop, 2R, flat at the session end (2010–24):
Asia (19:00, open 18:00) FAIL walk-forward −4.15 on 69 (4/yr); London (04:00, open 03:00) FAIL screen −8.93 on 50 (3/yr);
NY 10:30 FAIL controls +6.16 on 865, 2010–18 −0.73; NY 09:50 FAIL controls +4.37 on 791. The full cascade (09:50 + re-checks 10:30 /
11:00 / 11:30) is the version that passes. Caveat: the 0.25 RTH-ATR bar fires only 3–4×/yr overnight — the session table
(scratchpad sess/) shows overnight follow-through at coin-flip rates at any threshold, so a lower bar is not expected to help.
