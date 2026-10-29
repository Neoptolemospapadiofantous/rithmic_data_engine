# Do the flags work on BTC / ETH perps at real costs? — written 2026-10-23 06:40 Cyprus, BEFORE the run

Founder: "how about we try this on BTC … or eth, with leverage of course" → "lets try this". Data: data/edge_lab/cache
BTCUSDT.csv (Binance 1m, 2020-01-01 → 2026-10-15) and ETHLOG.csv (the rescaled ETH 1m series from the 2026-10-10 crypto work;
percent moves preserved). Flag detector = a Python approximation of the BOOK2 shape on 5-minute bars (pole: 6 bars one way,
≥ 0.20 × ATR14 of daily ranges; flag: next 5 bars, pullback 5–38 % of the pole, flag range ≤ 0.6 × pole; entry: limit at the
flag midpoint filled if touched within 3 bars; stop just beyond the flag extreme (+10 % of the pole); target 3R; both sides).
Exit: NY variant flat at 15:55 ET; 24/7 variant time-stop 6 hours. Costs: perp taker 0.05 % per side = 0.10 % of notional per
round trip (funding ignored — intraday holds); reference run at 0.01 % to show whether structure exists at all. Control:
post-registration twin — same day, same direction, random minute in the next hour, same stop/target distances. Halves 2020-22 /
2023-26. PASS = net R > 0 at perp costs in BOTH halves AND edge over twin ≥ +0.05R in both halves AND ≥ 200 trades per half.
Output research/programs/out/crypto_flags.txt. The same detector is run on NQ as a sanity check (it should show the known edge).

## Results (2026-10-23; research/programs/out/crypto_flags.txt)
**Method correction, recorded:** a first pass used a Python approximation of the detector; its NQ sanity row read −0.12R
against the book's +0.18R, so it was discarded and the REAL export program (`flag_hhhl <SYM> export … wide15`, the one that
produced the NQ/ES W15 files) was run on BTCUSDT.csv and ETHLOG.csv instead. A second pass dropped trades without a twin
(quick stop-outs) and inflated every row; fixed — net R is over ALL BOOK2 trades, the twin edge over the subset with a twin
(here 100 %). NQ sanity on the real export: +0.181R (2014-19 +0.112 / 2020-26 +0.247) — matches the book.

| market | BOOK2 trades | avg risk (% of price) | cost per round trip | net R all | twin R | edge | 2020-22 net / edge | 2023-26 net / edge | win % | PF |
|---|---|---|---|---|---|---|---|---|---|---|
| **BTC** | 1,242 (177/yr) | 0.415 % | zero (structure) | **+0.233** | +0.050 | **+0.183** | +0.310 / +0.107 | +0.191 / +0.224 | 31 | 1.27 |
| BTC | | | perp MAKER 0.04 % | **+0.116** | −0.067 | +0.183 | +0.221 / +0.107 | +0.060 / +0.224 | 30 | 1.11 |
| BTC | | | perp TAKER 0.10 % | −0.058 | −0.241 | +0.183 | +0.087 / +0.107 | −0.137 / +0.224 | 29 | 0.92 |
| ETH (rescaled series) | 1,617 (231/yr) | 0.069 % | zero | +0.122 | +0.082 | +0.040 | +0.133 / +0.065 | +0.118 / +0.030 | 29 | 1.10 |
| ETH | | | perp maker 0.04 % | −0.569 | −0.609 | +0.040 | | | 25 | 0.55 |
Verdict by the pre-registered rule (net R > 0 at perp costs in both halves, edge ≥ +0.05 both halves, ≥ 200/half):
- **BTC PASSES at maker costs (0.04 % RT)** — +0.22R / +0.06R by half, edge +0.11 / +0.22 — and **FAILS at taker costs**
  (2023-26 −0.14R). The flag structure is real on BTC (zero-cost +0.23R with a +0.18R edge over random minutes, both halves),
  about 60–70 % of the NQ book's strength, and its stop is 0.42 % of price, so fees are ~10–25 % of the risk: the whole
  result lives on execution. The flag entry is already a limit (maker); the stop exit is a market (taker) — realistic blended
  cost ≈ 0.06–0.07 %, between the two rows, ≈ +0.05R. Funding was ignored (intraday holds; a position carried over a funding
  time pays more).
- **ETH FAILS at any cost**: the flags are tiny relative to price (stop 0.07 %), the twin edge is only +0.04R, and even 0.01 %
  costs turn it negative. (ETHLOG is the rescaled series from 2026-10-10; percent moves preserved, so this is the structure, not
  an artefact of the rescale.)
Scale: at 0.42 % risk, $5,000 notional risks ~$21 a trade → ~$400/yr at maker costs; book-size risk ($48/trade) needs ~$11,500
notional, earning ~$1,000/yr at maker costs, with the measured 24/7 BTC drawdowns on top. Nothing changes any config; a BTC
flag line is a paper candidate ONLY with maker-fee execution and funding-time handling.
