# Dependence structure of the BOOK2 flag trades — written 2026-10-22 23:45 Cyprus, BEFORE the run

Founder: "do more tests on the flags, correlations, to get a better output" and "what other statistic formulas can we use?".
Every earlier test asked whether a VARIANT beats the book. None asked how the book's trades depend on each other — and the
account model's weak point is variance (worst drawdown $5,277 against a $2,000 trail, 2026-10-22_book2_account.md Part 3),
which is exactly what dependence inflates. Inputs: the W15 exports (w15_nq.csv / w15_es.csv, 71 columns, same files
flag_prop_ev reads), BOOK2 rule as in flag_prop_ev (`ok`, tf >= 4, pole mult >= 3, pullback 5–38 %, grade A or B, 2 open per
market, 2014-01-02 → 2026-10-15), net R = (pts − cost) / |entry − stop|, cost NQ 1.5 / ES 0.5 pts. Descriptive unless a
PASS rule is stated. Program: research/programs/flag_dependence.cpp (read-only), output research/programs/out/flag_dependence.txt.

## D1 — are 6,300 trades 6,300 pieces of evidence? (day clustering)
Mean net R with the naive SE (sd/√n) and the day-clustered SE (trades of one date are one cluster; pooled run clusters NQ+ES
of the same date together). Design effect = (SE_cluster / SE_naive)², effective N = n / design effect. Judged: design effect
> 1.5 means every trade-count threshold in the pipeline (≥ 20, ≥ 50, ≥ 100) overstates evidence by that factor.

## D2 — NQ and ES: one bet or two?
Daily book P&L (sum of net R) NQ vs ES: Pearson and Spearman on (a) days both traded, (b) every NY day (0 when flat). Lower-tail
dependence P(ES day < 0 | NQ day < 0) against the unconditional P(ES day < 0). Share of same-day trades on the same side.
Judged: Pearson on (a) > 0.3 → the two-market book diversifies little and the account model's 2-market variance is right to be
large; < 0.1 → they are separate bets.

## D3 — does the first flag of the day tell us about the second?
Within one market and day, trade k's mean net R given sign of trade k−1 (loss / win). Permutation test: shuffle the order of
trades inside each day 2,000 times, p = share of |difference| ≥ observed. PASS (an exploitable sequence) = p < 0.05 after Holm
AND the same sign of effect in 2014-19 and 2020-26. Expectation from 2026-10-18 (stop-after-loss failed): nothing.

## D4 — daily P&L memory
Pooled daily book P&L on every NY day (0 when flat): ACF lags 1–5, Ljung-Box Q(5) with its p, Wald-Wolfowitz runs test on
the sign of non-zero days, variance ratio VR(5). Judged: Ljung-Box p < 0.05 after Holm = the account model's independent-day
resampling understates streak risk.

## D5 — drawdown when days are NOT independent
12-month (252-day) max drawdown of the pooled daily series in R units at 1 unit a trade, 2,000 paths each: (a) iid day
bootstrap, (b) circular block bootstrap, blocks of 5 days, (c) blocks of 20 days. Median and 95th percentile. Judged: if (c)'s
median exceeds (a)'s by > 15 %, dependence matters for the drawdown cap and the month-block bootstrap in flag_prop_ev is the
one to trust, never an iid one.

## D6 — is the edge stable in time?
Yearly mean net R and trades; sign test over the 13 years (binomial, p = 0.5). Spearman rank correlation of net R against
trade order (a decay test) with its p. 2014-19 vs 2020-26 mean difference with day-clustered SE. Judged: fewer than 9 of 13
positive years OR a Holm-significant negative trend = the edge is not stable enough to size up.

## D7 — regime-conditional dependence
VIX (prior close) and ATR14 terciles over the book's trading days: per tercile mean net R by market, trades a day, and the
NQ–ES daily correlation. Descriptive: where the two markets move together is where the account draws down.

Holm correction over the judged p-values (D3, D4, D6 trend, D6 split). Nothing here changes the book or the live account; a
finding feeds the account model (D1, D5) and sizing (D2, D7).

## Results (2026-10-22 23:50 Cyprus; research/programs/out/flag_dependence.txt; 3,239 NQ + 3,081 ES BOOK2 trades)

