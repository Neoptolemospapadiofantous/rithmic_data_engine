# Pre-registration — BOS entries 09:30–11:30, 2–3 trades a day (written 2026-10-18, BEFORE any run)

Founder: "what about entering on bos on 9:30 - 11:30 2-3 trades max". Earlier: swing BOS / CHoCH entries FAIL (2026-10-16/18),
BOS + EMA pullback FAIL (2026-10-18 bos_ema). New here: the morning window, several trades a day, and today's direction / path filter.

BOS (5-minute bars, edge_lab swh / swl, confirmed swings with n bars each side, the day since 18:00): LONG on the first 5-min close
above the last confirmed swing high (cl(5,1) > swh(5,n) and the bar before closed at or below it); SHORT mirrored. Scan 5-min
closes 09:35–11:30, one position at a time, max m trades a day, flat 15:55, target 2R.
Grid: n ∈ {2, 3} × filter f ∈ {0 any BOS, 1 BOS on the side of the 09:30 open, 2 side of the open AND path ≥ 0.55} × stop ∈ {0.25,
0.35} ATR × m ∈ {2, 3} → 24 variants. Discovery 2010–18 (screen): pick = highest pts/session at 2 pt with ≥ 100 trades AND > 0 in
both halves (2010–14 / 2015–18). Confirmation 2019–24: full gates (`control_pct` 0.95, 400 seeds) AND pts/session at 2 pt ≥ the book's
(+9.2 in 2019–24) — the BOS strategy only matters if it does at least as well as what is already traded. 2025–26 reported.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick` screens; pts/session at 2 pt; 2010–18 2,057 sessions).
Discovery 2010–18: **all 24 variants < 0**, in both halves. Best: direction + straight path (f 2), n 2, 0.35 ATR −0.56 (max 2/day,
1,680 trades, win 43 %) / −0.57 (max 3); any BOS (f 0) −1.5 … −2.0; side of the open only (f 1) −1.6 … −2.1. → **No pick; FAIL.**
Description only (not selectable): 2019–24 best +1.50 pts/session (any BOS, n 2, 0.35 ATR, max 3; 2025–26 +11.32), most cells −0.2
… +1.4 — against +9.2 for the book. Morning BOS entries, filtered or not, 2 or 3 a day, do not earn what the one morning trend trade
does; the direction / path filter makes them lose less, not win.
