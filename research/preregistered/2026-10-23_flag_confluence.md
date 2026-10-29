# Flags × our strategies as confluence — full matrix, pre-registered 2026-10-23

Founder: "what about using our flag system with our strategies as confluence" → "test all matrices". Written BEFORE any run.
Not repeated: flag book + cascade side by side as one account (2026-10-23_three_way, descriptive) and flags filtered by the day's
direction so far / VWAP side (2026-10-23_flag_context_overlays L2d/L2e). New here: whether a flag is better when one of OUR STRATEGIES
already took a position the same way that day, and the reverse.

## Strategies (fixed book settings, no selection; `build-exp/edge_lab run --trades`, 2010-07 → 2026-10, NQ and ES each on its own data)
- CAS cascade: so_cascade k1 0.25, k2 0.30, stop 0.35 ATR, 2R (book F's cascade).
- GRT gap retest: gap_or_retest, stop 0.35 ATR, 2R.
- G10 10:00 gap rule: pdm_gap_confirm (T 10:00, straight path b 2, s 1), stop 0.25 ATR, 2R.
- WKL weakness long after a gap down: weak_day_gapdown x 0.3, v 0 (09:30 long, flat 15:55).
- ANY = any of the four.
A strategy SIGNAL for a flag = that strategy's entry on the same date and market with entry time < the flag's fill minute.

## Matrix A — flags given a strategy signal
Every wide15 flag trade (ok = 1; tf 1, 2, 3, 4, 5 and the BOOK2 subset), each market, 2014-01 → 2026-10. For each strategy and ANY:
AGREE (signal on the flag's side), AGAINST (opposite side), NONE. Cells: mean net R, edge over post-registration twins (5 random minutes
in the hour after the fill, same stop/target distances), n, by half (2014-19 / 2020-26). AGREE − NONE difference with a shuffle p (1,000,
labels shuffled within year × market). Read: a confluence is REAL only if AGREE beats NONE in R AND in edge-over-twins in BOTH halves on
BOTH markets (the twin is what separates "the day is trending" from "the flag adds something").

## Matrix B — strategy trades given a flag
Each strategy's trades 2014-01 → 2026-10, split by whether a BOOK2 flag on the same side FILLED earlier that day (AGREE), on the opposite
side (AGAINST), or none; also with any 1-5m wide15 flag. Mean net pts / trade by half and market. Descriptive: a sizing candidate for the
book only if AGREE > NONE in both halves on both markets; nothing is adopted from this file.

## C — account variants on the flag candidate (BOOK2 + gap sizing + 15m filter), `flag_prop_ev … mtm`
- CF_skipAgainst: skip candidate flags with an AGAINST signal from ANY strategy.
- CF_agree2: 2 micros when ANY strategy AGREES (else the candidate's own size: 2 with the gap, 1 otherwise).
- CF_onlyAgree: trade only flags with an ANY AGREE signal.
PASS = >= the candidate on 2020-25 on 2 of 3 plans (Topstep / Lucid / Select Daily) AND with +1 pt, AND the trade-level direction of the
rule holds on 2014-19 (AGAINST mean R < NONE for skip; AGREE > NONE for the other two). Three variants are tried; a single PASS is
reported with that count. A pass is a paper candidate only.

## Results (run 2026-10-23, after the text above)
Outputs: `research/programs/out/flag_confluence.txt` (matrices A, B), `out/flag_confluence_mtm.txt` (account); strategy trade lists from
`build-exp/edge_lab run --trades` on the four fixed specs (session scratchpad conf/).

**Matrix A (flags given a strategy signal earlier that day; 6 flag sets × 5 strategies).**
- A same-side signal from the cascade, gap retest or 10:00 gap rule goes with clearly better flag R in almost every cell — BOOK2 AGREE vs
  NONE: CAS +0.19 vs −0.05 (NQ 2014-19), GRT +0.42 vs −0.09, G10 +0.45 vs +0.02, ANY +0.22 vs −0.40; shuffle p ≤ 0.002 for every
  BOOK2 row except WKL. Same pattern on 2m-5m flags; weak on 1m.
- But the EDGE OVER TWINS mostly does not separate: e.g. BOOK2 ANY NQ 2014-19 edge +0.09 (agree) vs +0.11 (none). Random entries on the
  same days, after the flag, earn almost as much — the confluence measures that THE DAY is trending the strategy's way, not that the flag
  itself is better. Only 7 of 30 cells meet the pre-registered REAL rule (R and edge, both halves, both markets): 1m GRT, 2m GRT, 3m G10,
  4m CAS, 4m ANY, 5m ANY, BOOK2 CAS.
- The weakness long is the exception the other way: bull flags on a gap-down weakness day are WORSE than other flags in every cell
  (BOOK2 AGREE −0.19 vs NONE +0.28 on NQ 2020-26; p 0.999).
- On the CANDIDATE (which already filters on the 15m trend and sizes with the gap) 86 % of trades already have an ANY-agree signal:
  agree +0.24 / +0.40 R, none −0.27 / +0.02, against −0.58 / 0.00 (2014-19 / 2020-26).

**Matrix B (strategy trades given an earlier flag).** No consistent effect: the cascade after a same-side 1-5m flag is better in 3 of 4
cells (NQ 2014-19 +2.19 vs −0.19 pts, NQ 2020-26 +12.07 vs +8.38, ES 2020-26 +2.75 vs +0.49) but not ES 2014-19 (+0.16 vs +0.20); gap
retest and 10:00 rule mixed or too few. Nothing for the book.

**C — account (per eval, 2014-19 → 2020-25, conservative; % of the candidate's 2020-25):**
| variant | Topstep | Lucid | Select Daily | verdict |
|---|---|---|---|---|
| candidate | 1543 → 1648 (+1pt 1415) | 1084 → 1479 (1281) | 1251 → 1320 (1218) | — |
| CF_skipAgainst | 1582 → 1508 (1267) 92 % | 1107 → 1359 (1158) 92 % | 1307 → 1130 (1026) 86 % | FAIL |
| CF_agree2 | 1546 → 1156 (842) 70 % | 1024 → 825 (725) 56 % | 1145 → 902 (644) 68 % | FAIL (doubles 86 % of trades → too much risk for the $500 stop / $2K trail) |
| CF_onlyAgree | 1372 → 1547 (1430) 94 % | 1033 → 1373 (1293) 93 % | 1051 → 1240 (1038) 94 % | FAIL (fewer trades; the dropped ones were ~0 R in 2020-26) |
Read: confluence is real in the trades' R but is the day's direction, which the candidate already uses (15m trend filter + gap sizing);
it adds nothing to the account and doubling on it hurts. 0 of 3 variants pass. No change.
