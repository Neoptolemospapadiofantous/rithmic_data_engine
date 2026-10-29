# Flag book — state of the research (2026-10-22)

The NQ + ES flag strategy, what it earns on prop accounts after the corrections of 2026-10-22, how every number was validated, and
what is still open. Research only: nothing here trades live. Details per test are in `research/preregistered/2026-10-22_*.md`
and the rows of `research/README.md`; outputs in `research/programs/out/`.

## The strategy (candidate)

| Part | Rule |
|---|---|
| Flags | BOOK2: 4-5m charts, NQ + ES, pole >= 3× the average bar, pullback 5-38 %, grade A or B (14-feature walk-forward score) |
| Entry / exit | limit at the flag's middle; stop 0.1 ATR beyond the flag; target the measured move; flat 15:55; no early exit |
| Size | 1 micro each market; **2 when the flag points the same way as the 09:30 gap** (vs the prior RTH close) |
| Filter | **skip flags whose 15m trend at the pole's START is against them** (close of the last completed 15m bar vs its EMA20) |
| Account | at most 2 open trades per market; own daily stop $500, marked minute by minute |

Trade level (2014-26): win rate 31-32 % (37-38 % with the gap, 26-27 % against), average winner +2.8R, +0.22R a trade at today's
cost-to-range (+0.08R at historical costs), better than random entries in the hour after each flag.

## Prop account results (corrected model, `flag_prop_ev … mtm`, before eval fees and the firm's split)

Per eval bought, conservative marking, 2014-19 → 2020-25:

| Version | Topstep 50K | Lucid Flex 50K | Select Daily 50K | +1 pt cost (Topstep) |
|---|---|---|---|---|
| Flat 1 micro | $1,029 → $971 | $998 → $970 | $964 → $837 | $792 |
| + gap sizing | $1,502 → $1,540 | $1,007 → $1,336 | $1,237 → $1,204 | $1,174 |
| **+ gap sizing + 15m trend filter** | **$1,543 → $1,648** | **$1,084 → $1,479** | **$1,251 → $1,320** | **$1,411** |

Candidate with the filter: pass ≈ 46 %, ≈ 32 % of evals ever paid (Topstep / Lucid), daily mean $55 / sd $472, worst drawdown $7,358
(one account trading every day, no prop rules), 48 losing months of 147. Other firms (gap sizing): Apex EOD 50K $931, MFFU Builder 50K
$916, Builder 25K $213; intraday-trailing plans earn about a third (Apex intraday $324). Bigger size, full port and 2 minis lose on
every plan; 5 × $50 Builder 25K accounts at 1 micro started a month apart: 46 % chance one pays >= $300 after the split.

## Corrections made on 2026-10-22 (what was wrong before)
1. **Account model look-ahead** — flag_prop_ev / payout_sim checked the daily stop and drawdown one trade at a time with earlier trades
   at their FINAL P&L while still open. BOOK2 "$4,593 per eval" became $124-1,405. Fixed by `mtm` (all open trades marked each minute).
2. **"No trade" ≠ "never filled"** — the wide15 export's ok = 0 also covers fills skipped because the chart was busy; every backup-entry
   test (breakout, engulfing, BOOK3) was inflated by it. Fixed by rebuilding the fill (flag_engulf.cpp `midFill`); the backups add nothing.
3. **Circular higher-timeframe test** — trend read at entry includes the 5m pole itself. Measured at the pole's start it matters (15m
   trend filter passes; 15m / 30m / 1h trend, 1h / daily nesting, target beyond the 1h / 4h range beat 1,000 shuffles).
4. **Disk full** — my research CSVs filled the root disk; the collector logged write failures at 22:02 and 22:09 Cyprus (ticks re-queued,
   a few BBO rows may be missing). Space restored at 22:11. The root disk remains ~100 % full (7 GB free).

## Validation (re-run 2026-10-22, after the disk was freed)
| Check | Result |
|---|---|
| Every research program from today rebuilt from the repo and re-run | 6 / 6 outputs byte-identical to the saved ones |
| Variant files complete (line counts vs the export) | all complete |
| Account numbers re-run (flat / gap / gap + filter, +1 pt) | identical to the reported figures |
| mtm vs the old model where they must agree (no daily stop; no overlapping trades) | $177,050 = $177,050; 1,052 = 1,052 stopped days |
| Old model reproduced (for the record) | BOOK2 $4,593 |
| /flags page vs research: engulfing entries (NQ + ES, ~384 days each) | 0 differences |
| /flags CANDIDATE book vs research, day by day (2026 days in both files) | 9 / 9 identical |
| TypeScript + lint (flags page) | pass |
| Pre-registrations written before their first result | 8 / 8 |
| C++ gate (`make hermes`) | not run during RTH — run after 16:05 ET |

