# Pre-registration — entry timing on 1m / 5m / 15m bars for the cascade's direction (written 2026-10-19, BEFORE any run)

Founder: "try tick-1m-5m-15m as entries. we want to come up with a battle plan to win."
Direction = the 09:50 cascade check: D = sign(09:50 − open) when |09:50 − open| > 0.25 ATR. Base: enter at 09:50 (market).
Entries after 09:50, first one between 09:51 and 10:45, one a day, same exits (0.35 ATR stop, 2R, flat 15:55):
- `brk`: the first TF bar that closes beyond the previous TF bar's extreme in D (long: close > prior high).
- `pb`: the same, but only after a TF bar AGAINST D (prior bar closed against D) — a pullback then resume.
TF ∈ {1, 5, 15} → 6 specs + base. 2010-07 → 2024-12, all gates (controls 99 %); compare per trade and pts/session with the base.
Tick-level entries are not testable in edge_lab (1-minute bars only; MBO exists 2023-03+ only) — reported as a limit, not run.
A timeframe entry replaces the market entry only if it beats the base per session in BOTH 2010–18 and 2019–24 AND passes its gates.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (build-exp/edge_lab; per trade at 1 pt and pts/session, 2010–18 / 2019–24):
base (09:50 market) +0.81 / +10.09, +0.25 / +3.06 · brk 1m +0.13 / +8.76, +0.04 / +2.66 · pb 1m −0.30 / +8.35 · brk 5m −0.15 / +7.51 ·
pb 5m −0.06 / +7.53 · brk 15m +0.44 / +11.04, +0.10 / +2.57 · pb 15m +2.41 / +9.34 but only 106 / 74 trades, +0.12 / +0.47 per session.
All 7 FAIL at controls on their own (the 09:50-only cascade always did; the full cascade with re-checks is the version that passes).
**No timeframe entry beats the market entry per session in either era** — waiting for a confirming or pullback bar on 1m / 5m / 15m
gives up more of the move (and skips days) than it saves. Entry stays: market at the check time. Tick entries: not testable here.
