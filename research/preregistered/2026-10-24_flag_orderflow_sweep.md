# 2026-10-24 — order flow INSIDE the flag candles, BOOK2 (pre-registered before any feature is computed)

Founder: "test the winning flag system with order flow concepts … a large series of features … adjusted for our
flag system … go more in depth the candle to get an amazing flag".

## Data
- Flags: wide15 export (flag_hhhl NQ export … wide15), NQ, filled rows (ok = 1), 2023-03-01 → 2026-03-18 —
  the span of the Databento MBO files (bot/data/raw/mbo, NQ.c.0, every trade with aggressor side + top of book).
- BOOK2 = tf ≥ 4, pole ≥ 3× (x2), pullback 5-38 % (x4), grade A/B — 819 trades. Discovery set = every filled
  flag tf 1-5 in the window (BOOK2 included).
- Pole start / pole peak rebuilt from the 1-minute cache with the exported pole high / low; cut-off = start of
  reg_min (the minute the flag is registered and the flag-mid limit is placed). Nothing after the cut-off is used
  except in the separately labelled E_* (registration → fill) features, which end at the START of the fill minute.
- Features are measured on OUR side (a short flag's delta / imbalance is mirrored).

## Features (fixed now)
Baseline = the 30 minutes before the pole start.
- Pole P_*: volume per minute vs baseline, delta ratio, large-trade share (size ≥ baseline 95th pct) and large-trade
  delta, average trade size vs baseline, trade rate vs baseline, late-minus-early delta ratio, share of pole volume in
  the top quarter of the pole range, mean quote imbalance, mean spread, quote rate vs baseline, points per unit of
  delta (efficiency).
- Flag F_*: volume per minute vs the pole's (dry-up), delta ratio, counter-aggressive volume per point of pullback
  (absorption), large-trade share / delta, mean and last quote imbalance, last microprice lead, last and mean spread,
  quote rate vs pole, volume within 1 tick of the flag extreme (share), number of separate tests of the extreme,
  volume POC position in the flag range, price vs flag VWAP, last-minute delta and volume, buy/sell average trade
  size ratio, late-minus-early delta, flag counter-delta as a share of the pole delta, flag minutes, pullback speed vs
  pole speed.
- Session S_*: price vs session VWAP (from 09:30 ET) in pole ranges, session cumulative delta ratio.
- Book depth / cancels (mbo_1m, per minute, flag minutes): depth imbalance top-10 bps, order-count imbalance,
  cancel/add ratio, average resting order size; same for the pole.
- E_* (registration → fill minute): delta ratio, volume vs flag, last imbalance, minutes waited.

## Tests (fixed now)
1. Discovery 2023-03 → 2024-12, all flags: Spearman ρ(feature, R) with a day-clustered permutation p (shuffle
   feature values across days, 2,000×); Benjamini-Hochberg q < 0.10 → shortlisted with its sign.
2. Confirmation 2025-01 → 2026-03, BOOK2 trades only, read ONCE: keep the better third (by the discovery sign);
   pass = gated mean R > base mean R with a day-clustered bootstrap 90 % CI above 0, Holm over the shortlist,
   and gated mean R > base in BOTH 2025 and 2026-Q1 when 2026 has ≥ 15 gated trades.
3. A combined score (ridge-logistic on the shortlisted features, fit on discovery) read once on confirmation:
   top-half mean R vs base, same CI rule.
4. Only a feature/score that passes 2-3 goes to the account model (flag_prop_ev mtm, skip column) — 2023-26 only,
   since earlier years have no order-by-order data.
Power note: ~250 BOOK2 confirmation trades → only effects of roughly ≥ 0.25 R per trade can be confirmed.

## Results (2026-10-24, run after the plan above was written)
Extractor research/programs/flag_orderflow.cpp (MboFeed, every trade + top of book) over 37 MBO months, ~35 s each;
4,699 flags measured, 4,658 complete (baseline inside the file). Features + depth/cancels joined from mbo_1m.
Deviation: the discovery p is a day-clustered bootstrap z of Spearman ρ (not a day permutation) — same purpose.
Ties: a feature whose "better third" kept >90 % of confirmation trades used a strict cut (E_wait still degenerate).

- Discovery (2,835 flags, 2023-03 → 2024-12): 25 of 49 features pass BH q < 0.10. Strongest: spread in pole/flag
  (wider = better, ρ ≈ 0.26 — likely a volatility / time-of-day proxy), price vs flag VWAP (+0.25), volume share at
  the flag extreme (−0.20: heavy trading at the low = worse), last-minute flag delta (+0.18).
- Confirmation (307 BOOK2 trades 2025-01 → 2026-03, read once): NO gate passes. Every Holm p = 1.00; only
  F_cancel_add's 90 % low edge is above 0 and it fails Holm and keeps 266/307 trades (cut drifted).
  Combined ridge-logistic score: top half +0.24 R vs rest, 90 % low −0.12, AUC 0.577 — fail.
- Near misses worth a forward look, not passes: F_imb low third (book leans AGAINST us during the flag)
  +0.31 R vs rest, p 0.09, same sign in 2025 and 2026 and in BOOK2 discovery (ρ −0.11) — a third sighting of the
  fleet's "fade the book" (__inv) effect; F_tests low third (few tests of the flag extreme) +0.26, p 0.09.
- Many features FLIP sign between halves (depth imbalance, pole delta, cancel ratios): the 2023-24 signals do not
  carry, so a feature stack fitted on them would be overfit.
- Context: BOOK2 NQ itself was flat in the confirmation window (2025 +0.03 R, 2026 +0.00 R; 2020-24 +0.27-0.34),
  like 2016 and 2019. With ~300 trades only effects ≳ 0.3 R per trade are confirmable.
Verdict: no inside-the-candle order-flow feature is confirmed for BOOK2. Account model not run (rule: only passes).

## Add-on check (founder: "test the new performance with the new additions") — POST-HOC, picked after seeing 2025-26
Cuts fixed on BOOK2 2023-24. Trade level (BOOK2 NQ 2023-03 → 2026-03): base 792 trades 0.178 R (23-24 0.295, 25-26 −0.008);
skip book-with (F_imb top third) 546 · 0.188 (0.256 / 0.090); only book-against 268 · 0.212 (0.224 / 0.192);
skip many tests 538 · 0.106 (worse in 23-24 AND 2026 → dropped); both 376 · 0.110.
Account (flag_prop_ev mtm, 1 micro each market, $1,000 daily stop, extreme mark, NQ+ES, filtered trades removed):
base total $88,525, Topstep $982 / Lucid Flex $937 per eval; skip book-with $85,669, $927 / $880 (2025 accounts better,
2023 worse); only book-against $80,922, $777 / $736. Verdict: both LOWER the account EV — not added.

## Sizing by order flow (2026-10-24)
2 micros when F_imb is in the low third (or not in the top third), 1 otherwise, mtm $1,000 stop: Topstep $982 → $1,091 / $1,227.
CONTROL — the same share of trades doubled at RANDOM (3 seeds each): $867-1,125 / $945-1,268. The order-flow sizing sits inside
the random range → the gain is size, not order flow. FAIL.

## Exits by order flow — PRE-REGISTERED before the simulator runs
BOOK2 NQ trades 2023-03 → 2026-03 replayed tick by tick from the fill (first trade through the flag-mid limit in the entry minute)
to stop / target / 15:55 ET flat; cost 1.5 NQ pts per trade (as the export). Base must match the export's R (check printed).
Rules (stop/target/flat always stay), judged at each clock-minute close after the fill:
- FLIP_LOSS(m,x): ≥ m min in (2, 5), last-minute delta ratio ≤ −x (0.15, 0.30) AND trade below entry → exit at the next print.
- FLIP_ANY(m,x): same without the "below entry" condition.
- STALL(m): ≥ m min in (10, 20), MFE < 0.3 R and cumulative delta since the fill against us → exit.
- BE_FLOW(k): once price ≥ entry + k R (0.5, 1.0) and the last minute's delta ratio ≥ +0.15 → stop to entry.
- BOOK_LOSS: ≥ 2 min in, the minute's mean quote imbalance on our side < 0.40 AND below entry → exit.
Pass (all required): rule mean R > base in 2023-24 (choose) AND in 2025-26 (read once); beats its RANDOM-TIMING twin (same trades
exited at a uniformly random minute between the rule's earliest allowed exit and the base exit) with p < 0.05 over the full
window; and survives Holm over the 13 rules. Account model only for a pass.

### Exit results (2026-10-24)
Tick replay of 790 BOOK2 trades reproduces the export exactly (mean R 0.181 = 0.181, corr 1.000, 100 % same win/loss).
All 13 rules FAIL: none beats base in both halves; every early-exit rule is WORSE than exiting the same trades at a random minute
(twin p 0.85-1.00 — the order-flow moment is a worse exit than chance). Counter-flow bursts against an open trade mostly
reverse, i.e. exiting on them sells the flush. flip_any rules raise win rate to 39-42 % while mean R falls to 0.02-0.10 (the WR trap).
BE_FLOW lowers R (0.137 / 0.180 vs 0.181); BOOK_LOSS never fired (minute-mean imbalance rarely < 0.40).
Twin note: the first twin drew random minutes only BEFORE the real exit (look-ahead: it never held a loser to its stop) — replaced
by a draw over the whole holding time to 15:55; verdict unchanged.
Overall verdict for order flow on BOOK2: entry filters, sizing and exits all fail. The one consistent pattern is "fade": book or
flow leaning against us during the flag/trade is not bad news (flag F_imb, exit rules, fleet __inv).

## Round 3 — add on a flush, enter on exhaustion (PRE-REGISTERED 2026-10-24, before the simulator runs)
Same 790 BOOK2 NQ trades, tick replay (research/programs/flag_candidates.cpp). Every minute close is a candidate; Python picks.
A) ADD: while the base trade is open (from the fill minute's close), adding 1 unit at the next print after a minute close,
   same stop/target/flat; added R in units of the original risk, cost 1.5 pts. Rules: FLUSH(x) first minute with delta ratio
   ≤ −x (0.15, 0.30) and price still above the stop; contrast FLOW(x) delta ratio ≥ +x. Twin: add at a uniformly random
   minute close among the trade's open minutes. Metric: mean added R per add.
B) ENTRY: replace the flag-mid limit. Window = registration (t_cut) → the export's flag end (end_min). Candidate = any minute
   close with price at/below the flag mid (long; mirrored for shorts). EXHAUST(x): first such minute whose delta ratio ≥ +x
   (0.10, 0.20) after at least one minute with delta < 0 since registration → enter at the next print, same stop/target/flat.
   Twin: a uniformly random candidate minute of the same flag. No candidate/signal = no trade. Metric: mean R per trade and
   total R vs base.
Pass: beats its twin with p < 0.05 (2,000 draws), better than its comparison (A: twin; B: base mean R AND twin) in 2023-24 AND
2025-26, Holm over the 4 rules. Account model only for a pass.

### Round 3 results (2026-10-24)
ADD on flush passed its PRE-REGISTERED twin (p 0.000) but that twin was biased: it drew from every open minute, while the rules
fire early (median minute 5) and late minutes near the flat are poor adds — the "add on FLOW" contrast passed the same twin too.
Time-matched controls (research/programs/flag_orderflow/round3b.py): vs the same minute on other trades the flush add is NOT better
(p 0.72 / 0.16) → FAIL. Only effect: within a trade, adding on the flush minute beats ±2 minutes (p 0.001; with-flow loses, p 1.0)
— 1-minute dip-buying micro-timing, which the flag-mid limit already does. ENTRY on exhaustion: x0.10 worse than random dip
entries (p 0.997); x0.20 p 0.04, Holm 0.08 → FAIL, and it keeps 31 R of base's 143 R. Round 3 verdict: no pass.

## Round 4 — LIQUIDITY (PRE-REGISTERED 2026-10-24, before code). BOOK2 NQ + ES, 2014-01 → 2026-10, 1-minute cache bars.
Choose on 2014-2019, read 2020-2026 once; cost 1.5 NQ / 0.5 ES pts; R in the original trade's risk units.
- LQ1 stop hunt: share of stopped trades whose original target trades later that session. SWEEP-RECLAIM RE-ENTRY: after a stop-out,
  the first 1m close back beyond the flag extreme within 15 min → re-enter at that close, stop = the sweep extreme ∓ 1 tick, original
  target, 15:55 flat. Twin: re-enter at a random minute close of the same 15-min window (stop = extreme so far ∓ 1 tick).
  Pass: re-entry mean R > 0 in both halves AND > twin with p < 0.05.
- LQ2 sweep inside the flag: the flag's extreme undercut an earlier swing low of the same flag (≥ 1 tick) and a later bar closed
  back above it before registration. Pass: swept flags mean R > the rest in both halves, day-clustered 90 % CI > 0.
- LQ3 sweep of a known pool: the flag's extreme traded through the prior RTH session's low (long; high for shorts) or the overnight
  low (18:00 → 09:30). Same pass rule as LQ2.
- LQ4 book thickness (NQ 2023-03 → 2026-03, book_1m): mean top-of-book size at the registration minute vs its trailing 20-session
  median; thin third vs thick third. Exploratory (one split only: 2023-24 / 2025-26).
Holm over LQ1-LQ3. Account model only for a pass.

### Round 4 results — liquidity (2026-10-24)
6,320 BOOK2 trades NQ+ES 2014-2026 (mean R 0.138; 2014-19 0.068 / 2020-26 0.207).
- LQ1: 67 % of trades stop out; only 5.6 % of stopped trades see the original target later that session → our stops are rarely
  "hunted". Sweep-reclaim re-entry (1,303 = 31 % of stops): mean R −0.060, negative in both halves (−0.087 / −0.037), below its
  random re-entry twin (0.140, p 1.000); taking all re-entries = −78 R. FAIL.
- LQ2 flag swept an earlier flag low (52 % of flags): +0.158 R in 2014-19, −0.070 in 2020-26, 90 % low −0.037. FAIL (flips).
- LQ3 flag swept prior-RTH / overnight extreme: only 51 flags (1 %), +0.705 / −0.703. FAIL (too rare, flips).
- LQ4 (exploratory) book thickness at registration, NQ 2023-26: thick third 0.537 R in 2023-24 but −0.011 in 2025-26; thin 0.282 /
  0.030. Not stable. FAIL.
Verdict: no liquidity concept improves BOOK2. Code research/programs/flag_orderflow/liquidity.py, out flag_liquidity.txt.

## Round 5 — why BOOK2 went flat in 2025-26 (diagnosis 2026-10-24)
Per-trade table research/programs/flag_orderflow/diag_build.py. NQ 2025-26: win rate (33 %) and reach +1R (54 %) are normal; the
TARGET hit rate fell to 5 % (12 % normal) and 15:55 flat exits rose to 30 %. The earlier flat years 2016/2019 show the same signature
(target hits 6-7 %). Mechanism: the measured-move target (~4.6 R) stops being reached in no-follow-through regimes.
PRE-REGISTERED next step: regime signals KNOWN BEFORE the trade (VIX, ATR vs its 1-year median, overnight range / ATR for entries after
09:30, trailing 20-session trend efficiency |close−open|/range, trailing BOOK2 target-hit rate of the previous 60 trades, entry time,
stop size / ATR, target distance in R) — ranked on 2014-2022 only; a rule passes only if its bad bucket is worse in 2023-24 AND in
2025-26 and, as a skip or a half-size rule, it raises account EV in the mtm model on both halves.

### Round 5 results (2026-10-24)
- No pre-trade variable holds: each one's worst third in 2014-22 is not the worst in 2023-24 AND 2025-26 (VIX, ATR vs 1-y median,
  overnight range, trailing trend efficiency, entry time, stop / ATR, target R, pole size, pullback, gap, m60). Medians of all of them
  in 2025-26 match 2014-22 — nothing measurable changed.
