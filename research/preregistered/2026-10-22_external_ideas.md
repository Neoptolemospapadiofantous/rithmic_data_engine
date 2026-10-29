# Ideas from papers and open-source projects — overfitting check, absorption, quarterly re-fit, second detector — pre-registered 2026-10-22

Founder: "check the internet … are there any other active git projects … papers we can utilize?" → findings → "test all". Written BEFORE
any run. Programs: new `research/programs/pbo.cpp` (overfitting), new `research/programs/flag_extra.cpp` (E2-E4), variant writers get
an env filter `VARIANT=<name>` so one variant file is written at a time (the disk is ~100 % full; nothing is deleted). Account =
`flag_prop_ev … mtm` (1 micro each market, 2 open, $500 stop). Candidate = BOOK2 + gap sizing + the 15m trend filter (htf_context
FILT_TREND_15m); "CAND-G" = gap sizing alone.

## E1 — Probability of backtest overfitting (Bailey, Borwein, López de Prado, Zhu: CSCV) + deflated Sharpe (Bailey & López de Prado)
Matrix = the daily P&L (mtm, conservative, 2014-01 → 2026-10) of every account variant tried on the corrected model today (flat,
book_optimize O-knobs, backup entries, regime sizing S/SC/SK, context A/B, htf SIZE/FILT/STRICT). CSCV with S = 16 contiguous blocks:
for each of the 12,870 splits into 8 in-sample and 8 out-of-sample blocks, the variant with the best in-sample daily Sharpe; PBO = share of
splits where it ranks at or below the out-of-sample median. Deflated Sharpe of the candidate given N = the number of variants, with the
variance of the variants' Sharpe ratios and the candidate's skew / kurtosis. Report: PBO, the candidate's DSR, and how often the candidate
itself is the in-sample winner. Read: PBO < 0.5 and DSR > 0.95 = the selection is unlikely to be luck. Descriptive, no pass / fail on the book.

## E2 — Absorption at the flag's low (Bookmap's MBO definition, 1-minute proxy; NQ only, book + aggressor data 2023-03 → 2026-03)
Absorption minute (mirrored to the flag's side) = a 1m bar inside the flag (after the pole top, before the fill) whose low is in the lower
half of the flag's range so far, with aggressor-sell volume >= 1.5 × the session's average per minute so far, closing in its upper half,
and resting bid size (bidq, within 10 bp) at or above its session average — sellers hit, price held, bids refilled. Feature: >= 1 such
minute before the fill. Trade level on NQ BOOK2 + every 4-5m wide15 flag: R and edge over post-registration twins, halves 2023-03 →
2024-09 / 2024-10 → 2026-03; shuffle p (1,000, within month). Account: the candidate + (a) skip NQ flags WITHOUT absorption on book days,
(b) +1 micro on NQ flags WITH absorption (other days unchanged). PASS = trade-level better in both halves, shuffle p < 0.05, and the account
variant >= the candidate on 2023-26 (the only years with data) on 2 of 3 plans (Topstep / Lucid / Select Daily).

## E3 — Quarterly re-fit of stop and target (Arévalo et al. 2017's dynamic window)
From each BOOK2 fill (entry price / minute unchanged): stop = flag low − k × ATR, k ∈ {0, 0.05, 0.1, 0.2, 0.3}; target = pole top + m × pole,
m ∈ {0.75, 1.0, 1.25, 1.5}; 1m re-simulation with the page's conventions (stop first in the entry bar, flat at the 15:55 open). Re-fit:
each calendar quarter uses the (k, m) with the best total net R over the previous 4 quarters (2013 warm-up), applied to the next
quarter; also the best static (k, m) chosen on 2014-19. Both with the candidate's sizing and filter. PASS = >= the candidate on 2020-25 on 2 of 3
plans and with +1 pt cost; the fixed (0.1, 1.0) must reproduce the candidate exactly (check).

## E4 — A second, independent detector (Lo, Mamaysky & Wang 2000: kernel-regression smoothing)
Nadaraya-Watson (Gaussian kernel, bandwidth 3 bars) on the flag's own chart closes up to the bar before the fill; extrema of the smoothed
line. CONFIRMED = the smoothed line has a local minimum → local maximum rise of >= 2 × the chart's average bar range ending within the last 8
bars, followed by a smoothed decline of 5-62 % of that rise. Feature on BOOK2: confirmed vs not. Trade level as E2 (2014-19 / 2020-26,
both markets, shuffle p); account: the candidate skipping unconfirmed flags. PASS = better in both halves on both markets, shuffle
p < 0.05, account >= the candidate on 2020-25 on 2 of 3 plans and with +1 pt.

## Not run
Image-based CNN pattern recognition (Velay & Daniel 2018; Chen & Tsai 2020) and the Python GitHub detectors (zeta-zetra/chart_patterns,
BennyThadikaran/stock-pattern): this project is C++ only, our own shape models already failed, and a rigorous ConvNet study found little
predictive value in candlestick charting. E4 covers the "independent detector" idea in C++.

## Results (run 2026-10-23, after the text above)
Outputs: `research/programs/out/pbo.txt`, `out/flag_extra.txt` (trade level), `out/flag_extra_mtm.txt` (account).
Account = per-eval EV, 2014-19 → 2020-25, conservative marks; +1pt = 2020-25 with one extra NQ point a trade.

| Variant | Topstep | Lucid | Select Daily | Verdict |
|---|---|---|---|---|
| E0 candidate | 1543 → 1648 (+1pt 1415) | 1084 → 1479 (1281) | 1251 → 1320 (1218) | reference |
| E3 fixed (0.1, 1.0) | identical | identical | identical | CHECK PASSED (trade level 3,827 / 3,837 within 1 %, account identical) |
| E1 PBO / DSR | — | — | — | PBO 0.187, DSR 1.000 (95 variants × 2,042 sessions); the candidate is the in-sample winner in 48.5 % of splits |
| E2 skip no-absorption | 579 | 533 | 282 | FAIL (removes most NQ trades) |
| E2 +1 micro on absorption | 1649 (1415) | 1480 (1281) | 1332 (1227) | FAIL: only 8 absorption trades, so the trade-level test cannot pass; the account gain is noise |
| E3 quarterly re-fit | 1003 → 619 | 728 → 600 | 656 → 399 | FAIL |
| E3 best static (k 0.10, m 1.50) | 1675 → 1736 (1528) | 1178 → 1463 (1299) | 1351 → 1225 (1059) | FAIL (better on 1 of 3 plans only) |
| E4 kernel-confirmed only | 46 | 46 | 0 | FAIL (~12 confirmed flags; the detector almost never agrees) |

Read: the selection is unlikely to be luck (PBO well under 0.5, deflated Sharpe ≈ 1 even after 95 trials; heavy right skew helps
it). None of E2-E4 improves the candidate. The candidate stays BOOK2 + gap sizing + the 15m trend filter.
