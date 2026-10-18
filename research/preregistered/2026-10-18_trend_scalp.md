# Pre-registration — the daily trend scalp: 1–3 trades, 10–30 points each, from path and direction (written 2026-10-18, BEFORE any run)

Founder: "we want 1-3 trades 10-30 points each everyday. with path and direction. trend". Fixed-point targets, so NQ 2019 → on
(10–30 points meant little at NQ 2,000–7,000).

Rule `ts` (5-minute closes 10:00–13:55, one position at a time, max 3 trades a day, 1 NQ, flat 15:55, real cost 1.0 pt in edge_lab;
pts/session also reported at the measured 2.0 pt):
- DIRECTION D = sign(px − 09:30 open). PATH: |px − open| / max(hod − lod, 1) ≥ 0.55 (straight), |px − open| ≥ 0.15 ATR (a move),
  price on D's side of VWAP. All three must hold at the decision bar (trend still on).
- ENTRY e: 0 = any 5-minute close while the trend holds (continuation) · 1 = VWAP pullback: the last 5-min bar came within
  0.05 ATR of VWAP and closed back in D · 2 = EMA pullback: the last 5-min bar touched the 5-min EMA(20) and closed back in D.
- EXIT: stop S ∈ {10, 20, 30} points, target T ∈ {10, 20, 30} points. 27 variants.
Discovery 2019-01 → 2022-12 (screen; per variant trades, win rate, net/trade, sessions with a trade, pts/session at 2 pt).
Selection (fixed now): among variants that trade on ≥ 50 % of sessions, the highest 2019–22 pts/session at 2 pt, provided it is > 0
in both 2019–20 and 2021–22. Confirmation 2023-01 → 2026-10: the one fixed variant through the full edge_lab gates
(`control_pct` 0.95, 400 seeds, `min_trades` 100) AND pts/session at 2 pt > 0 in 2023–24 and in 2025–26. (2025–26 was looked at
for other rules today — stated.) Per-day statistics for the pick via `--trades`.

## Amendment (written after the `ts` discovery run, BEFORE any momentum run) — founder: "momentum"
Rule `tm`: identical to `ts` except DIRECTION = the 1-minute momentum side, re-read at every decision bar: the last 45 one-minute
moves with efficiency ≥ 0.3 (er(1,45)), D = sign of that 45-minute move, price on D's side of VWAP and of the 09:30 open (the
confirmed 10:00 momentum rule's definition, now at every 5-minute close 10:00–13:55). The intraday PATH condition (≥ 0.55 since
09:30, ≥ 0.15 ATR move) is kept as a second variant axis m ∈ {0 = momentum only, 1 = momentum + path}. Same entries e, stops S,
targets T → 54 variants; same discovery period, selection rule and confirmation as `ts`, applied to `tm` separately.

## Amendment 2 (written after the `ts` confirmation failed, BEFORE this run)
Rule `tb`: the CONFIRMED every-day entries instead of a new trend signal — cascade (09:50 / 10:30 / 11:00 / 11:30), the 10:00 gap and
momentum rules on a straight path, the gap retest (priority as in 2026-10-18_daily_book, no weakness long: it has no stop) — but
with the founder's fixed exits: stop S ∈ {10, 20, 30} points, target T ∈ {10, 20, 30} points, up to 3 trades a day, one position
at a time. 9 variants; same discovery (2019–22), selection and confirmation (2023–26) rules as `ts`.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab; sessions 2019–22 983 = 490 + 493; pts/session at 2.0 pt = trades × (net/trade − 1) / sessions).

### `ts` (direction = side of the open + straight path), discovery 2019–22
Best: S 30 / T 30 / VWAP pullback +0.95 pts/session (905 trades, win 55 %, 2019–20 +0.30, 2021–22 +1.59, trades on 56 % of
sessions) → the pick. All 10- and 20-point targets ≤ 0 (e.g. S 30 / T 10 wins 77 % of trades and loses −0.84 to −2.55/session;
S 10 / T 10 −2.07 to −5.29). **Confirmation 2023-01 → 2026-10 (`ts_vwap30_confirm`): FAIL at CONTROLS** — walk-forward +1.02/trade
(336, t 0.62), random direction beaten in 90 %, random time 84 % (need 95 %); 2023–24 −1.44 pts/session, 2025–26 +0.48.

### `tm` (direction = 1-minute momentum, ± path) — no pick
Variants trading on ≥ 50 % of sessions are only the `e = 0` (any close) ones; the best, S 30 / T 30 / momentum + path, is +0.74
pts/session but 2019–20 −0.71 → fails the both-halves condition. The pullback forms trade on 0.0–0.2 trades/session.

### `tb` (the confirmed every-day entries with fixed 10–30 point exits) — no pick
Every variant < 0 pts/session at 2 pt: S 30 / T 30 −0.47 (53 % win), S 30 / T 10 −1.32 (78 % win), S 10 / T 10 −2.95.

**Verdict: no 10–30-point-per-trade daily rule makes money at real cost** — not on a new trend signal, not on 1-minute momentum,
and not on the confirmed entries themselves. The same cascade / gap / momentum entries that earn +9 pts/session with ATR-sized
exits (2R of 0.35 ATR ≈ 150–250 pts in 2023–26) lose with 10–30-point targets: the edge is the size of the day's move, and a
fixed small target caps the winners while the cost (2 pt) and the noise eat the rest.