- The flat stretch is within history: NQ's last 331 trades (2025-26) average 0.025 R; 16 % of earlier 331-trade windows were at or
  below that (worst −0.324, ending 2017-07). ES 2025-26 (0.135 R) is ordinary (62 % of windows lower). Verdict: a drawdown episode
  like 2016/2019, not decay.
- Strategy-momentum sizing (closest near-hold: trailing 60-trade target-hit rate in its 2014-22 low third → 1 micro, else 2; mtm,
  $1,000 stop, NQ+ES): total $164,499 vs 17 random-sizing seeds with the same share $139,314-163,354 (beats 17/17, p ≈ 0.06);
  worst month −$4,065 vs −$4,910…−$6,406 (better than all); EOD drawdown $10,692 (better than only 18 %); Topstep EV $1,073 vs
  random $752-1,394 (median). Skipping those trades instead: Topstep $1,054 → $1,019, Lucid $804 → $685. CAVEAT: the variable was
  picked after seeing a table that included 2025-26 → PROMISING for a continuous / funded account, NOT for evals; needs forward data.
- Method lesson: Topstep EV across 17 random-sizing seeds spans $752-1,394 — differences of ±$150-300 per eval between two variants
  are noise. Compare account variants against random controls, never against one base number.

## Round 6 — order flow as COST and as HIDDEN LIQUIDITY (PRE-REGISTERED 2026-10-24, before code)
A) REAL FILLS (research/programs/flag_fills.cpp), 790 BOOK2 NQ trades 2023-03 → 2026-03, MBO top of book:
   - entry: the flag-mid limit is "through" if any print trades ≥ 1 tick beyond the mid inside registration → flag end; "touch-only"
     if price only reached the mid (queue risk). - target: through vs touch-only before the exit. - stop: a stop-market fills at the
     best bid (long) / ask (short) of the last quote at the trigger print → slippage in ticks. - 15:55 flat: half the spread.
   Report rates, the mean R of touch-only fills, measured stop slippage; then REALISTIC R = base R − measured stop slippage − flat half
   spread, touch-only entries removed, touch-only targets counted at 50 % (sensitivity: 0 % / 100 %). Account model (mtm, 1 micro,
   $1,000 stop) on the realistic R vs base — a cost measurement, no pass/fail.
