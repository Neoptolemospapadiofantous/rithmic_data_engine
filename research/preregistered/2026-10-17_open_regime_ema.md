# Opening regime (first 5 / 15 / 30 min) → EMA-14 trades for the rest of the day (pre-registered 2026-10-17, before any run)

Founder: "so we set a regime 5-15m-30m first at market open then trade after based on regime and ema strategy?"
Regime readings at 09:35 / 09:45 / 10:00, from 1-minute bars since 09:30 (only data before the reading time):
- ER efficiency = |close − open| / sum of |1-min moves|   - MOVE |close − open| / ATR   - RANGE (high − low) / ATR
- ACR lag-1 autocorrelation of 1-min moves (15 and 30 min only)
Each reading in terciles low / mid / high; cut points for year Y from years Y−2 and Y−1 only.
Direction: (a) OPEN — the sign of the opening move at the reading time; (b) PLAN — the plan's direction (first cascade / gap-retest trigger).
The EMA trade after the reading time: 5-min bar touches EMA-14 and closes back in the direction, on that side of VWAP and the open,
≤ 13:55, max 3, done after a loss, stop 0.25 ATR, break-even at +1R, no target, flat 15:55 (the least-bad handling, 2026-10-17), cost 1.6.
Questions: does any opening-regime bucket make the EMA trade positive in all three eras (2010-18 / 2019-22 / 2023-24)?
Walk-forward: each year trades only the buckets whose total R over 2010..year-1 is positive; compared with trading every day.
2025-26 one look for the walk-forward's final choice.

## Results (2026-10-17; scratchpad openreg.cpp). Buckets need two earlier years for cut points → scored 2012-24 (eras 12-18 / 19-22 / 23-24).
- Every day, no opening regime: OPEN direction −0.016 / −0.052 / −0.043 R (after 5 / 15 / 30 min); PLAN direction −0.03 R.
- Of 66 regime cells (2 directions × 11 readings × 3 terciles) only 3 are positive in all three eras — FEWER than the ~8 chance alone
  gives (1 in 8 per cell), so the table as a whole shows no opening-regime effect: trending, choppy or mean-reverting first 5 / 15 / 30
  minutes do not tell the EMA trade when to work.
- The one stand-out: PLAN direction on days whose first 15 minutes are NARROW (range low tercile) +0.116 R (+0.03 / +0.18 / +0.24).
  Walk-forward picked exactly that cell from 2015 on: +0.130 R / +7.3 pts per trade (n 587, 8 of 11 years positive; trading every
  day +0.015 R). HOLDOUT 2025-26 (one look): −0.305 R / −22.3 pts (n 37) — failed. Verdict: FAIL; a forward-paper candidate at most
  ("quiet first 15 minutes, then the plan direction"), not a rule.
