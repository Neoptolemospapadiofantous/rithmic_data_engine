# Pre-registration — checks on the 10:00 gap rule: legs, VIX, ES agreement, diversification (written 2026-10-18, BEFORE any run)

Founder: "more tests". Base = the confirmed GAP rule (pdm_gap_confirm). NQ 2010-07 → 2024-12 + the 2025+ holdout as before.

1. **Long / short legs** (the drift rule, never checked for this rule): screen each side alone in 2010–18, 2019–24, 2025–26.
   PASS = both sides net > 0 in 2010–18 AND 2019–24. A failing short side means the rule is partly NQ's up-drift.
2. **VIX level** (prior VIX close; the conditioner of the weakness bet): split VIX < 15 / 15–20 / ≥ 20. Discovery 2010–18
   screen; a size rule only if the top bucket nets ≥ 1.25 × the rest in 2010–18 AND 2019–24 (no separate confirmation run —
   both periods must agree, and 2025–26 is reported once).
3. **ES agreement**: ES on the gap's side of its own 09:30 open at 10:00 (`xs * gap side > 0`) vs not. Same rule as 2: a size
   rule only if agree ≥ 1.25 × disagree in 2010–18 AND 2019–24.
4. **Diversification** (description): monthly correlation of the GAP rule with `weak_day_long` over their shared walk-forward
   months, and the share of months where both lose.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick` screens; `pair: ES` for xs). Net pts/trade (n).

| split | 2010–18 | 2019–24 | 2025–26 | verdict |
|---|---|---|---|---|
| long | +1.51 (173) | +22.24 (139) | +4.38 (27) | |
| short | +5.01 (106) | +22.93 (75) | +34.79 (23) | **legs PASS** — both sides pay, shorts no worse: not NQ's up-drift |
| VIX < 15 | +1.24 (140) | +22.13 (50) | +38.85 (5) | |
| VIX 15–20 | +8.45 (82) | +17.54 (76) | −0.52 (33) | |
| VIX ≥ 20 | −1.31 (57) | +26.94 (88) | +61.78 (12) | **no size rule** (top bucket worst in 2010–18) |
| ES agrees | +2.70 (263) | +21.96 (200) | +19.69 (49) | |
| ES does not | +5.17 (16) | +29.95 (14) | −46.32 (1) | **no rule** — ES agrees on 94 % of days (redundant) |

Diversification (walk-forward months 2011-07 → 2026, full-history GAP run vs `weak_day_long`): 176 shared months, monthly
correlation **0.19**; GAP losing months 51, weakness 49, both losing 18 (10 %). The gap rule and the weakness bet are close to
independent — they can share a book (unlike the cascade and weak-day, 0.4–0.7 in the 2026-10-16 sweep).
