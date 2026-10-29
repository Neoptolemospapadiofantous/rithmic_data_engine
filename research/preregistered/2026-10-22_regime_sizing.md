# Regime sizing on the flag book (volatility regime, 15m + daily EMA trend, noise band) — pre-registered 2026-10-22

Founder: "so sometimes the length, volatility or other features are more likely to give us a positive EV on the flag" → three ideas
from our data and an online search → "go for it". Written BEFORE any run. Program: new `research/programs/regime_size.cpp` (features +
variants with a per-trade size column), judged by `flag_prop_ev … mtm` (corrected account model; flag_prop_ev reads the optional size
column, default 1).

Book: BOOK2 trades (wide15 exports, NQ + ES), baseline 1 micro each market, 2 open per market, $500 own daily stop. Every trade is kept;
favourable trades are sized 2 micros, the rest 1 (one variant also skips the least favourable, stated below).

Features, all known before the entry minute, mirrored to the flag's side where it has one:
- R1 VOL RISING: ATR14 at the prior close > ATR14 twenty sessions earlier
- R2 PRIOR WEEK: the last 5 RTH sessions' close-to-close change, with vs against the flag's side
- R3 GAP: today's 09:30 open vs the prior RTH close, with vs against the flag's side
- E1 EMA TREND (Arévalo et al. 2017 flag rule's filter): the last completed 15-minute RTH bar's close above its EMA20 (15m RTH bars
  chained across days) AND the prior RTH close above the daily EMA20 of RTH closes — both with the flag's side
- N1 NOISE BAND (Nasdaq-100 intraday momentum): at the minute before entry, (close − 09:30 open) / open × side above σ, σ = the mean
  over the previous 14 sessions of |close / open − 1| at the same minute of the session
For each feature the favourable value (yes / no) is chosen on 2014-19 BOOK2 trades by mean net R (both markets pooled); 2020-26 shown.

Variants (each judged against the baseline):
- S1..S5 size 2 micros when that single feature is favourable
- SC2, SC3, SC4 size 2 micros when at least 2 / 3 / 4 of the five are favourable
- SK size 2 at >= 3 favourable, skip the trade at 0 favourable
**PASS** (conservative marking): on at least two of topstep_50k / lucid_flex_50k / select_daily_50k, 2014-19 EV >= baseline + $100 AND
2020-25 EV >= baseline, and the optimistic marking not below the baseline. Reported for each pass: daily mean / sd and the worst
drawdown, since sizing up raises both.

## Results (2026-10-22; out/regime_size.txt, regime_size_mtm.txt)

Mean net R of BOOK2 trades by feature (2014-19 | 2020-26): GAP WITH THE FLAG NQ +0.301 vs −0.086 | +0.432 vs +0.073, ES +0.127 vs
−0.089 | +0.307 vs +0.039 — large and stable on both markets. Volatility rising: better in 2014-19, mixed after (ES reverses). Prior week:
small, inconsistent. EMA trend: chosen "against" on 2014-19 by ES, NQ disagrees. Noise band: NQ strong, ES mixed.

Account (mtm, conservative; Topstep | Lucid | Select Daily, 2014-19 → 2020-25; baseline $1,029 → $971 | $998 → $970 | $964 → $837):
**S3 GAP PASSES** $1,502 → $1,540 | $1,007 → $1,336 | $1,237 → $1,204 (Lucid misses +$100 in 2014-19; 2 of 3), daily mean $62.6 /
sd $572 (ratio 0.109 vs 0.092), worst drawdown $7,913 vs $6,116; also Apex $931 vs $661, Builder $916 vs $622, MFF $799 vs $615.
Robustness (after the verdict): +1 pt extra cost a trade S3 $1,174 / $951 / $922 vs baseline $792 / $751 / $703; better than the
baseline in 11 of 13 years (worse in 2019 and 2026, both losing years for the book — double size amplifies them).
Not pre-registered, information only: SKIPPING the against-gap trades instead is worse ($632 Topstep) — keep them at 1 micro.
FAIL: S1 vol rising $936 → $870, S2 prior week $1,009 → $881, S4 EMA trend $849 → $707, S5 noise band $718 → $1,063, SC2 / SC3 / SC4 /
SK combination scores (best SC4 $1,087 → $977).

New candidate: BOOK2 flags, 1 micro each market and 2 when the flag is with the 09:30 gap, 2 open per market, $500 own daily stop. On
/flags (lib/flags.ts BOOK_CFG.CAND gapSize, bookQty); the page matches flag_prop_ev mtm day for day on the 9 shared 2026 days (incl. 3
double-size days and a $500 stop day: page −$500 vs research −$502.25 with flattening slippage).

## What-if (founder: "what if we full port" + the Builder 25K rules, "2 consecutive wins", "2 contracts") — not pre-registered, descriptive
flag_prop_ev mtm with builder_25k added (eval $1.5K target, $1K EOD drawdown, $600 DLL, 2 minis; funded $1K EOD, $600 DLL, 50 %
consistency, buffer $1.1K, min $250, max $1,000; same assumptions as builder_50k). out/full_port_mtm.txt, full_port_evalsize_mtm.txt.
- Size up = less per eval on every plan. Builder 25K (own stop $300): flat 1 micro $287 (pass 48 %), 2 → $113, 5 → $17, 10 → $2; gap
  sizing $234 at 1. Topstep 50K: ≈ $1,000-1,480 at 1 micro (gap) → $0-124 at 10. One account trading every day: worst drawdown $5.4K
  at 1 micro, $35K at 10 — far beyond a $1-2K drawdown limit.
- Big size in the eval only (funded at 1 micro): passes faster (1-2 days at 8-10 micros) but less often; Builder 25K $72-94 per eval at
  10 micros vs $266 at 1; Topstep 50K $153-230 vs $982-1,016.
- "2 contracts" = 2 NQ minis (20 micros), NQ only, 1 open: daily mean $290, sd $3,433. Builder 25K passes 12 % (in 1 day) but funded
  accounts at 2 contracts almost never pay (0 %): $6 per eval; funded at 1 micro $64. Builder 50K $58-99, Topstep 50K $108-167.
- Two wins in a row happen on 19.8 % of consecutive BOOK2 trade pairs (twice the 9.9 % if independent — winners cluster on trend days);
  average winner +2.84R, win rate 31.5 %. The cluster is real, but at 2 contracts one stop-out is about the whole 25K drawdown.
