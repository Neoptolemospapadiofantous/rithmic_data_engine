# Faster payouts without sizing: more streams per account, and a firm that pays sooner — pre-registered 2026-10-25

Founder: "too slow" (6 separate Lucid Pro 50K accounts, 2 weeks apart: median first payout 89 sessions). Written BEFORE the run.
Program: `research/programs/rotation_groups.cpp` extended (env RG_STREAMS, RG_FIRM; defaults reproduce 2026-10-24 exactly).

Streams per account (every account trades every stream, same direction everywhere, 1 MNQ / 1 MES units):
- F  = the flag candidate with live fills (as 2026-10-24).
- F+C = F + the cascade on NQ (so_cascade k1 0.25, k2 0.30, stop 0.35 ATR, 2R; edge_lab --trades, fixed setting).
- F+B = F + the cascade + the gap retest + the 10:00 gap rule on NQ (book F, fixed settings from 2026-10-23_flag_confluence).
Strategy trades: points scaled to today's NQ ATR like the flags, cost 1.5 pts, $2 per point (1 MNQ), skipped on days outside the flag
calendar (roll window). Opposite positions inside one account are allowed (they net); nothing is held across accounts in opposite directions.
Firms (rules as recorded 2026-10-23):
- Lucid Pro 50K: as 2026-10-24 (eval +$3,000, $2,000 EOD trail, one-day pass; funded paid at profit ≥ $2,600 with best day ≤ 40 %, buffer $2,100,
  paid min($2,000, profit − $2,100) × 90 %).
- Lucid Flex 50K: eval +$3,000, $2,000 EOD trail, best day ≤ 40 % of profit at the pass, ≥ 3 days; funded: first payout once 5 days of
  ≥ $150 and the balance ≥ +$1,000 (so that 50 % ≥ the $500 minimum), paid min($2,000, 50 % of profit) × 90 %; funded trail $2,000 EOD
  locking at +$100 once +$2,100 (ASSUMED, as for Pro).
Accounts: 4 and 6 separate accounts started 10 sessions (2 weeks) apart; daily stop $1,000 per account; 160 sessions each; programs every
5th session 2014-01 → 2025-04. Reported as 2026-10-24 (P(paid), within 30 / 45 / 60 / 90, median first payout, paid per program, net).
Read (descriptive, the founder picks): the setup with the shortest median first payout AND P(paid) at least that of 6 × Lucid Pro flags-only
(53.5 %) is the recommended one.

## Results (run 2026-10-25, after the text above; `research/programs/out/speed_streams_firms.txt`)
Check: with no env set the program reproduces the 2026-10-24 output line for line. Strategy trades on the flag calendar: cascade 1,751,
gap retest 1,333, 10:00 gap rule 435 (2014-01 → 2026-10, roll-window days skipped).
| firm | streams | accounts (2 wk apart) | P(paid) | ≤ 30 / 45 / 60 / 90 sessions | median 1st payout | paid / program | net | 2020-25 P(paid) / net |
|---|---|---|---|---|---|---|---|---|
| Lucid Pro | flags | 4 / 6 | 46.1 / 53.5 % | 1 / 4 / 11 / 28 % | 84 / 89 | 1.35 / 2.03 | +$2,054 / +$3,093 | 59.4 / 69.3 % |
| Lucid Pro | flags + cascade | 4 / 6 | 38.4 / 46.1 % | 4 / 9 / 14 / 28-29 % | 72 / 78 | 0.97 / 1.45 | +$1,488 / +$2,221 | 49.8 / 60.4 % |
| **Lucid Pro** | **flags + book F** | 4 / **6** | 45.1 / **55.2 %** | 5 / 13 / 22 / 39-43 % | 62 / **68** | 1.15 / 1.72 | +$1,871 / **+$2,796** | 58.1 / **70.2 %** |
| Lucid Flex | flags | 4 / 6 | 43.2 / 49.5 % | 0 / 4 / 13 / 33-34 % | 71 / 76 | 1.29 / 1.91 | +$1,319 / +$1,939 | 55.5 / 63.6 % |
| Lucid Flex | flags + cascade | 4 / 6 | 39.0 / 46.9 % | 3 / 13 / 22 / 35-38 % | 56 / 62 | 1.00 / 1.49 | +$925 / +$1,367 | 46.7 / 56.9 % |
| Lucid Flex | flags + book F | 4 / 6 | 38.0 / 46.1 % | 3 / 15 / 25 / 37-40 % | 52 / 57 | 0.96 / 1.43 | +$1,042 / +$1,542 | 52.0 / 63.1 % |
Read by the pre-registered rule (shortest median first payout with P(paid) ≥ 53.5 %): **6 Lucid Pro accounts, 2 weeks apart, each trading
the flags + the cascade + the gap retest + the 10:00 gap rule at 1 micro** — median first payout 68 sessions (vs 89), paid within 60
sessions 22 % (vs 11 %), P(paid) 55.2 % (70 % on 2020-25 starts), net +$2,796. Adding the cascade ALONE makes it faster but riskier (more
accounts blow); adding all of book F diversifies enough to keep the odds. Lucid Flex is faster still (median 52-57) but pays less often
(46 %) and half as much — it fails the odds condition. Caveats: the book-F settings were chosen on this same history (in-sample); the Flex
funded trail rule is assumed; the cascade and flags can hold opposite NQ positions in one account (they net).
