# Pre-registration — checks of the adopted VIX short rule (written 2026-10-18, BEFORE any run)

Founder: "do more tests". Adopted rule (2026-10-18_cascade_es_vix): cascade SHORTS after the prior VIX close fell (vixc1 < 0) get 2×.
Checks (description; they can only add caveats, not change the rule's adoption, which the NQ + ES tests decided):
1. Same split on the other NQ short entries: gap-retest shorts and 10:00 gap-rule shorts (vixc1 < 0 vs ≥ 0), 2010–18 / 2019–24 /
   2025–26 net/trade. Consistent = "after VIX fell" > "after VIX rose" in both of the first two periods.
2. The 5-day VIX change (`vixc5`) in place of the 1-day, on cascade shorts — same reading.
3. Book effect: the book (weakness long + cascade + retest, 0.35 ATR / 2R) vs the book with a second unit on cascade shorts after
   VIX fell: pts/session at 2 pt in the three periods, max drawdown 2019–24 (units counted).

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick` / `--trades`; net/trade, n; 2010–18 / 2019–24 / 2025–26).
1. Other NQ short entries: gap-retest shorts — VIX fell +0.57 (197) / +17.78 (147) / +59.02 (26) vs rose +0.39 (173) / +1.29 (127) /
   −18.95 (31): **consistent** in every period. 10:00 gap-rule shorts — fell +4.43 (47) / +24.92 (43) / +68.51 (13) vs rose +5.47 (59) /
   +20.24 (32) / −9.04 (10): mixed (2010–18 reversed, small n).
2. 5-day VIX change on cascade shorts — fell +0.12 (327) / +14.69 (235) / +23.76 (45) vs rose +0.97 (367) / +5.45 (259) / +38.08 (63):
   mixed — the 1-day change carries the effect, the 5-day does not.
3. Book: +0.41 / +9.23 / +21.35 pts/session, max DD 2019–24 1,481 → with a 2nd unit on the 589 cascade shorts after VIX fell:
   +0.51 / +11.01 / +30.13, max DD 2,005. Points +19 % (2019–24), drawdown +35 %: the rule raises the return but slightly lowers
   the return per point of drawdown (9.23 / 1,481 = 0.0062 → 11.01 / 2,005 = 0.0055).
**Reading:** the effect is real on the book's two short entries (cascade, retest) and on ES; doubling size is a choice of more points
for proportionally more drawdown, not a free improvement. On a prop account with a fixed trailing drawdown, prefer 1.5× (or keep 1×).
