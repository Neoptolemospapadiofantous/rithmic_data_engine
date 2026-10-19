# Pre-registration — a prop-legal gap-down weakness long (written 2026-10-19, BEFORE any run)

DESK open lead (2). The gap-down weakness long (gap ≤ −0.1 ATR, prior IBS ≤ 0.5, VIX ≥ 20; buy 09:30, flat 15:55; the
combine-many pick v 20 / x 0.5) has NO stop, which the Vest $150 daily limit forbids. Test stops k ∈ {0.5, 0.75, 1.0, 1.5} ATR
(no target). Per trade at 2 pt: 2010–18 / 2019–24 / 2025–26 vs no stop. Pick = the stop with the highest 2010–18 net/trade that is
> 0 (ties → wider). Then on Vest $5k (lenient daily-loss model, each trade sized to its OWN stop): book F at $40 per trade alone vs
F + W at $40 per trade; adopt for Vest only if the pass rate is not lower AND pts/session (1 NQ) higher in 2010–18 and 2019–24.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (build-exp/edge_lab --quick --trades; 2 pt; v 20, x 0.5; 55 / 86 / 12 trades).
Per trade 2010–18 / 2019–24 / 2025–26: no stop +23.66 / +32.33 / +77.54 · 0.5 ATR +18.65 / +24.75 / −91.14 · 0.75 +17.30 / +25.26 /
+83.59 · 1.0 +19.81 / +17.84 / +92.74 · **1.5 ATR +23.29 / +24.44 / +77.52 → pick 1.5** (keeps nearly all of the no-stop edge).
Book (1 NQ, 2 pt): F +0.15 / +15.05 / +13.86 → F + W(1.5) +0.77 / +16.48 / +16.82 pts/session.
Vest $5k, lenient model, each trade sized to its own stop, 2019–26 starts: $40 F 74.6 % (30 days, $54/pass) → F+W 75.3 % (31, $53);
$50 F 66.0 % (20, $61) → F+W 66.6 % (20, $60).
**ADOPTED for Vest by the rule** (pass rate not lower, pts/session higher in both eras): the gap-down weakness long with a 1.5 ATR stop
is prop-legal at Vest size. Caveats: 12.6 trades/yr (small n, 1 pick of 4 stops); at $40 per trade the position is a fraction of one
MNQ (Vest perps allow fractional size; a CME micro account could not size this).

## Follow-ups (description, 2026-10-19)
- DESK lead (5), exits on the book's own cascade (edge_lab exits on the rebuilt cascade, 1,738 walk-forward trades): base +5.55 pts/trade,
  max DD 1,216; vwap +5.04 (DD 1,303); open +5.80 (1,145); vwap + stall +5.24 (1,141); runners and trails lower — **no exit rule
  passes**. The VWAP exit's 2026-10-16 PASS was on `cascade_gap_agree` (gap-agreeing days only), not on the book's cascade.
- Asia open drive (20:20 ET, k 0.1, stop 0.2 ATR, 2R) as a third Vest piece: +0.30 / +0.14 / +6.26 pts/trade; Vest $40 75.3 → 75.2 %,
  $50 66.6 → 57.4 % → **not for Vest** (≈ 0 points, more chances to hit the daily limit).
