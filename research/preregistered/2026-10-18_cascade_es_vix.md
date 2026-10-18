# Pre-registration — ES confirmation and the VIX change on the cascade (written 2026-10-18, BEFORE any run)

Founder: "do more tests". Base = the book's cascade (0.35 ATR, 2R; +0.80 / +8.37 / +13.71 per trade in 2010–18 / 2019–24 / 2025–26;
longs +0.79 / +8.03, shorts +0.57 / +9.85). Cells restrict the base (a filtered check lets the next check fire).
1. **ES confirmation** at the check bar (edge_lab `xs` = ES's move from its own 09:30 open in ES ATR, `pair: ES`; D = cascade side):
   ES strong (D·xs ≥ 0.25) · ES weak (0 < D·xs < 0.25) · ES against (D·xs ≤ 0). 3 cells.
2. **VIX change** of the prior close (`vixc1`, fraction): longs after VIX fell (vixc1 < 0) · longs after VIX rose · shorts after VIX
   fell · shorts after VIX rose. 4 cells.
Rule (as before): SIZE-UP if net/trade ≥ 1.25 × the (side) base in BOTH 2010–18 and 2019–24; SKIP if < 0 in BOTH; else nothing;
2025–26 reported once. Anything passing then needs the ES-replication check before use.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick`; net/trade, n; `pair: ES` — base unchanged by it).
| cell | 2010–18 | 2019–24 | 2025–26 | rule |
|---|---|---|---|---|
| base | +0.80 (1,378) | +8.37 (989) | +13.71 (213) | |
| ES strong (≥ 0.25 ATR same way) | +1.26 (1,063) | +8.33 (781) | +20.70 (180) | no |
| ES weak | −0.57 (562) | +11.64 (406) | +9.28 (73) | no |
| ES against | −1.85 (123) | +8.82 (85) | −25.24 (5) | no |
| longs after VIX fell | +1.24 (364) | +9.09 (286) | −17.46 (49) | no (vs longs ≥ 0.99 / ≥ 10.04) |
| longs after VIX rose | +0.34 (365) | +6.81 (249) | +0.77 (65) | no |
| **shorts after VIX fell** | **+1.58 (327)** | **+12.81 (260)** | +50.22 (56) | **SIZE-UP** (vs shorts ≥ 0.71 / ≥ 12.31) |
| shorts after VIX rose | −0.33 (367) | +6.56 (234) | +12.61 (52) | no |
**ES replication** of the shorts split (ES cascade, 0.35 pt cost): all shorts −0.26 (673) / +0.95 (452) / +6.50 (95); after VIX fell
+0.09 (328) / +2.78 (229) / +12.10 (48); after VIX rose −0.58 (345) / −0.93 (223) / +0.78 (47) → **same sign in every period**.
**Verdict: ADOPTED as a size rule** — cascade SHORTS after the prior close's VIX fell (vixc1 < 0) get the larger size; shorts after a
VIX rise stay at base (they did not fail the skip test: +6.56 on NQ 2019–24). It is 1 of 7 cells, which is why the ES check was
required; it passed in every period on both indices. Not explained here (a guess: a short after a calm day catches complacency).
