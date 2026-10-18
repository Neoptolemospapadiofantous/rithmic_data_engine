# Pre-registration — 1-minute direction, then BOS / CHoCH, then an EMA pullback (written 2026-10-18, BEFORE any run)

Founder: "so we get direction on the 1m and then we see BOS/CHO ema pullback?"

## Definitions (NQ, NY 10:30–14:00 scan on completed 5-minute bars, fill next bar's open, flat 15:55, one trade a day)
- 1-minute DIRECTION D: the last 45 one-minute closes moved fairly straight (efficiency er(1,45) ≥ 0.3); D = sign(cl(1,1) − cl(1,46));
  price on D's side of VWAP and of the 09:30 open (the tested momentum definition, mtf_1m_dir_5m_entry / momentum_entries).
- BOS in D: last hour's high above the prior 2 hours' high and last hour's low not below the prior 2 hours' low (mirror for D < 0).
- CHoCH in D: the PRIOR structure was against D (the 2 hours before the last hour made a lower high than the 2 hours before
  those — lag(60, hh(5,24)) < lag(180, hh(5,24))) and the last hour broke that prior 2-hour high (mirror for D < 0).
- EMA pullback: the completed 5-minute bar trades through EMA(E) of 5-minute closes and closes back on D's side.
- Stop stopk × ATR, target 2R. Grid E ∈ {9, 20}, stopk ∈ {0.2, 0.3}. 2010-07 → 2024-12, ES confirm, 1.0 pt cost;
  2025-01 → 2026-10-16 holdout judged ONCE at the frozen setting.

## Tests (family of 4 → controls at 99 %, 400 seeds)
1. `m1dir_ema` — D + EMA pullback, no structure (reference: what structure adds).
2. `m1dir_bos_ema` — D + BOS in D + EMA pullback.
3. `m1dir_choch_ema` — D + CHoCH in D + EMA pullback.
4. `m1dir_bos_ema_gap` — test 2 only when |gap| ≥ 0.1 ATR on D's side.

## Adoption
Forward-paper candidate only if it passes every gate AND its frozen 2025+ holdout is positive AND (for 2–4) it beats test 1
per trade walk-forward. Otherwise a FAIL.

## Results (appended 2026-10-18 after the runs — tests unchanged)
| test | walk-forward net/trade (trades, /yr) | verdict | 2025+ holdout, frozen EMA-9 / stop 0.3 ATR (info) |
|---|---|---|---|
| m1dir_ema (no structure) | +4.71 (318, 19/yr) | FAIL controls (random direction 92 %) | +27.61 on 44, t 1.5 |
| m1dir_bos_ema | **+10.81 (215, 13/yr, win 54 %, PF 1.64, 11/14 years +)** | FAIL controls (random direction 97 % vs 99 %) | **−7.73 on 25 (PF 0.84)** |
| m1dir_choch_ema | +8.15 (53, 3/yr) | FAIL screen (too few trades), latest era negative | — |
| m1dir_bos_ema_gap | +20.22 (53, 3/yr) | FAIL screen (too few trades) | — |
Verdict: nothing adopted. The BOS layer looked like it doubled the per-trade result 2010–24 but fails the direction control and
LOSES on 2025+; CHoCH is too rare to judge (3 a year). The 1-minute direction itself remains the useful part — it is already
on paper with the VWAP-pullback entry (mom_dir_45_vwap_plan), which tested better than any EMA entry.
