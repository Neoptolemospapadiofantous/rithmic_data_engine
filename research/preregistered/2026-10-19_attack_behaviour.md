# Pre-registration — six attacks on NQ behaviour (written 2026-10-19, BEFORE any run)

Founder: "what can we attack from its behaviour" → "test all". Base for every add / change: the Vest plan (book F + gap-down weakness
long; eval first trade $120; funded $50; claim $500 keep $250; 10 slots refilled same day; scaled cost 2 and 6 pts; windows 2019–25 and
2010–18 — `research/programs/awk_20261019/port5.awk`). "Adopt" = mean net per year up in all four cells and losing windows not up in any.

1. **Overnight holds (IF Vest allows them — unverified).** From the session table (NY close ≈ 15:59, next Asia / London / NY):
   O1 long every day close → next 09:30 open; O2 long close → next 09:30 only after a weak close (IBS ≤ 0.3); O3 long close → next
   15:59 close after a weak close AND prior-day VIX ≥ 20 (the weakness book, one session). Stop 1.5 ATR (fills at the next open if it
   gaps through), sized to the stop. Per trade by era; then each as a funded add on Vest. Daily-loss check = the trade's own adverse
   excursion (conservative: Vest measures at 20:00 ET).
2. **Targets by predicted range** (walk-forward range model, research/programs/out/intraday_range_pred.csv, 2014+): book F trades
   re-simulated on 1m bars with the 0.35 ATR stop and target 1.5R on the predicted-quiet two fifths, 2R in the middle, 3R on the
   predicted-wild two fifths, vs 2R everywhere (same re-simulation). Adopt if net R/trade rises in 2014–18 AND 2019–24, then Vest.
3. **Evals only on predicted-wild days**: new evals are bought only on days in the top two predicted-range fifths (2014+ windows).
   vs buying every day, same windows.
4. **Fade the first break of yesterday's high / low** (open inside; the first 1-min bar that takes PDH / PDL, 09:35–15:00): trade back
   inside, stop k ATR beyond (k ∈ {0.15, 0.25}), target rr R (rr ∈ {1, 2}), flat 15:55. edge_lab gates, 2010-07 → 2024-12; holdout once.
5. **Mon / Tue long lean**: book F trades by weekday × direction; SKIP Mon/Tue shorts if < 0 in BOTH 2010–18 and 2019–24, then Vest.
6. **Anti-streak sizing**: 1.5× the book in the calendar month after a losing book month; pts/session and pts per point of max DD,
   2010–18 and 2019–24; adopt if both rise in both.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19. Founder relayed Vest's answer the same hour: overnight and weekend holds ARE allowed (NQ locked Fri 20:00 ET → Sun
evening, positions stay open but cannot be changed), and hourly FUNDING fees apply to perpetuals — tested at 0 and ≈ 6 NQ pts per hold
(scaled by ATR; an assumption, the real rate must be read from Vest).
1. Overnight (per trade at 2 pt, 2010–18 / 2019–24 / 2025–26): O1 every day close→open −0.41 / +4.09 / −5.07; O2 weak close→open +1.89 /
   +7.51 / −14.51; **O3 weak close + VIX ≥ 20, close→next close +5.01 / +43.57 / +69.06 pts (+0.029 / +0.116 / +0.051 R), ~15 a year.**
   Vest operation (mean net/yr, losing windows; 2 pts 19–25 / 10–18, 6 pts 19–25 / 10–18): FW $15,566 (5) / $10,788 (5) / $11,289 (15) /
   $7,480 (8) · +O1 $16,895 (7) / $10,039 (4) / $12,489 (10) / $6,861 (13) · +O2 $15,967 (5) / $10,768 (7) / $11,867 (10) / $7,676 (9) ·
   **+O3 $16,139 (5) / $11,201 (5) / $12,542 (7) / $7,718 (7) → ADOPTED**; with ≈ 6 pts funding per hold $15,972 / $11,156 / $12,479 / $7,706 —
   still up everywhere. O1 / O2 fail (losing windows up).
2. Targets by predicted range (book trades re-simulated on 1m; 2R everywhere reproduces +0.090 / +0.198 / +0.086 R): by range +0.104 /
   +0.193 / +0.055 R → **FAIL** (2019–24 lower).
3. Evals only on predicted-wild days (windows 2014–25, plan incl. O3): every day $13,494/yr (10/141 losing), wild only $12,685 (10/141) at
   2 pts; 6 pts $9,729 (12) vs $8,847 (25) → **FAIL**.
4. Fade the first break of yesterday's high / low: **FAIL at SCREEN** — all 4 variants lose, −2.62 pts/trade walk-forward, every era
   negative, worse than random. The 83 % "back within 30 min" is noise around the level, not a tradeable reversal.
5. Mon/Tue shorts: −0.0016 (443) / −2.02 (333) / +3.91 (66) pts/trade → skip rule met by the letter; Vest operation without them $15,341 /
   $11,065 / $11,538 / $7,540 vs $16,139 / $11,201 / $12,542 / $7,718 → **not adopted** (operation lower).
6. Anti-streak (1.5× in the month after a losing book month): pts/session +0.15 / +15.05 / +13.86 → +0.50 / +17.69 / +19.84, pts/DD
   2019–24 0.0121 → 0.0139, 2010–18 max DD 1,066 → 1,043 → its own rule MET; Vest operation (with O3) $17,653 (7) / $15,954 (0) / $13,888
   (11) / $10,868 (1) vs $16,139 (5) / $11,201 (5) / $12,542 (7) / $7,718 (7) — more money in all four but losing windows UP in 2019–25 →
   **not adopted under the operation rule.** Monthly autocorrelation is −0.03 since 2019, so most of the gain is simply more size. Founder's
   call: +$1.3–4.8k/yr for 2–4 more losing 12-month windows in 2019–25.