B) HIDDEN LIQUIDITY at the flag extreme (feature F_hidden, added to flag_orderflow.cpp): volume traded AT the flag-extreme price while
   it was the best bid (long; best ask for shorts) ÷ the largest size displayed there. Also F_hidden_vol = that volume / flag volume.
   Test: discovery = all flags 2023-24 (Spearman, day-clustered bootstrap p < 0.05, sign fixed there); confirmation = BOOK2 2025-26,
   better third (cut on BOOK2 2023-24) vs rest, day-clustered 90 % CI > 0 and better in 2025 and in 2026 (if ≥ 15 trades). Holm over 2.

## Round 7 — CANDLE BEHAVIOUR (PRE-REGISTERED 2026-10-24, before code). BOOK2 NQ + ES 2014-01 → 2026-10, flag's own tf candles
(4/5 m, aligned to 09:30) built from the 1-minute cache; pole start/peak as geometry.py; everything ends at the registration minute.
Features (our side, shorts mirrored):
- pole: n candles, mean body/range, mean close location (CLV), share closing our way, largest candle's share of the pole, mean
  wick against us / range.
- flag: n candles, share of counter-colour candles, mean body/range, mean CLV, mean wick toward the stop (rejection) / range,
  mean overlap of consecutive candles, last-candle range / first flag candle range (contraction), lower-highs share (orderly slope).
