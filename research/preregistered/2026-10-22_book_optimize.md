# Optimisation on the corrected account model + engulfing candles inside flags — pre-registered 2026-10-22

Founder: "explain now the math and what else can we do. research more. optimize more", then "so test engulfing candles if that gives
us an advantage … on all matrix tests we had". Written BEFORE any run. Account model = `flag_prop_ev … mtm` only (the old
one-trade-at-a-time model had look-ahead, 2026-10-22_book3.md). Programs: flag_prop_ev (mtm knobs), new `flag_engulf.cpp`.

## Why these ideas (the maths)

BOOK2 at 1 micro each market makes μ ≈ $34.5 a day with σ ≈ $375 a day (2014-26, mtm, $500 own stop). A 50K eval needs +$3,000
before a $2,000 trailing drawdown: at μ the target takes ~87 trading days on average, longer than the 60-day eval window modelled,
so most passes are variance and the pass rate sits near a coin flip's 40 %. Account EV therefore rises only with a better μ / σ
(risk-adjusted daily edge), not with size (doubling size doubles μ and σ: same ratio, faster ruin against fixed-$ limits).
Every idea below targets μ / σ or the account mechanics.

## Common rules

Baseline per plan = BOOK2, 1 micro each market, 2 open per market, $500 own daily stop (the corrected sweep's stable pick).
Plans judged: topstep_50k, lucid_flex_50k, select_daily_50k (+ apex_eod_50k, builder_50k, 100K / 150K reported).
**PASS** (any idea): conservative marking, on at least two of the three judged plans, 2014-19 EV >= baseline + $100 AND 2020-25
EV >= baseline; and the optimistic (close) marking not below baseline on those plans. Parameters listed are the whole grid —
picking the best grid point uses 2014-19 only.

## O — optimisation knobs (mtm)

- O1 max open trades across BOTH markets: 1, 2, 3
- O2 equal-$ risk sizing: micros = clamp(round(X / 1-micro risk), 1, 3), X = $40 / $60 / $80
- O3 skip trades whose 1-micro risk exceeds $60 / $90 / $120
- O4 no new entries after 11:00 / 12:00 / 13:00
- O5 A grade only
- O6 NQ only / ES only
- O7 daily profit lock at +$300 / +$600 (realised + open at the minute's close)
- O8 no new entries after 1 / 2 losing trades in the day
- O9 eval 2 micros, funded 1 / eval 1, funded 2
- O10 account size: Tradeify Select Daily / Flex 100K and 150K (BOOK2 at 1, 2, 3 micros)
Then: the O ideas that pass are combined (all passing knobs together, each at its 2014-19 best) and the combination must pass too.

## E — engulfing candles inside the flag, on the flag matrix (flag_engulf.cpp)

Engulfing (bodies, as edge_lab pat_engulf): bullish = previous chart bar down, this bar up, close >= previous open, open <=
previous close; bearish mirrored. Measured on the flag's own chart (1-5m, 09:30 grid), mirrored for bear flags, bars closed before
the event. Sets: every wide15 flag; BOOK2. Matrix cells: chart 1-5m × pole multiple (2-3, 3-4, 4-5, 5+) × pullback (5-25, 25-38,
38-50, 50-70 %) × grade A / B / C × side.
- EA filter: flag-mid fills whose last closed chart bar before the fill is a bearish engulfing (selling overwhelmed buying into
  our limit) vs a bullish engulfing vs neither. PASS = skipping one class raises the edge (vs post-registration twins) in both
  halves (2014-19 / 2020-26) on both markets AND the mtm account rule above (as a BOOK2 skip variant).
- EB entry: instead of the flag-mid limit, enter at the next minute's open after the first bullish engulfing chart bar that closes
  inside the flag (below the pole top) between registration and the flag's end, before 15:00; stop = lowest low since the pole top
  − 0.1 ATR, target pole top + pole, flat at the 15:55 open, risk > 0.5 pt, cost NQ 1.5 / ES 0.5. PASS = EB beats the flag-mid
  entry's edge on BOOK2 flags in both halves on both markets AND (as BOOK2's entry, and separately as a backup when the limit never
  filled) the mtm account rule.
- Matrix table per cell: n, R and edge for mid entry vs EB, and for EA classes — descriptive; a cell is not a rule unless it was
  chosen on 2014-19 and holds on 2020-26 (top 5 cells by 2014-19 edge reported with their 2020-26).

Nothing changes in the live account; a pass is a paper candidate.

## Results (2026-10-22; out/book_optimize_sweep.txt, flag_engulf.txt, flag_engulf_mtm.txt, book_optimize_combo.txt)

Baseline BOOK2 1 micro each, 2 open, $500 stop (conservative; 2014-19 → 2020-25 per eval): Topstep $1,029 → $971, Lucid $998 → $970,
Select Daily $964 → $837; daily mean $34.5, sd $375.5, worst drawdown $6,116.

PASS (2 of 3 judged plans): **EB backup** (engulfing entry when the flag-mid limit never filled) Topstep $1,192 → $1,077, Lucid
$1,138 → $1,068, Select Daily $1,029 → $1,043 (h1 +65, short of +100); **O2 equal-$ risk $80** Topstep $1,213 → $990, Select Daily
$1,187 → $859 (Lucid 2020-25 below). Combination (both): Topstep $1,428 → $1,042, Select Daily $1,319 → $845 PASS by the rule, Lucid
$1,268 → $963 (−$7 in 2020-25); daily mean $47.7, sd $492. NOTE: the combination's gain is almost all in 2014-19 (where the grid point
was chosen); in 2020-25 it adds +$71 / +$8 / −$7 — EB backup alone adds more out of sample (+$106 / +$98 / +$206). Recommendation:
EB backup alone is the candidate; equal-$ risk is not robust out of sample.

FAIL: O1 caps on open trades across markets (1: $32, 2: $431, 3: $870 Topstep — fewer trades lose); O3 skip wide-stop trades (fewer
trades); O4 entry cut-offs 11/12/13:00; O5 A grade only; O6 one market; O7 profit lock; O8 stop after 1-2 losses; O9 eval/funded size
split; O10 2-3 micros on 50K; 100K/150K accounts (best $804-817 at 2 micros, below the 50K baseline).

Engulfing (part E): EB as the ENTRY instead of the limit FAILS ($243 Topstep); on the same BOOK2 flags EB beats the limit in 2014-19
(NQ +0.065 vs −0.107, ES +0.129 vs −0.035 edge) but not in 2020-26 (+0.010 vs +0.013, +0.009 vs +0.038). EA filter FAILS: a
BEARISH engulfing bar right before the fill is the BEST class on NQ (+0.37 edge both halves; ES −0.18 / +0.45) — a sharp pullback into
the limit is good, not a warning; skipping bullish-engulf fills helps 2014-19 only ($1,203 → $888 Topstep). Matrix: the top 5
chart × pole × pullback × grade cells by 2014-19 EB edge shrink or flip in 2020-26 (e.g. 4m pole 5×+ pullback 5-25 % B +0.31 → −0.24
NQ). Standalone 15m engulfing failed earlier (edge_lab pat_engulf, 2026-10-16).

## CORRECTION (same day, 2026-10-22_backup_entries.md)
The EB-backup PASS above is WITHDRAWN: its trades were on flags whose limit HAD filled but was skipped (chart busy), because the export's
ok = 0 does not distinguish that from "never filled". With the fill rebuilt, the engulfing backup triggers 3 times in 16 years and
changes nothing. The other verdicts in this file stand (they do not use backups).
