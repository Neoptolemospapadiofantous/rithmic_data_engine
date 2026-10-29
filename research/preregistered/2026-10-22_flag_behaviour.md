# Flag behaviour: the flag's shape, a delayed entry, and exits when the flag misbehaves (written 2026-10-22, BEFORE the run)

Founder: "what about the flag behaivor. cause yes the pole of the flag might be ready but what about the behavior of the actual flag" → "yes".
Flags: flag_hhhl `export wide15` (NQ and ES, 2014-01 → 2026-10), mirrored so every flag reads as a bull flag. H = pole high, L = pole low,
M = H − L. The flag = the chart bars after the pole-high bar, followed until a bar's high goes above H (BREAKOUT), a bar closes below H − 0.5 M
(FAILURE), or 12 bars. Trades simulated on 1-minute bars: stop = the flag low so far − 0.1 ATR, target = H + M, flat 15:55, cost 1.5 NQ /
0.5 ES pts per trade (and +1× cost). One position at a time per market. Sets: BOOK2 (4-5m, pole >= 3×, pullback 5-38 %, A or B), RULE
(3-5m, pole >= 4×, pullback 15-50 %, A-list, 5m HH/HL, with the break), EVERY (3-5m, pole >= 4×, pullback 15-50 %).

## A. Shape (descriptive only — measured over the first up-to-6 flag bars, which can come after today's entry)
TIGHT BOX: depth (H − flag low) <= 0.38 M and |slope of the closes| <= 0.03 M a bar. DOWN CHANNEL: slope < −0.03 M a bar and >= 60 % lower
highs. DEEP: depth > 0.5 M. WEDGE: the later half's average bar range < 0.7 × the earlier half's. OTHER. Reported: share of flags, breakout
rate, average R of today's trade.

## B. Delayed entry (judged)
Wait for the close of the 4th flag bar. Condition HOLD: no breakout and no failure yet, and the flag low >= H − 0.5 M. Condition QUIET: the 4 flag
bars' average range <= 0.6 × the pole bars' average range. Entries from the next minute until the flag ends (breakout / failure / 12 bars,
entries before 15:00): D1 a limit at the middle of the flag so far ((H + flag low) / 2); D2 a buy stop at H + 0.25 (the breakout). Variants
HOLD+QUIET and HOLD only. Compared with today's trade (early mid entry) on all flags of the set.

## C. Exits when the flag misbehaves after today's entry (judged)
X1 time: if no 1m high above H by the close of the 6th chart bar after the entry bar, exit at that close. X2 half: exit at a chart-bar close
below entry − 0.5 × (entry − stop). X3 flag: exit at a chart-bar close below the flag low at entry. X4 break-even: once price trades above H,
the stop moves to the entry.

## Pass (each B / C variant)
On BOOK2 and on EVERY: avg R >= today's trade + 0.05R on BOTH markets, in both halves (2014-19, 2020-26), and still >= today's at +1× cost.
A passing variant is then run through the BOOK2 account EV (flag_prop_ev.cpp) before anything is adopted.

## Results (2026-10-22; research/programs/flag_behavior.cpp, out/flag_behavior.txt; re-simulation matches the export, mean |ΔR| 0.001-0.002)
**No delayed entry and no exit passes.**
A (descriptive, same on both markets and all three sets): TIGHT BOX ~43-46 % of flags, break out ~67-72 %, today's trade +0.36 to +0.82R;
OTHER ~26-31 %, break ~70-77 %, +0.32 to +0.77R; DOWN CHANNEL ~12-14 %, break ~28-38 %, −0.29 to −0.53R; DEEP ~10-13 %, break 5-8 %, −0.84
to −1.07R; WEDGE 1-3 %, mixed. What the flag does after the pole decides the trade — but most of it happens after today's entry.
B: waiting 4 bars loses the best fills. BOOK2 NQ: D1 HOLD+QUIET −0.053R vs +0.153R today (266 trades), D2 HOLD only +0.153R (677, PF 1.35)
but ES +0.033R; EVERY: nothing beats today's on both markets.
C: every exit cuts more winners than losers: BOOK2 NQ X1 −0.036, X2 −0.041, X3 −0.098, X4 −0.070R; ES X1 +0.016 (the only gain) but X2-X4
−0.03 to −0.11R; RULE and EVERY the same.
Reading: down channels and deep flags are the losers, but by the time a flag shows it is one, today's limit is already filled and the stop is
the cheapest exit; waiting for a clean flag gives up the good early fills.

## Part D — short-horizon decision on the flag (written 2026-10-22 after Parts A-C, BEFORE its own run)
Founder: "what about short horizon decion on the bull flags". On today's trade (early mid entry, stop under the flag, target the measured
move), fixed before the run:
- QUICK CHECK K: K minutes after the entry (K = 3 / 5 / 10 / 15), exit at that minute's close unless price has already traded at least
  +0.5R above the entry (S1), or unless that minute closes above the entry (S2).
- QUICK TARGET: take the profit at +1R / +1.5R / +2R instead of the measured move (Q).
Same pass as Parts B-C: >= today's + 0.05R on BOOK2 and EVERY, both markets, both halves, and at +1× cost.

### Part D results (2026-10-22; flag_behavior.cpp, out/flag_behavior.txt)
**Nothing passes; every short-horizon decision is worse than holding, and the shorter the horizon the worse.** BOOK2 NQ / ES vs today's
+0.153 / +0.100R: exit at 3 min unless +0.5R reached +0.017 / −0.015R; at 15 min +0.113 / +0.068R; exit unless in profit at 3 min +0.075 /
+0.019R, at 15 min +0.137 / +0.073R (closest, still below). Quick targets lose most: +1R −0.036 / −0.041R, +2R +0.029 / +0.033R. RULE and
EVERY the same (RULE NQ +0.392R → +0.18 to +0.36R with checks, +0.03 to +0.14R with quick targets). Cutting early or capping the
target removes the runners the average depends on.