- registration candle: CLV, body/range, colour, hammer (wick toward the stop ≥ 2× body), inside bar, engulfing.
Discovery 2014-2019 (Spearman ρ with R, day-clustered bootstrap p, BH q < 0.10, sign fixed). Confirmation 2020-2026 read once:
better third (cut on 2014-19) vs rest, day-clustered 90 % CI > 0 AND better in 2020-22 and in 2023-26. Holm over the shortlist.
Binary features: present vs absent. A pass then goes to the account model against random-skip controls.

### Round 6 results (2026-10-24)
A) REAL FILLS, 790 BOOK2 NQ trades 2023-03 → 2026-03 (flag_fills.cpp): flag-mid limit only touched (not traded through) on 10 trades
   (1.3 %); every one of 76 target exits traded THROUGH the target; stop-market slippage vs the book mean 0.58 ticks (median 0.56,
   90th pct 0.96, max 5.9; 6.1 % > 1 tick); 15:55 flat half spread 0.24 pts. Realistic R 0.181 → 0.173-0.177 (−2 to −4 %), inside the
   1.5-pt cost already charged → the account model is not re-run (cost change too small to move it). CONSEQUENCE: the 4.6-5.7 ticks
   of live ORB slippage (FILLS agent, 2026-09-30) are not market liquidity — they come from our execution path (pg-polled feed latency,
   routing). Fixing execution, not the strategy, is where those ticks are.
