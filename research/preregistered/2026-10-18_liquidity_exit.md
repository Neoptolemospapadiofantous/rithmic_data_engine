# Pre-registration — liquidity as the EXIT (written 2026-10-18, BEFORE any run)

Founder: "What about liquidity — can we mark it or spot it and test it". Liquidity as an ENTRY is already tested and fails
(2026-10-16 liquidity boxes: sweep & break of equal highs/lows, Asia/London ranges, value area, naked POC, VWAP bands, week/month,
round numbers, with and without order flow; 2026-10-17 break-box-go traps; 2026-10-18 structure sweeps). Untested: taking profit
AT the nearest liquidity pool instead of a fixed R target.

## Pools (known at entry, the page's levels): prior-day high/low, overnight (18:00–09:29) high/low, the 15-minute range high/low
(from 09:45), and every UNBROKEN 5-minute swing high/low since 18:00 (2 bars each side, confirmed 2 bars later).

## Exit rule `liq` (meta.hpp ExitSim): target = the nearest pool in the trade's direction at least 1R beyond the entry (R = the idea's
stop), filled 2 ticks before the pool (resting orders sit on it); no pool ≥ 1R → the idea's own target. Stop unchanged. `liq_run` =
the same with NO fallback target (hold to 15:55 when no pool) — a runner variant.
## Control: a TARGET-DISTANCE twin — the rule's own target distances (in each day's ATR) shuffled across trades, everything else the same:
does the target at the LEVEL beat targets of the same sizes placed without it? ≥ 99 % of 200 seeds (eleven rules in the family), plus
the exits gates (better per trade, better in ≥ 2 of 3 eras incl. the latest, higher prop break-even fee).
Bases: so_cascade, gap_or_retest, mom_scan_3_pb, union_2r. Verdict per base; nothing tuned after seeing results.

## Results (appended 2026-10-18 — rules unchanged; `scripts/edge_lab.sh exits`, reports data/edge_lab/reports/<id>__exits.md)
| base | base 2R net (total pts) | liq | liq_run | target-distance twin beaten | verdict |
|---|---|---|---|---|---|
| so_cascade | +11,139 (+4.80/trade) | +10,452 | +10,675 | 29 % / 19 % | FAIL — not better than base |
| gap_or_retest | +11,015 (+9.03) | +10,706 | +10,713 | 89 % / 92.5 % (need 99) | FAIL — timing/target control |
| mom_scan_3_pb | +6,499 (+3.09) | +5,621 | +6,882 | 26 % / 24 % | FAIL — not better than base |
| union_2r | +9,937 (+4.01) | +8,759 | +9,835 | 43.5 % / 43.5 % | FAIL — not better than base |
Verdict: taking profit at the nearest liquidity pool is never better than the fixed 2R target, and where it comes close (gap retest)
targets of the same sizes placed WITHOUT the level do as well 89–92 % of the time — the pool is not where the move stops.
Liquidity now fails as entry (2026-10-16/17) and as exit (today); on the page it stays a drawn level, not a signal.
