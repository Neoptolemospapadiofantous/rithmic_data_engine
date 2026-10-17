# Regime optimisation, walk-forward (pre-registered 2026-10-17, before any run)

Founder: "we want to optimize and test again the regimes for any results". Earlier today every regime USE failed (switch, filter,
trail, EMA/fade gates). This test lets the data pick the regime reading, the bucket, the action and the exit — but only the way a
live trader could: each year is traded with the rule chosen on the years before it. In-sample "optimised" numbers are printed for
contrast only and decide nothing.

## Samples
NQ 1-minute, roll windows out, days with a full RTH session. Sessions and decision times T (ET):
- New York: open 09:30, T ∈ 10:00 10:30 11:00 11:30 12:00 13:00, flat 15:55
- London: open 03:00, T ∈ 04:00 05:00 06:00 07:00, flat 08:00
- Asia: open 18:00, T ∈ 20:00 21:00 22:00 23:00, flat 02:30
Direction at T = sign(price at T − session open). Entry = the price at T.

## Regime readings at T (only data before T; 13)
acr(1m) over the last 30 / 60 / 120 min · efficiency ratio of 5-min closes over the last 30 / 60 min · session efficiency
|move| / path since the open · |move from open| / ATR · session range / ATR · share of minutes on the current VWAP side ·
order-flow delta / volume since the open signed with the direction (2023-03+) · gap / ATR signed with the direction ·
prior VIX close · overnight (18:00→09:30) range / ATR (NY only; elsewhere skipped).
Buckets = terciles; the cut points for year Y come from years Y−2 and Y−1 only.

## Actions and exits
FOLLOW (the direction) or FADE. Exits: time only at T + 30 / 60 / 120 min or the session flat; or the same with a stop
0.25 ATR (overnight 0.15 ATR) and a 2R target (stop first if both in one bar). Cost 1.0 NQ point per trade.
Rules = session × T × reading × bucket × action × exit, plus the unconditional rules (no reading) as the baseline.

## Walk-forward
Test years 2013 → 2024. For each session and year, pick the rule with the best t-stat (mean / sd × √n, n ≥ 100) over all earlier
years from 2011; trade it in the year. Secondary: the top-10 rules, equal weight.
## Controls (all must pass)
1. Beats the same walk-forward run over the unconditional rules only (does the regime add anything to "follow / fade at T")?
2. Beats ≥ 95 % of 200 runs with the readings shuffled across days within each year (regime ↔ outcome link broken).
3. ≥ +2 NQ pts / trade after cost, positive in 2013-18, 2019-22 and 2023-24.
Then the rule chosen on ≤ 2024 gets ONE look at 2025-01 → 2026-10-16 (must be positive, ≥ 30 trades).
Run outside edge_lab (scratch program), so not in its registry: 9,408 rules searched (NY 4,032 · London 2,688 · Asia 2,688) — count recorded here.

## Results (2026-10-17, 200 shuffles; program scratchpad regopt.cpp, ~4 s)
| Session | Walk-forward 2013-24, regime rules | No-regime baseline | Shuffle percentile | Eras 13-18 / 19-22 / 23-24 | Verdict |
|---|---|---|---|---|---|
| NY best rule | +2.63 (n 1181) | +1.60 | 80th | −0.66 / +10.37 / +3.96 | FAIL |
| NY top-10 | +3.34 (n 10045) | +1.59 | 100th | −0.55 / +8.82 / +7.56 | FAIL (2013-18 negative) |
| London best / top-10 | −0.50 / +0.12 | +0.70 / −0.23 | 10th / 20th | — | FAIL |
| Asia best / top-10 | −0.52 / −0.97 | −0.92 / −1.14 | 74th / 72nd | — | FAIL |

Reading: the only regime information that carries is New York "already trending by 10:00–11:00 in the gap's direction"
(gap agrees / 5-min efficiency high / session efficiency high / most minutes on one VWAP side / move ≥ high tercile), traded
FOLLOW to the close with a 0.25-ATR stop and 2R target. That is the cascade + gap retest already in the plan — the optimiser
rediscovers them, it finds nothing new, and it fails the same low-volatility era (2013-18) the cascade is weak in. Regime-picked
fades and every London / Asia rule fail. Holdout printed for the record (NY top-10 +3.53 n 934; London 06:00 VIX-high fade
+7.45 n 121): no PASS, so these were looked at and are no longer clean tests — forward paper only if ever pursued.