B) HIDDEN LIQUIDITY at the flag extreme: F_hidden discovery ρ +0.016 (p 0.50); F_hidden_vol ρ −0.098 (p < 0.001) but the measure is
   sparse (median 0, p90 0 — the exact extreme usually prints once). Confirmation: high-F_hidden third −0.21 R vs rest, low-F_hidden_vol
   third −0.09 R; Holm 0.82 / 1.00. FAIL. (A ±2-tick zone version would be a new, exploratory test.)

### Round 7 results — candle behaviour (2026-10-24, candles.py, 6,320 BOOK2 trades NQ+ES)
Discovery 2014-19: 10 of 20 features pass BH. Confirmation 2020-26 (base 0.207 R): NONE passes.
- Near miss: pole candles closing near their highs (P_clv top third) +0.181 R vs rest, 90 % low +0.032, positive in 2020-22 (+0.121)
  and 2023-26 (+0.239), but Holm 0.21. P_green (share of pole candles closing our way) +0.125, Holm 0.61.
- REVERSAL: the "confirmation candle" — registration candle closing strong (R_clv), our colour (R_green), engulfing, hammer — was GOOD
  in 2014-19 (ρ +0.04 to +0.18) and is BAD in 2020-26 (−0.11 to −0.15 R vs rest, negative in BOTH 2020-22 and 2023-26). A strong
  candle right before the flag-mid order now marks worse trades. Do not add candle-confirmation rules.