**Three corrections made while running, each caught by an independent cross-check, before any verdict:**
1. The W15 `pts` column is ALREADY net of the 1.5 / 0.5 pt cost (flag_prop_ev adds it back before rescaling to today's ATR). A
   first run subtracted it again and read NQ +0.058R instead of the file's +0.181R. Net R here = the file's `r`.
2. 80 % of same-day consecutive pairs (2,513 of 3,153) OVERLAP in time — trade k−1 has not exited when trade k enters, and
   95.8 % of same-day NQ × ES pairs are on the same side. "Trade k after a loss" on all pairs read −0.71R vs +2.15R after a win:
   that is two positions sharing one afternoon move, not behaviour. D3 is therefore judged on CAUSAL pairs only (k−1 exited
   before k entered), with the permutation labels pooled across pairs (a within-day shuffle cannot move a day's single pair).
3. The D6 trend (Spearman +0.351 of R against trade order, confirmed independently) is NOT a strengthening edge: the loss share
   is flat (60–72 % every year) while the median loser moved from −1.19R (2014) to −1.03R (2025), because a FIXED 1.5-pt cost
   on an index that tripled fell from ~0.2R to ~0.06R a trade. Raw R must not be compared across years; the ATR rescaling in
   flag_prop_ev is what makes years comparable.

| Test | Result | Verdict |
|---|---|---|
| D1 clustering | design effect NQ 1.76, ES 1.68, POOLED 2.62 → 6,320 trades ≈ 2,415 independent; t 5.10 naive → 3.15 clustered | **judged: > 1.5** — every trade-count threshold overstates evidence ~1.7× per market, ~2.6× for the two-market book |
| D2 NQ vs ES | daily P&L Pearson 0.615 (both traded), 0.534 (all days); P(ES<0 | NQ<0) = 0.83 vs 0.62 | **judged: > 0.3** — the two markets are largely ONE bet; the account model's large two-market variance is right |
| D3 sequence | causal pairs: NQ −0.14R diff p 0.70; ES +0.41R diff p 0.22 (n after win 44 / 63) | FAIL — no exploitable sequence (matches stop-after-loss, 10-18) |
| D4 daily memory | ACF lag1 −0.037, Ljung-Box Q(5) 7.4 p 0.19, runs z +1.3, VR(5) 0.93 | not significant — days are independent |
| D5 drawdown | 12-month max DD median 56.1R iid / 55.5R blocks-5 / 56.8R blocks-20; p95 113 / 111 / 111 | iid vs block within 2 % — day-to-day dependence does not move the drawdown; the month-block bootstrap in flag_prop_ev is adequate |
| D6 stability | 11 of 13 years positive (binomial p 0.011); 2014-19 +0.068R vs 2020-26 +0.207R, clustered z 1.57 p 0.12; trend p < 0.001 but see correction 3 | positive years PASS; the split is not significant once clustered; the "trend" is a cost artefact |
| D7 regimes | NQ–ES correlation 0.56–0.65 in EVERY VIX and ATR tercile; low-ATR tercile NQ +0.10 / ES +0.01, mid +0.21 / +0.15, high +0.24 / +0.13 | descriptive: the co-movement is structural, not a regime; low-ATR days are where ES earns nothing |

Holm over the 5 judged p-values: only the D6 trend survives, and it is explained by correction 3.

**What this changes:** nothing in the book, the account or the live config. It changes how the evidence is COUNTED: (a) a
trade-count gate of 20 / 50 / 100 is really ~11 / 29 / 57 independent observations for one market and fewer for the book;
(b) NQ + ES BOOK2 is one correlated bet at ~1.6 units, not two bets — the prop-account variance the drawdown tests found is
the consequence, and a second market on the same signal cannot diversify it; a second STREAM must trade different hours or a
different driver (the 2026-10-22 recommendation stands); (c) iid-day resampling is safe for this book's drawdown, so D5 closes
the question the pre-registration asked.

## Validation (2026-10-22, after the run; synthetic W15 files with known structure, same binary, session scratchpad)
| synthetic case | design effect NQ / pooled | NQ–ES Pearson | Ljung-Box p | 12-m DD iid vs blocks-20 | expected |
|---|---|---|---|---|---|
| A iid trades, markets independent | 0.98 / 0.96 | −0.009 | 0.32 | 44.0 vs 43.2 | all ≈ 1, 0, flat, equal — met |
| B shared day shock (sd 1.5) in both markets | 1.55 / 2.31 | 0.622 | 0.53 | 107 vs 105 | clustering + correlation, no memory — met |
| C AR(1) day shock, φ 0.6, both markets | 1.58 / 2.40 | 0.645 | < 1e-4 | 159 vs 206 (+30 %) | memory and block > iid — met |
Case B reproduces the real book's numbers (design effect 1.76 / 2.62, correlation 0.61) with a single shared day shock, which is
the simplest reading of D1 + D2: the day's move is the common factor. The real book's D4/D5 match case B, not C.
