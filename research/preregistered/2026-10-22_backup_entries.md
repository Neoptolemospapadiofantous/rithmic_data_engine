# Backup entries + ideas from an online search — pre-registered 2026-10-22

Founder: "yes" (take the engulfing backup forward + remaining ideas), then "do an online search to find possible ideas on how to read
the market and handle bear flags and bull flags". Written BEFORE any run. Program: flag_engulf.cpp `backup` mode; account =
`flag_prop_ev … mtm` (1 micro each market, 2 open, $500 stop).

Online ideas already covered by earlier tests (not re-run): flag volume dry-up, counter-trend flag slope (our data: drift WITH the pole
is better), tight vs loose (only visible after entry), break entry vs inside-the-flag entry, high-and-tight (shrinks out of sample),
first-15-minutes filter (built in), delta / absorption confirmation, scale-out / trailing exits.

New baseline = the candidate: BOOK2 + EB backup (engulfing entry when the flag-mid limit never filled).
Backups (all only on BOOK2 flags whose limit never filled, page conventions: entry before 15:00, flat at the 15:55 open, risk > 0.5 pt,
stop = lowest low since the pole top − 0.1 ATR, target pole top + pole, cost NQ 1.5 / ES 0.5 (+0.25 pt slippage on stop entries)):
- B1 BRK: buy stop one tick past the pole top (book3 rule)
- B2 BRKVOL: B1 only if the breakout minute's volume >= 1.5 × the flag's average 1m volume (online: "breakout volume +50 %")
- B3 H2 (Al Brooks High 2): on the flag's chart, the second bar after the pole top whose high exceeds the previous bar's high, the
  previous bar's high still below the pole top; buy stop one tick above that previous high (filled at max(trigger, minute open)),
  only at / after registration
- B4 EB→BRK: the engulfing entry, else the breakout, whichever triggers first
- B5 H2 or EB, whichever first
- C1 trend context (online: "bull flags fail in bear markets"): on the candidate, take bull flags only when the prior close is above the
  200-day average and bear flags only below (DT200, re-tested because its earlier verdict used the biased account model)
**PASS** (vs the candidate): conservative marking, on at least two of topstep_50k / lucid_flex_50k / select_daily_50k: 2014-19 EV >=
candidate + $100 AND 2020-25 EV >= candidate; optimistic marking not below the candidate. Backup trades also reported as R and edge
over post-registration twins by half and market.

## Results (2026-10-22; out/flag_engulf.txt, backup_entries_mtm.txt)

**A BUG FOUND BY THE /flags PAGE CHECK CHANGES THE CANDIDATE.** The wide15 export's ok = 0 covers two cases: the flag-mid limit never
filled, or it filled while that chart already held a trade (skipped, one position per chart). Every backup in book_more (T1, BOOK3)
and in this file's first run treated ok = 0 as "never filled", so backups also traded flags whose limit HAD filled. flag_engulf.cpp
now rebuilds the page's fill rule (`midFill`) and only trades a backup where the limit truly never filled; the page check
(bot/frontend/scripts/flag-parity/book-eb.js) then matches exactly (NQ + ES 383-384 days each: 0 only-page, 0 only-research, no field
differences).

Corrected backups on BOOK2 (Topstep 50K, conservative, 2014-19 → 2020-25; baseline $1,029 → $971): B0 EB $1,029 → $972 (3 trades in 16
years — the engulfing backup's earlier "pass" was the bug: its trades were second trades on busy charts), B1 BRK $988 → $1,072, B2
BRKVOL $1,031 → $975, B3 H2 $1,027 → $1,085, B4 = B1, B5 H2 or EB $1,027 → $1,088. None passes (2014-19 needs +$100). H2 has the best
trade-level edge (NQ +0.19 / +0.13, ES +0.13 / +0.04 R over twins, 534 trades) — a hypothesis, not a rule. C1 DT200 FAILS ($707 → $201
under the first run; it removes about half the trades). Separate accounts for WEAK DAY / MONDAY / CASCADE: $3-557 per eval (too few
trades to reach $3,000 in 60 days at 1-3 micros) — FAIL.

CANDIDATE = BOOK2's flags on the corrected model: 1 micro each market, 2 open per market, $500 daily stop (≈ $1,000 per Topstep / Lucid
Flex 50K eval before fees and split). Page: /flags book "CANDIDATE" (lib/flags.ts BOOK_CFG.CAND); its minute-by-minute daily stop
matches flag_prop_ev mtm day for day on the 2026 days both parity files share (9 / 9 identical in points; BOOK2 at $150: 8 stop days,
page −$150 vs research −$151…−$157 = the research's flattening slippage).

Accidental finding, NOT pre-registered and found on the full history (so only a hypothesis for the paper test): letting a second trade
in on a chart that already holds one (the buggy "EB backup") added ≈ +$100-200 per eval in both halves.