## Round 8 — ORDER FLOW × ORB and ORB × FLAGS (PRE-REGISTERED 2026-10-24, before code)
A) ORB: paper twin orb_5_five_shots, label nq_hist (MBO replay), 2024-03 → 2026-03-18; "live-like" = the first 3 trades of a day with
   entry ≤ 10:30 ET (the live rule). Per trade, from MBO (flag_orderflow.cpp, segments re-mapped): pre-open 09:00-09:30 (baseline),
   the opening range 09:30-09:35, the approach from 09:35 to the entry print (nothing at/after the entry). Features on the trade's
   side: opening-range delta, volume vs pre-open, large-trade delta, mean imbalance and spread; approach delta, last-minute delta,
   large-trade delta, book imbalance / microprice / spread at the break, minutes from range end to the break; session delta since 09:30.
   Discovery = all ORB trades < 2025-03-01 (Spearman with $ per contract, day-clustered bootstrap, BH q < 0.10, sign fixed);
   confirmation = live-like trades ≥ 2025-03-01, read once: better third (cut on discovery live-like) vs rest, day-clustered 90 % CI > 0,
   better in 2025 and 2026, Holm over the shortlist.
B) ORB × FLAGS, BOOK2 NQ+ES 2014-2026 from bars: for flags registered after 09:35, the day's FIRST 5-minute-range break (before the
   flag's registration) agrees with the flag's side vs disagrees vs no break yet. Split 2014-19 / 2020-26, day-clustered CI. Order-flow
   add-on (2023-26, bars_1m buy volume): opening-range delta agrees with the flag's side. Pass = agree > disagree in both halves with
   the 90 % CI > 0; then the account model against random skips.

### Round 8 results (2026-10-24)
A) ORB (orb_5_five_shots, nq_hist MBO replay, 2,352 trades 2024-03 → 2026-03, live costs): the strategy itself loses — discovery
   −$4.98, confirmation (live-like: first 3 trades, ≤ 10:30) −$4.68 per MNQ per trade. Of 13 order-flow features, two pass discovery
   (minutes from range end to the break ρ +0.073; approach delta ρ −0.075 — buying INTO an upside break is worse, the fade pattern
   again); neither confirms (diff −$1.76 and +$0.78, Holm 0.85 / 0.75). FAIL.
