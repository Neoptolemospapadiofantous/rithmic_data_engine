# Momentum + EMA, optimised the honest way (pre-registered 2026-10-17, before any run)

Founder: "optimize the momentum ema strategy more". Base = mom_dir_30_ema20 (NQ New York: last W one-minute moves with
efficiency ≥ Q, price on that side of VWAP and the 09:30 open; entry at a 5-min close within TOL × ATR of the 5-min EMA-L,
closing back; stop S × ATR; break-even at +B R, hold to 15:55; ≤ 3 a day; done after a loss; cost 1.6 pts).
Grid: W 15/20/30/45/60 min · Q 0.2/0.25/0.3/0.35/0.4 · L 9/14/20/30/50 · TOL 0/0.03/0.06 ATR · S 0.15/0.2/0.25/0.35 ATR ·
B 0.75/1/1.5R/none · last entry 11:30 or 13:55 → 5×5×5×3×4×4×2 = 12,000 settings, NQ 1-min 2010-07 → 2024-12.
Honest number = WALK-FORWARD: each year 2013-24 trades the setting with the best total R over 2010..year-1 (≥ 100 trades).
Also reported: (a) the frozen base on the same years; (b) PLATEAU — the in-sample best setting and the average of its 2-step
neighbourhood (one step on any one parameter); a lone spike = overfit; (c) ES with the walk-forward's final setting (cost 0.65);
(d) 2025-26 for the final setting — NOT clean (2025-26 was already looked at for this family), printed for the record only.
Adopt a change on paper only if the walk-forward beats the frozen base AND the final setting's neighbourhood is also better
than the base AND ES is not worse than the base on ES.

## Results (2026-10-17; research/programs/momopt.cpp → out/momopt.txt, 12,000 settings in 7 s)
- BASE (W30 Q0.30 EMA-20 tol 0.03 stop 0.25 BE 1R ≤ 13:55): +0.079 R/trade 2010-24 (+0.066 / +0.076 / +0.136), n 964.
- WALK-FORWARD: +0.168 R / +5.25 pts per trade, 2013-24 (n 683, +115 R total) vs the base on the same years +0.079 R (+64 R).
  From 2015 it picked the same setting every year: W45 Q0.30 EMA-9 tol 0.06 stop 0.15 ATR, NO break-even, last entry 11:30;
  12 years: 7 positive, 5 negative (2013 −0.68 on 36 trades, 2016 −0.25; 2014 / 2020 / 2023 about 0).
- PLATEAU: in-sample best (W45 Q0.25 EMA-9 tol 0.06 stop 0.15 no BE ≤ 11:30) +0.149 R (+0.087 / +0.225 / +0.213, n 1,465);
  its one-step neighbourhood +0.072 — no better than the base itself (+0.079); the base's neighbourhood +0.045. Averaged over all
  8,144 settings with ≥ 300 trades every parameter value is negative (−0.02 … −0.09): the edge lives in a small region.
- ES: final setting −0.042 R (−0.171 / +0.215 / −0.137), base −0.063 — neither works on ES.
- 2025-26 (not clean): final +0.554 (n 127), base +0.223 (n 74).
VERDICT (pre-registered rule): walk-forward beats the base ✔, ES not worse ✔, neighbourhood better than the base ✘ (+0.072 <
+0.079) → NOT adopted as a replacement. What the data say when optimising: a longer momentum window (45 min), a fast EMA-9 with
a looser touch (0.06 ATR), a tighter stop (0.15 ATR) held with no break-even, and entries only until 11:30.
