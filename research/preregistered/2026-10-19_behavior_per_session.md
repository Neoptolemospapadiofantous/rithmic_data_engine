# Behaviour per session — Asia / London / NY measured the same way (written 2026-10-19, BEFORE any run; description, no rules)

Founder: "do everything per session". Program `research/programs/behavior_sessions.cpp`. Sessions in ET: Asia 18:00 → 03:00,
London 03:00 → 09:30, NY 09:30 → 16:00. Each session: its own anchored VWAP (typical price × volume from its open) and its own ATR
(mean range of its previous 14 sessions); eras 2010–18 / 2019–22 / 2023–26.
1. VWAP: 5-min closes crossing the session VWAP per session; share with no cross after the first hour; at 1 h and at mid-session, by
   distance from VWAP (session ATR): touches VWAP again, closes on the same side, move to the close away from VWAP.
2. Straightness at 1 h (|px − open| / range so far; straight ≥ 0.55 and moved ≥ 0.25 session-ATR / middle / choppy < 0.30) → trend
   session (|close − open| ≥ 0.5 × range) and closes on the first hour's side.
3. Trend sessions: deepest pullback after the first hour against the session's direction (session ATR).
4. Prior session's high / low (Asia ← prior NY, London ← Asia, NY ← London) taken from inside: back through within 30 min (sweep) vs
   hold; then closes beyond.
5. Round numbers (x00 vs x50 vs x25): first touch, through vs rejected by 0.15 session-ATR within 30 min.
6. Second half after a trend first half (half-session straight ≥ 0.55 and ≥ 0.4 session-ATR): move in that direction to the close;
   share giving back ≥ half.
7. Early volume: first-15-min volume ÷ mean of the previous 20 same sessions; trend-session rate by fifth.
8. Order flow 2023-03+: first-30-min aggressor delta (share of volume) vs the rest of the session — corr, same direction, and
   "first-30 move ≥ 0.1 ATR with delta agreeing vs disagreeing".

## Results (2026-10-19, research/programs/out/behavior_sessions.txt) — Asia / London / NY
| Behaviour | Asia | London | NY |
| --- | --- | --- | --- |
| VWAP crosses per session (5-min closes) | 9.0 | 8.2 | 6.4 |
| One side of VWAP after the first hour | 6.5 % | 8.1 % | 20.4 % |
| ≥ 0.25 ATR from VWAP at mid-session: touches it again / closes that side | 48 % / 79 % | 68 % / 67 % | 41 % / 81 % |
| Move to the close away from VWAP (any distance) | −0.04 … +0.02 | −0.02 … +0.02 | 0.00 … +0.03 ATR |
| Straight + moved at 1 h → trend session / closes that side | 52 % / 79 % | 48 % / 77 % | **60 % / 84 %** |
| Choppy at 1 h → trend session / closes that side | 41 % / 52 % | 40 % / 51 % | 37 % / 57 % |
| Trend sessions: deepest pullback after the first hour | 0.52 ATR | 0.52 ATR | 0.48 ATR |
| Prior session's high/low taken: back within 30 min / then closes beyond | 73 % / 46 % | 80 % / 46 % | 84 % / 46 % |
| ... a 30-min hold closes beyond | 68 % | 65 % | 72 % |
| Round numbers x00 vs x50 broke through | 52.9 vs 48.7 % (eras mixed) | 51.7 vs 49.1 % (mixed) | 50.5 vs 48.4 % |
| Second half after a trend first half: move / gives back ≥ half | +0.004 / 17 % | −0.020 / **23 %** | +0.018 / 14 % |
| Early volume (RVOL) fifths → trend rate | 42–47 %, flat | 42–44 %, flat | 46–50 %, flat |
| Order flow 2023+: first-30 move with delta agreeing / disagreeing → rest | +0.065 / +0.024 | **−0.067 / −0.073** | +0.060 / −0.079 |
Reading: the same mechanics everywhere (VWAP neither magnet nor engine, ~0.5 ATR pullbacks inside trend sessions, most level breaks
come back, round numbers / RVOL nothing) — but NY is the only session with real follow-through: twice as many one-sided sessions, the
best straight-first-hour odds, and the smallest give-back. London's first move fades even when order flow agrees (−0.07 ATR either way)
and gives back the most; Asia crosses VWAP the most (9 a session) — the choppiest. This is why every overnight direction rule tested as a
coin flip and the plan trades NY plus the one overnight HOLD (which is a daily weakness bet, not a session-direction bet).