B) ORB × FLAGS (6,320 BOOK2 flags, all registered after 09:35): the day's first 5-min range break agreeing with the flag's side vs
   against: +0.127 R in 2014-19, −0.023 in 2020-26, 90 % low −0.092 → FAIL (flips). Order-flow add-on (NQ 2023-26, 825 flags):
   opening-range delta WITH the flag's side is worse than against (−0.088 R; 2023-24 −0.057, 2025-26 −0.097; not significant) — the
   fade direction once more, too weak to use.

## Round 9 — the same tests on CRYPTO (PRE-REGISTERED 2026-10-24, before code)
BTC only: BOOK2 on BTCUSDT (wide15 export of 2026-10-23, 1,242 trades 2020-2026, the one crypto book that passed at maker cost).
ETH excluded: its flag series (ETHLOG) is rescaled, so its prices cannot be matched to real trades, and it failed at every cost.
Order flow = Binance aggTrades (every trade, aggressor side; NO order book) → only flow features (delta, large trades, volume,
absorption, tests of the extreme, POC, VWAP, last-minute delta, size asymmetry, session delta); no book / spread / fill tests.
R = gross R from the export's entry/exit prices minus a maker round trip of 0.04 % of price (the pre-registered crypto cost).
- C1 flow sweep inside the candles: discovery 2020-2022 (all BTC flags, BH q < 0.10, day-clustered bootstrap), confirmation BOOK2
  2023-2026 once — better third vs rest, 90 % CI > 0, better in 2023-24 AND 2025-26, Holm.
- C2 exits on order flow: the 13 Round-2 rules, tick replay from aggTrades; pass = better than base in both halves.
- C3 candles (Round 7 features) and C4 liquidity (stop hunt, sweep-reclaim re-entry, in-flag sweep) on BTC 1-minute bars,
  discovery 2020-2022 / confirmation 2023-2026, same rules as their NQ rounds.

### Round 9 results — BTC (2026-10-24; scripts research/programs/flag_orderflow/btc/, outputs research/programs/out/btc_flag_*.txt)
BOOK2 BTC 1,240 trades 2021-01 → 2026-10 at a 0.04 % maker round trip: mean R 0.125 (2021-22 0.228 / 2023-26 0.069). Order flow
from 82 months of Binance aggTrades (~100 M trades a month), extracted with the same flag_orderflow.cpp (Binance reader added).
- C1 flow sweep: 18 of 30 flow features pass discovery (all 6,307 flags 2020-22); NONE confirms on BOOK2 2023-26 (791 trades).
  Closest: many tests of the flag low (F_tests high third) +0.357 R vs rest, 90 % low +0.004, positive in 2023-24 and 2025-26, but
  Holm 0.91 — and on NQ the FEW-tests side was the better one, so it does not carry across markets. Combined score AUC 0.519.
- C2 exits: tick replay reproduces the export exactly (0.125 = 0.125, corr 1.000); all 13 rules worse than base and worse than
  random-timing exits (p 0.93-1.00) — the same result as NQ.
- C3 candles: the CONFIRMATION-CANDLE REVERSAL REPLICATES — registration candle strong / green and flag candles closing high were good
  in 2021-22 (ρ +0.16 to +0.18) and are bad in 2023-26 (−0.21 to −0.42 R vs rest). Second market, same flip.
- C4 liquidity: stop hunt 7.3 % (rare, as NQ); sweep-reclaim re-entry −0.127 R (fail, as NQ); a sweep of an earlier flag low is
  WORSE in both halves (−0.172 / −0.166) — opposite to the pre-registered direction, so post-hoc; it agrees with NQ 2020-26 (−0.070).
  Prior-day sweep: 11 flags, too rare.
Verdict: crypto, with 6+ years of trade flow, says the same as NQ — order flow does not improve the flag book; two robust "don'ts":
no confirmation-candle rule, no re-entry after a stop sweep. Candidate for a pre-registered forward test: skip flags whose low swept
an earlier low inside the flag (BTC both halves + NQ 2020-26).