## Tests of 2026-10-22 (all pre-registered)
| Round | Pass | Fail |
|---|---|---|
| Pole × flag types, shape models, order flow | — | grid, shape GBM, path kNN, order flow |
| Eleven account ideas (old model) | breakout backup, 3m — both VOID (corrections 1-2) | re-entry, cascade / weak day / Monday, size mixes, news days, book imbalance |
| BOOK3 confirmation | VOID (corrections 1-2) | — |
| Optimisation on mtm + engulfing | engulfing backup — WITHDRAWN (correction 2) | caps, cut-offs, A only, one market, profit lock, loss stop, size splits, 100K/150K, engulfing entry / filter |
| Backup entries + online ideas | — | breakout, breakout + volume, Brooks High 2, 200-day trend, separate accounts |
| Regime sizing | **gap sizing** | volatility, prior week, 15m + daily EMA (published flag rule), noise band, scores |
| Context sizing | open in the overnight top third, open beyond yesterday (same family as the gap) | room, 20-day, opening drive, week, 1h / 4h / 15m / 30m trend at entry (circular), stacking 3 micros |
| HTF context done properly | **15m trend filter at the pole's start** | 1h range filter (2014-19 worse), nesting as a rule, daily trend / room, sizing by HTF |

About 120 variants were tried today; passes are judged on 2020-25 data that the choices never saw, but some shrinkage live is expected.

## What runs where
- `/flags` (bot/frontend): CANDIDATE book = gap sizing + mtm daily stop (checked day for day); **the 15m trend filter is not on the page yet**.
- Not in the paper engine or the executor. Going live is the founder's decision.

## Open
Firm rules and fees to confirm (Topstep's reported 10-day first-payout rule; Builder assumptions); the 15m filter on /flags + parity;
a forward paper scorecard; `make hermes` after the close; freeing disk space (founder's call — nothing is deleted without asking).

## Overfitting check and paper ideas (2026-10-23)
- PBO (CSCV, 16 blocks, 12,870 splits) over all 95 variants tried on the corrected model: **0.187**. Deflated Sharpe of the candidate: **1.000**
  (daily SR 0.131, rank 1 of 95, expected max of 95 random trials 0.045). The candidate choice is unlikely to be luck.
- Absorption at the flag low (MBO proxy), quarterly re-fit of stop/target, best static stop/target and a kernel-regression second detector: all FAIL.
  Details: `research/preregistered/2026-10-22_external_ideas.md`.

## Live realism and robustness (2026-10-23)
- Limit fills: requiring price to trade 1 tick THROUGH the flag-mid limit keeps 97-102 % of the 2020-25 EV (2 ticks: 90-100 %). Only 1-5 %
  of trades are touch-only. A resting limit is fine live.
- Skipping the last entry hour or the weakest weekdays: both FAIL. Trade every hour and every weekday.
- Rule plateau: pole 2.5/3/3.5x × pullback max 30/38/50 % — all nine positive, neighbours 70-142 % of BOOK2, best cell flips between halves.
  No cliff, but the spread (wider than the pre-registered ±25 %) shows each cell is noisy; do not re-tune. Details: `research/preregistered/2026-10-23_realism_plateau.md`.
- Exits as they would fill live (target needs 1 tick through, stops slip 2 ticks, entry 1 tick through): 94-98 % of the 2020-25 EV, but
  2014-19 drops 10-30 %. Quote live expectations from this pessimistic version (~$1,050-1,560 per eval). Skipping FOMC / CPI / NFP days
  FAILS. Daily P&L 90 % intervals: 2014-19 $51-140, 2020-24 $67-152, 2025-26 $10-152 per trading day with a trade.
  Details: `research/preregistered/2026-10-23_exits_news_uncertainty.md`.
- Confluence with our strategies (cascade, gap retest, 10:00 gap rule, weakness long): flags on the same side as a strategy that already
  fired are much better in R, but random entries on those days are nearly as good — it is the day's direction, which the candidate
  already uses. Skip-against, 2× on agree and only-agree all FAIL on the account. Bull flags on weakness-long days are worse.
  Details: `research/preregistered/2026-10-23_flag_confluence.md`.
- Live planning (pessimistic fills): EV per 50K eval by start year has a median of ~$1.0-1.6K; one start-year in four (2016, 2018, 2019)
  paid nothing. Five accounts started ~20 trading days apart: 47-57 % chance at least one pays >= $300. No decay (2014-26 slope +0.014 R/yr,
  last 12 months at the 65th percentile). Daily stop: $1,000 beats $500 under live fills on all 3 plans (PASS by the pre-registered rule;
  keep it a little under each plan's own daily limit). Details: `research/preregistered/2026-10-23_live_planning.md`.
