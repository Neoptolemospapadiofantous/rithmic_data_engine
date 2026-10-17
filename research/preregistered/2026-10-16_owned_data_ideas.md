# Pre-registration — ideas testable on owned data (written 2026-10-16, BEFORE any run)

Founder: "test all. I cant have more expenses." Everything here uses data already owned (NQ/ES 1m 2010-26, Binance
BTC/ETH 1m 2020-26). Not tested: other markets (needs a purchase), big-tech earnings days and CPI/auction days (no dates
in the database; scraping 16 years of free calendars is not reliable enough to pre-register on).

## Family: 9 signal tests → every control must clear 99.5 % of 400 seeds (0.05 / 9). Max 8 variants per test.
1. `cta_cross_1800` — trend-follower trigger: yesterday's close crossed the 50-, 100- or 200-day average of RTH closes
   (net direction of the crosses) → enter 18:00 in that direction, flat 15:55 next day (one trading day). stop k × ATR,
   k ∈ {0 (none), 0.5}.
2. `cta_cross_0930` — same signal, enter 09:30, flat 15:55. k ∈ {0, 0.5}.
3. `cascade_fail_reverse` — the 09:50 cascade signal (|px − open| > 0.25 ATR) whose 0.25-ATR stop is then hit
   (by 12:00) → trade the OTHER way from the bar after the stop; stop k ∈ {0.15, 0.25} ATR, target 2R, flat 15:55.
4. `nq_es_diverge` — every completed 5-minute bar 10:00–15:00: d = NQ (px − open)/ATR − ES (px − open)/ES ATR at the
   same minute; |d| > z → trade NQ toward ES (−sign d); z ∈ {0.3, 0.5}, stop k ∈ {0.15, 0.25} ATR, target 1.5R.
5. `btc_weekend_monday` — Mondays: BTC return from Friday's 16:00 NQ close to the decision time; |r| > z → trade NQ in
   r's direction at 09:30, flat 15:55; z ∈ {2 %, 4 %}, stop k ∈ {0, 0.5} ATR.
6. `btc_weekend_sunday` — same signal at the Sunday 18:00 open, flat 09:25 Monday; z ∈ {2 %, 4 %}, k ∈ {0, 0.5}.
7. `btc_cascade`, 8. `eth_cascade` — the NQ cascade spec unchanged on BTC / ETH 1m bars (US RTH 09:30–16:00 ET as the
   session), prices on a log scale (1 point = 1 basis point) with a 10 bp round trip cost (taker fees + slippage).
   No prop gate (no micro on our prop firm); judged on gates 1–4.
9. (sizing, not a signal) `vol_regime_sizing` — cascade + VWAP/stall exit in the payout simulator (Select Daily 50K,
   monthly withdrawal, 2019–26): 2 MNQ normally, 4 MNQ when the prior day's ATR14 is above its 60-day average, vs fixed
   2 and fixed 4. Passes if it pays in more months than fixed 2 AND more $/yr than fixed 2 AND blows no more evals/yr
   than fixed 4.

## Pass
All edge_lab gates (controls 99.5 %). A pass becomes a working-fleet candidate (forward test), never live directly.

## Results (appended after the runs — tests unchanged)
| # | test | OOS trades | net pts/trade | verdict |
|---|---|---|---|---|
| 1 | cta_cross_1800 | 214 | +7.37 | FAIL controls (direction 75 %); 2010–18 −1.22, whole-period best t 0.66 |
| 2 | cta_cross_0930 | 174 | −1.11 | FAIL screen |
| 3 | cascade_fail_reverse | 255 | +5.47 | FAIL controls (direction 92 %); 2023–26 −5.05; monthly corr −0.21 with the cascade |
| 4 | nq_es_diverge | 343 | −2.44 | FAIL screen |
| 5 | btc_weekend_monday | 86 | +8.09 | FAIL controls (90 %); 2023–26 −7.00 |
| 6 | btc_weekend_sunday | 25 | −3.05 | FAIL walk-forward |
| 7 | btc_cascade (10 bp cost) | 772 | −2.93 bp | FAIL walk-forward |
| 8 | eth_cascade (10 bp cost) | 837 | −3.11 bp | FAIL walk-forward |
| 9 | vol_regime_sizing (sim, monthly withdrawal) | — | — | FAIL: payout months 26 % vs fixed 2 MNQ 49 % / fixed 4 60 %; evals/yr 2.3 vs 0.4 / 0.8 |
Verdict: nothing passes. Sizing up into high-ATR days blows more evaluations, not fewer.
