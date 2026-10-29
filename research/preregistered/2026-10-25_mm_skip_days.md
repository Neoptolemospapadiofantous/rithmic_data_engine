# Market maker: are there days to skip? Regime filters on all data — pre-registered 2026-10-25

Founder: "add also our regime filters. Also test to see which days should we skip, if we need to skip any". Written BEFORE
any per-day result below was looked at. Setting = the frozen one (docs/market_making_system.md §5), pessimistic queue.

## Data
- History: Tardis Hyperliquid sample days (the 1st of each month 2024-11 → 2026-10), 30 tuned coins (`rithmic_dex/tardis`)
  and 15 never-tuned coins (`tardis_extra`), at Lighter fees, 300 ms — `build/mm_robust --dash` per-day P&L.
- Live recordings since 2026-10-25: Lighter (Hyperliquid tick rule, Lighter grid, `params_lighter.json`), our Hyperliquid
  recording. Partial days only so far; descriptive.

## H1 — skip weekends (the only day feature known before the day that varies on these sample days)
Day = UTC calendar day; weekend = Saturday or Sunday. On the TUNING days (before 2025-11-01, 30 tuned coins) compute
mean day P&L for weekdays and for weekend days. The rule "skip weekends" is ADOPTED only if (a) tuning weekend mean < 0
and weekday mean > 0, (b) on the HOLDOUT days (2025-11-01 → 2026-10-01) skipping weekends raises total P&L, and (c) the
15 never-tuned coins (all 24 days) agree in sign with (b). Otherwise no day is skipped. A mirror rule ("skip weekdays")
is judged the same way.
Per-weekday means and every single day are reported descriptively. With 24 sample days (≈ 7 weekend days) the test is
weak; a "no" here means no evidence, not proof that every day is fine.
Not testable on this data: news / macro days (only the 1st of each month is sampled, which is almost always the US ISM
day when it is a weekday — so "news day" and "weekday" are the same set), prior-day volatility (no prior day recorded).

## H2 — regime filters on all data
The pre-registered rule from 2026-10-25 (quote only while trades in the last 60 s ≤ 20 AND spread ≥ 2 bps,
`--allow activity=0,1 --allow spread=1,2,3`) and the AND of every feature's tuning-chosen buckets are run on every
dataset: tuned coins (holdout judged), never-tuned coins (every day holdout), the Hyperliquid recording and Lighter's own
books (`mm_regime --params params_lighter.json`, added today so Lighter runs in the strategy's tick unit; without
`--params` the tool's output is byte-identical to before). Descriptive for the live sets (partial days).

## Results (run 2026-10-25 21:40 Cyprus, after the text above; `data/mm_gates/alldata.json`)
**H1 — no day is skipped.** Mean sample-day P&L, tuned coins: tuning weekday +$71.7 (n=8), weekend +$17.6 (n=4); holdout
weekday +$39.0 (n=8), weekend +$33.5 (n=4). Never-tuned coins: weekday +$28.6 (n=8), weekend +$28.2 (n=4). Weekends are
positive in every set, so condition (a) fails for "skip weekends"; weekdays are positive too, so the mirror fails.
Descriptive, not a rule: the last four tuned-coin sample days (2026-07 … 2026-10) are −$18, +$13, −$37, −$22 and
2026-09-01 is the worst day in both coin sets (−$37 / −$41) — watch for a fading edge on the live days.
**H2 — regime filters.** Pre-registered rule vs no filter (holdout): tuned coins see the regime study (history row);
never-tuned +$344 vs +$341 (0.64 vs 0.60 bps, 83 % of the time); our Hyperliquid recording (partial day) +$19 vs +$19;
Lighter's own books (partial day, `--params`) −$26 (−7.21 bps) vs −$24 (−4.16 bps) — no filter rescues Lighter so far.
Check: `mm_regime` without `--params` byte-identical to before on tardis_extra; with `--params` the Lighter baseline
(−$24, 588 fills) matches the G1 replay (−$24.64, 558 fills, minutes later) within the extra recorded minutes.
