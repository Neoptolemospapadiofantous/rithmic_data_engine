# Exit realism, news days, how sure we are — pre-registered 2026-10-23

Founder: "doc and lets do more tests". Written BEFORE any run. Candidate = BOOK2 + gap sizing + the 15m trend filter
(htf_context FILT_TREND_15m files). Program: `research/programs/flag_realism.cpp` (extended). Account = `flag_prop_ev … mtm`
(2 open per market, $500 daily stop), Topstep / Lucid / Select Daily 50K, 2014-19 → 2020-25 per eval, conservative marks, +1 pt.

## G1 — Exits as they would fill live
The backtest fills the measured-move TARGET the minute price touches it and fills STOPS exactly at the stop price. Live, a resting
target limit needs price to trade through it (queue), and a stop market order slips in a fast move. Variants (1m re-simulation, same
conventions as F1):
- G1tgt: target fills only when the 1m high is >= target + 1 tick (else the trade keeps running to the stop / 15:55).
- G1slip: stop exits fill 2 ticks worse than the stop (0.50 pt on both markets).
- G1all: entry needs 1 tick through (F1) + G1tgt + G1slip — the pessimistic live picture.
Check: the unchanged simulation still reproduces all candidate trades. Read, no pass/fail: G1all keeping >= 70 % of the candidate's
2020-25 EV on 2 of 3 plans = the edge survives realistic fills; below that, the live EV must be quoted from G1all.

## G2 — How sure are the numbers (descriptive)
From the candidate's daily P&L (mtm, conservative): mean daily P&L and a 90 % interval by a monthly block bootstrap (2,000 draws) for
2014-19, 2020-24 and 2025-26 (2025-26 = the most recent stretch, shown as its own period); per-eval EV by start year (min / median / max).

## G3 — News days
FOMC (14:00 ET decision), CPI and NFP (08:30 ET). Trade level: candidate mean R on those days vs other days, by half (2014-19 /
2020-26). Variants: G3fomc = skip every candidate trade on FOMC days; G3fomcpm = skip FOMC-day entries from 13:30 on; G3data = skip
CPI and NFP days. A variant PASSES only if the skipped trades' mean R is < 0 on 2014-19 AND the account is >= the candidate on 2020-25
on 2 of 3 plans and with +1 pt.

## Results (run 2026-10-23, after the text above)
Outputs: `research/programs/out/flag_realism.txt` (trade level incl. G3 table), `out/flag_exits_news_mtm.txt` (account). Per eval,
2014-19 → 2020-25, conservative; % = 2020-25 vs the candidate. Check: the unchanged simulation reproduces 3,837 / 3,837 trades.

| variant | Topstep | Lucid | Select Daily |
|---|---|---|---|
| candidate | 1543 → 1648 (+1pt 1415) | 1084 → 1479 (1281) | 1251 → 1320 (1218) |
| G1tgt target needs 1 tick through | 1410 → 1629 (1397) 99 % | 995 → 1464 (1264) 99 % | 1292 → 1313 (1212) 99 % |
| G1slip stops slip 2 ticks | 1434 → 1512 (1320) 92 % | 1029 → 1376 (1169) 93 % | 1131 → 1273 (1064) 96 % |
| G1all entry 1 tick + target 1 tick + stop slip 2 | 1091 → 1557 (1306) 94 % | 808 → 1398 (1187) 95 % | 1132 → 1299 (1051) 98 % |
| G3fomc skip FOMC days | 1475 → 1578 (1405) 96 % | 1061 → 1436 (1248) 97 % | 1051 → 1312 (1181) 99 % |
| G3fomcpm skip FOMC entries >= 13:30 | 1543 → 1632 (1400) 99 % | 1084 → 1464 (1266) 99 % | 1251 → 1317 (1214) 100 % |
| G3data skip CPI / NFP days | 1444 → 1556 (1424) 94 % | 1017 → 1418 (1248) 96 % | 969 → 1217 (1092) 92 % |

**G1:** the pessimistic live picture keeps 94-98 % of the 2020-25 EV (>= 70 % on all 3 plans) → the edge survives realistic fills. But
2014-19 falls 10-30 % (Topstep 1543 → 1091, Lucid 1084 → 808), so quote live expectations from G1all, not the headline: roughly
$1,050-1,560 per eval depending on plan and period. Stop slippage is the biggest single cost.
**G3:** FOMC-day trades were negative on 2014-19 (pooled −0.83 R on 34) but positive 2020-26; skipping them lowers the account on all
3 plans → FAIL. FOMC entries after 13:30: only 3 trades — nothing to skip. CPI/NFP days are not negative on 2014-19 (+0.13 R) → FAIL by
the trade-level rule, and the account is lower too. Trade every day.
**G2 (daily P&L of the candidate, conservative marks, days with at least one trade; 90 % interval = monthly block bootstrap):**
| period | days | mean $/day | 90 % interval | total | months positive |
|---|---|---|---|---|---|
| 2014-19 | 720 | $93.5 | $51 … $140 | $67,320 | 45 / 71 |
| 2020-24 | 586 | $109.2 | $67 … $152 | $63,989 | 43 / 60 |
| 2025-26 | 143 | $74.8 | $10 … $152 | $10,693 | 11 / 16 |
The most recent stretch (2025-26; NQ data has a hole 2026-03-20 … 09-18) is still positive but weaker and its interval nearly reaches
zero — consistent with the earlier years, not proof on its own. Per-eval EV by start year was not produced (the mtm mode does not split
by start year); not done rather than estimated.
