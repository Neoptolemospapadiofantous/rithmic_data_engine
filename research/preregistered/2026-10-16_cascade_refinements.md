# Pre-registration — refinements of the cascade (written 2026-10-16, BEFORE any run)

Founder: "test all". Base = `so_cascade` (09:50 k1, 10:30 / 11:00 / 11:30 k2, stop 0.25-ish ATR, 1 trade, flat 15:55),
NQ 2010–26, 1.0 pt cost per contract (every added unit pays its own cost).

## Tests (controls at 99.3 % for the 6 edge_lab tests; 0.05 / 7 incl. the sizing test)
1. `cascade_reentry` — up to 2 trades a day: after the first exits, any later check (10:30, 11:00, 11:30, and the added
   12:00, 12:30, 13:00, all at k2) may open a second trade in the open's direction at that time. Same grid as the base.
   (Amended before the run: edge_lab cannot restrict the second trade to the added checks only.)
2. `cascade_pyramid` — add one unit when the trade reaches +1R; both units' stops then move to the entry; both exit
   together (stop / target / 15:55). rr ∈ {2, 3}, stop / thresholds as the base grid.
3. `cascade_trail` — no fixed target; once +1R is reached the stop trails the best price by t × the initial stop,
   t ∈ {0.5, 1.0}; base thresholds / stops.
4. `cascade_calm_sizing` (payout simulator, no parameters): the walk-forward cascade trades at 2 units on days the prior
   VIX close was < 20, 1 unit otherwise, vs a flat 1 unit — per era, max drawdown, Select Flex 50K payout speed.
5. `cascade_t0945`, `cascade_t0955`, `cascade_t1000` — the base with the first check at 09:45 / 09:55 / 10:00
   (sensitivity, not an optimisation: the base stays at 09:50 unless all three agree on a better time).
## Adoption (all must hold)
A refinement replaces the base only if it passes every edge_lab gate AND its frozen-setting holdout beats the base's in
BOTH splits (choose on 2010–18 → test 2019–22; on 2010–22 → 2023–26). Sizing: adopted only if it beats flat sizing in
all three eras on net per unit of risk AND does not increase max drawdown per unit by more than the gain.

## Results (appended after the runs — tests unchanged except the amendment above, made before running)
Base `so_cascade`: +4.80 pts/trade (2,322 walk-forward trades), controls direction 100 % / time 16 % / day 96 %.
| test | trades | net pts/trade | controls (dir / time / day) | verdict |
|---|---|---|---|---|
| cascade_reentry (≤ 2 trades/day) | 3,511 | +3.62 | 100 / 6 / 94 % | FAIL — more trades, each worse; total +12.7k vs +11.1k pts with more risk days |
| cascade_pyramid (add at +1R) | 2,355 | +4.62 | 99 / 4 / 76 % | FAIL — no better per trade; the random-day control collapses |
| cascade_trail (no target, trail after +1R) | 2,311 | +2.56 | — | FAIL — the 2R target beats trailing |
| cascade_t0945 / t0955 / t1000 | 2,456 / 2,351 / 2,465 | +3.57 / +4.42 / +3.98 | 100 / 22 / 96 % (09:55) | FAIL (controls at 99.3 %) — but all positive: the edge is not tied to one minute; 09:50 stays best |
| calm-day sizing (2 units on VIX < 20) | 2,322 | per unit: +1.02 / +7.42 / +10.29 by era vs flat +1.04 / +7.70 / +8.56 | — | FAIL — better only in 2023–26; return / drawdown 11.4 vs 9.0 comes from that era alone |
No refinement replaces the base cascade.
