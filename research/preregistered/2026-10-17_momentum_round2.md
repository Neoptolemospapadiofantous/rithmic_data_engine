# 1-min momentum direction, round 2: second market, other sessions, exits (pre-registered 2026-10-17, before any run)

Founder: "lets optimize and do more research" after `2026-10-17_mtf_1m_dir_5m_entry.md` (frozen rule: the last 30 one-minute
moves went one way with efficiency ≥ 0.3, price on that side of the session VWAP and the session open, entry at a 5-min close,
stop 0.25 ATR, break-even at +1R, hold; ≤ 3 a day; done after a loss). The rule's window / threshold are NOT re-tuned here.
1. SECOND MARKET: the frozen rule on ES 1-minute 2010-2024 (cost 0.65 ES pts ≈ the same $ per micro as 1.6 NQ pts).
   Confirms only if positive in all three eras (2010-18 / 2019-22 / 2023-24) for the any-bar entry.
2. OTHER SESSIONS (NQ and ES): London (open 03:00, entries 03:15–07:00, flat 08:00), Asia (open 18:00, entries 18:15–01:00,
   flat 02:30), VWAP and open of that session. Reported, not adopted on their own.
3. EXITS (NQ, New York, any-bar entry): break-even at +0.5 / +1 / +1.5R then hold; 2R target; trail 0.25 ATR from the best price;
   break-even at +1R then trail 1R; time exits 60 / 120 min. Adoption only via walk-forward (each year the best exit of earlier years)
   beating the frozen exit, and positive in all eras.
4. SPLITS of the frozen NQ rule (descriptive): entry before / after 11:30; plan direction agrees / disagrees / not set.
2025-26 printed once per row for the record; only a confirmed rule's 2025-26 counts as its out-of-sample look.

## Results (2026-10-17; research/programs/mom2.cpp → out/mom2.txt) — R/trade 2010-24 (eras 10-18 / 19-22 / 23-24)
1. SECOND MARKET — NOT CONFIRMED. ES New York, frozen rule: any-bar −0.062 (−0.114 / +0.008 / +0.028), EMA-20 −0.063. The NQ
   result does not carry to ES, so it may be NQ-specific trend persistence or luck; it stays a forward-paper candidate on NQ only.
2. OTHER SESSIONS — negative everywhere: NQ London −0.11, NQ Asia −0.13, ES London −0.14, ES Asia −0.16 (any-bar). New York only.
3. EXITS (NQ NY, any-bar): frozen BE +1R hold +0.037 (all eras +); BE +1.5R +0.038 (all eras +); BE +0.5R −0.005; 2R −0.003;
   0.25-ATR trail −0.012; BE then 1R trail −0.009; time 60 / 120 min −0.039 / −0.008. Walk-forward picked the frozen exit in all 12
   years (+0.060 vs +0.060) — nothing to change. EMA-20 entry: +0.079 frozen, +0.101 with BE +1.5R (all eras +).
4. SPLITS (frozen, NQ NY): entries ≤ 11:30 +0.043 (all eras +, 2,808 of 3,271 trades) vs later +0.000; most trades come BEFORE the
   plan has a direction (+0.044, all eras +); when the plan direction already agrees +0.047 (2010-18 −0.001); disagrees −0.124 (n 149).
Reading: on NQ New York the momentum rule is an EARLIER read of the same trend day the cascade waits for (most entries 09:45–10:30,
before the 09:50/10:30 checks resolve). Keep it on NQ paper, New York only, frozen; do not trade it overnight or on ES.

## The real engine on every year (2026-10-17): build/paper_engine, config = the two fleet ids, label `mom_hist`
NQ 1-min bars 2010-07 → 2026-03-05 (188 Databento OHLCV files, 4 synthetic prints per bar, the fleet's measured costs:
commission $1.82 + 3 ticks slippage, stops fill at the crossing print), plus `backfill` = the collector's ticks 2026-09-17 → 10-16.
| id | 2010-18 | 2019-22 | 2023-24 | 2025-26 (to 03-05) | 09-17 → 10-16 (ticks) |
|---|---|---|---|---|---|
| mom_dir_30_any | −$1.33 / MNQ / trade (n 2,074) | +$10.84 (996) | +$1.39 (494) | +$18.42 (293) | 18 trades, +38.9 pts gross |
| mom_dir_30_ema20 | −$1.67 (612) | +$5.61 (380) | +$14.44 (154) | +$55.85 (85) | 5 trades, +56.9 pts gross |
Win rate 34–44 %, average hold ~3 h (most exits: stop / break-even stop, or flat 15:55). With the engine's costs the 2010-18 era
is slightly NEGATIVE (the research simulator, cost 1.6 pts flat, had it slightly positive) — the same era split as the cascade:
it earns in the volatile, trending years since 2019 and is about breakeven in quiet ones. Forward paper decides.
