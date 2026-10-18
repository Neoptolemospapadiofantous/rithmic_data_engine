# Pre-registration — PATH and DIRECTION on the daily timeframe (written 2026-10-18, BEFORE any run)

Founder: "more tests. also on a daily can we set the path and direction?". The intraday edge is the day direction on a straight
path at 10:00 (2026-10-18_path_direction_map / _robustness). Here the same two readings one timeframe up, from prior RTH closes
only (known before the day): daily DIRECTION Dd(N) = sign(C[-1] − C[-1-N]); daily PATH e(N) = |C[-1] − C[-1-N]| / Σ|ΔC| over those
N steps. N ∈ {5, 10, 20} (new DSL vars e5 r5 e10 r10; e20 r20 exist). Straight = e(N) ≥ the top-tercile cut of e(N) on 2010–18
(measured before any outcome: printed in Results), choppy = below the bottom-tercile cut.

## Test 1 — daily path × direction as a filter on the confirmed GAP rule (pdm_gap_confirm: gap side, 10:00 candle close, intraday
path ≥ 0.55, stop 0.25 ATR, 2R)
Cells per N: (a) daily straight AND Dd = the trade's side · (b) daily straight AND Dd against · (c) daily not straight.
Discovery 2010–18 screen; pick: the cell with the highest net/trade with ≥ 60 trades that nets ≥ 1.25 × the unfiltered GAP rule
(+2.84) AND is above it in both halves 2010–14 / 2015–18 (base halves +2.07 / +3.57). Confirmation 2019–24: full gates
(`control_pct` 0.983 for 3 N, 400 seeds) AND ≥ 1.25 × its complement AND above the unfiltered rule (+22.48 screen / +28.26 WF).

## Test 2 — daily path × direction on its own
Enter at the 10:00 candle close in Dd's direction when the daily path is straight (no intraday conditions), stop 0.25 ATR,
2R, flat 15:55. One idea, variants N ∈ {5, 10, 20}; full edge_lab gates on 2010-07 → 2024-12 (`control_pct` 0.983, 400
seeds), the usual drift rule: the long and the short trades must each net > 0 (NQ rose over the sample).
Also reported (description): the same with the INTRADAY gates added (intraday path ≥ 0.55, price on Dd's side of the open) —
i.e. Dd as a third direction source beside gap and momentum.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab; discovery `--quick`, confirmation + test 2 via scripts/edge_lab.sh).
Tercile cuts on 2010–18 (session_stats RTH closes, distribution only, before any outcome): e5 0.298 / 0.661, e10 0.191 /
0.429, e20 0.134 / 0.307 → "straight" = e5 ≥ 0.661, e10 ≥ 0.429, e20 ≥ 0.307.

### Test 1 — filter on the GAP rule, discovery 2010–18 (unfiltered +2.84 on 279)
| N | daily straight, same side as the gap | daily straight, against | daily NOT straight |
|---|---|---|---|
| 5 | +1.34 (29) | +1.53 (46) | +3.34 (204) |
| 10 | −0.44 (39) | −2.39 (39) | **+4.49 (201)** |
| 20 | +2.05 (34) | −0.68 (43) | +3.65 (201) |
The gap rule works best when the market has NOT trended straight over the last sessions, whichever way. Pick: N = 10, not
straight (+4.49 = 1.58 × base; halves +2.49 vs +2.07, +6.68 vs +3.57).
Confirmation 2019–24 `dpf10_notstraight_confirm`: **PASS every gate** — walk-forward +29.44/trade on 119 (t 3.75), eras
+31.68 / +26.12, controls direction 100 % / day 100 % (time 0 %: the window is one bar, so a random time ties with the real
one — not claimed), robustness 6/6. vs the unfiltered rule: +29.44 vs +28.26 walk-forward, +24.18 vs +22.48 screen ✓.
vs its complement (daily straight, 62 trades: +25.19 same side / +11.41 against → +18.30) 1.32 × ✓ → **CONFIRMED**.
Holdout 2025+ (one look): 2025 29 trades +1,255, 2026 7 trades +230 → **36 trades +1,485 (+41/trade): PASS**; the excluded
daily-straight days: 7 trades −3 (the unfiltered gap rule's holdout was 50 trades +919).
The complement is still positive in 2019–24, so per the earlier filter studies' convention this is a SIZE rule (full size when
the 10-session daily path is not straight, reduced when it is), not a hard skip.

### Test 2 — daily path × direction on its own (2010–24, full gates) — FAIL
dpa_5 −2.63/trade (FAIL screen) · dpa_10 −1.04 (FAIL screen) · dpa_20 +0.56 (FAIL walk-forward, PF 1.02). A straight daily
trend does not continue into the next session on its own. With the intraday gates added (description): N=5 −2.43 (204),
N=10 +3.19 (220), N=20 +3.12 (244) — weaker than the gap side as a direction source.

**Verdict:** on the daily timeframe the useful reading is the PATH, not the direction: the 10:00 gap rule pays more after a
choppy fortnight (e10 < 0.429) and less after a straight one, in 2010–18, 2019–24 and 2025–26. Daily direction alone is no edge.
