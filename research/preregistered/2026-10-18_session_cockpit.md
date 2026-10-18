# Pre-registration — session cockpit: ORB 5/15 → direction → trend or mean reversion, NY / Globex / London (written 2026-10-18, BEFORE any run)

Founder: "a full daily strategy for NY open, Globex open and London … check ORB first 5m then 15m, then wait for a direction
and place a trade, either trend momentum or mean reversion … can we test everything?"

## Already answered — NOT re-run (verdicts from the ledger)
| question | idea(s) | verdict |
|---|---|---|
| NY ORB 5 / 15 break as an entry | orb, orb5, orb15, orb15_break_price, + order-flow versions | FAIL at controls (+1.5 / +2.5 pts; random time does as well) |
| NY failed ORB-15 break → fade | orb15_sweep_price (2010–26) / orb15_sweep_of | FAIL at screen (−2.47) / too few trades |
| NY mean reversion with no direction | nd_range, nd_vwap, nd_open, all_* | FAIL at screen, every era negative |
| Globex-open drive / cascade | cascade_asia_2h / _hold, asia_open_drive | gates passed for _2h but holdout and random-time fail; others FAIL |
| London open drive / cascade / boxes | london_open_drive, cascade_london_*, ldnbox_* | FAIL |
| Asia-range sweep fade | asiabox_sweep_price / _of | FAIL (price −0.52; flow +10.4 fails direction control at 99.75 %) |

## New tests (NQ, 2010-07 → 2024-12; ES = confirm; real cost 1.0 pt; holdout 2025-01 → 2026-10-16 judged ONCE, frozen setting)
Family of 13 tests → controls at 99.6 % with 400 seeds (0.05 / 13 ≈ 0.4 %). Grids ≤ 8 variants, round numbers chosen now.

A. ORB as a FILTER of the NY cascade (base `so_cascade` grid unchanged; each run beside its complement)
1. `cascade_orb15_narrow` — the 15-minute opening range ≤ 0.35 ATR (narrow open = more of the day ahead).
2. `cascade_orb15_beyond` — at the check, price is already outside the 15-minute range on the cascade's side.
3. `cascade_orb5_beyond` — the same with the 5-minute range.

B. Overnight sessions as a FILTER of the NY cascade (+ complements)
4. `cascade_ldn_agree` — London's move (03:00 open → 08:25) points the cascade's way.
5. `cascade_gx_agree` — the Globex session's move (18:00 open → 09:29) points the cascade's way.

C. Globex open (18:00 ET), flat 20:00 (the window where the only gate-passing overnight idea lived)
6. `gx_orb5_break` — first 1-minute close beyond the 18:00–18:05 range, 18:05–19:30, follow. stopk {0.1, 0.15}, rr {1.5, 2}.
7. `gx_orb15_break` — the same with 18:00–18:15, from 18:15.
8. `gx_orb15_sweep` — a 5-minute bar pokes beyond the 18:00–18:15 range and closes back inside → fade, 18:20–19:30.

D. London open (03:00 ET), flat 08:25 (before US data)
9. `ldn_orb5_break` — 03:00–03:05 range, 03:05–06:00, follow. stopk {0.1, 0.15}, rr {1.5, 2}.
10. `ldn_orb15_break` — 03:00–03:15, from 03:15.
11. `ldn_orb15_sweep` — fade a failed break of the 03:00–03:15 range, 03:20–06:00.

E. NY failed ORB-5 break (only ORB-15 was tested)
12. `orb5_sweep_price` — a 5-minute bar pokes beyond the 09:30–09:35 range and closes back inside → fade, 09:40–12:00.

F. Forward check (not a gate, no choosing)
13. `asiabox_sweep_of` frozen at its best development setting on the collector's own sessions 2026-09-18 → 2026-10-16.

## Adoption
- A standalone idea (6–12) is a forward-paper candidate only if it passes every edge_lab gate at 99.6 % AND its frozen
  2025+ holdout is positive.
- A cascade filter (1–5) is adopted only if it passes every gate AND beats its complement on walk-forward net per trade in
  ≥ 2 of 3 eras incl. 2023–24 AND its frozen 2025+ holdout beats the frozen base cascade's.
- Anything else is reported as a failure, or as cockpit CONTEXT (shown, never a signal).

## Results (appended 2026-10-18 after the runs — tests unchanged)
Base for comparison, `so_cascade` on the same 2010-07 → 2024-12 window: walk-forward +4.27 pts/trade (2,113 trades); frozen
setting (k1 0.2, k2 0.25, 3R, stop 0.25 ATR — also every filter's best) on the 2025+ holdout: **+12.16 pts/trade, 154 trades, t 1.54**.

| # | test | walk-forward net/trade (trades) | verdict | 2025+ holdout, frozen (vs its complement) |
|---|---|---|---|---|
| 1 | cascade_orb15_narrow | +4.79 (1,097) vs compl +4.17 | FAIL controls (time 14 % / day 85 %) | +11.03 vs +13.40 — worse than base |
| 2 | cascade_orb15_beyond | +5.33 (1,959) vs compl +2.39 (514) | **PASS all gates** (day 99.75 %, 7/7 robust) | +9.24 vs compl +28.20 — **filter loses to its complement and to the base → not adopted** |
| 3 | cascade_orb5_beyond | +5.04 (2,135) vs compl −10.95 (79) | FAIL controls (day 97.75 %) | +13.56 (t 1.67) — ≈ the base (it keeps 97 % of its trades) |
| 4 | cascade_ldn_agree | +5.39 (1,114) vs compl +4.74 | FAIL controls (day 95.5 %) | +11.36 vs +9.87 — below base |
| 5 | cascade_gx_agree | +6.88 (1,182) vs compl +2.44 | FAIL controls by one seed (day 99.5 % vs 99.6 %), 7/7 robust | +10.60 vs compl +10.69 — no difference |
| 6 | gx_orb5_break | −1.46 (348) | FAIL screen | — |
| 7 | gx_orb15_break | −1.38 (194) | FAIL screen | — |
| 8 | gx_orb15_sweep (fade) | −0.95 (639) | FAIL screen | — |
| 9 | ldn_orb5_break | −0.71 (554) | FAIL screen | — |
| 10 | ldn_orb15_break | −0.91 (892) | FAIL screen | — |
| 11 | ldn_orb15_sweep (fade) | −0.99 (1,112) | FAIL walk-forward | — |
| 12 | orb5_sweep_price (NY fade) | −1.42 (1,006) | FAIL screen | — |
| 13 | asiabox_sweep_of forward 2026-09-18 → 10-16 | 0 trades | not checkable: the `flow` flag is set only for the archived order-book months (2023-03 → 2026-03) | — |

Verdict:
- **Globex open and London: no ORB edge, follow or fade.** With every earlier overnight test, these sessions have nothing tradeable.
- **NY ORB: context, not a signal or filter.** In 2010–24 the cascade did better when price was already beyond the 15-minute
  range (and when the Globex move agreed), but on the 2025+ holdout the trades those filters REMOVE did as well or better —
  the filters cut trades without adding edge. The plain cascade (+12.16, 2025+) stays the rule.
- **Mean reversion off an ORB: fails in all three sessions** (NY 5 and 15, Globex 15, London 15).
- Cockpit: show ORB-5 / ORB-15 levels, London and Globex direction as CONTEXT; the only NY signals stay the cascade, the gap
  retest and the momentum VWAP pullback; the only mean-reversion trade stays the weak-day long at the open.
