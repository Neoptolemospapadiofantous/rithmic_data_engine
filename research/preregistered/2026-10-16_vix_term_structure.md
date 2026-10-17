# Pre-registration — VIX term structure as a mechanical filter (written 2026-10-16, BEFORE any run)

Founder: "what else can we add" / "more mechanical if we can". Owned free data: CBOE VIX and VIX3M daily closes
(data/regime). vts = VIX / VIX3M at the prior close (> 1 = backwardation: near-term fear priced above 3-month).
Why: in backwardation hedgers are paying up for protection NOW — forced de-risking flows dominate the session, which is
when the open's direction (cascade) and the oversold bounce (weak-day) should carry most.

## Family: 3 tests, controls at 99 % (0.05 / 3), 400 seeds
1. `cascade_ts_fear` — the cascade spec unchanged, every check also requires vts ≥ 1.
2. `cascade_ts_calm` — the cascade spec unchanged, every check also requires vts < 1 (the complement; reported, and
   counted, because a filter that works must show in one half and not the other).
3. `weak_day_ts` — weak close (pibs ≤ x, x ∈ {0.3, 0.5}) AND vts ≥ t, t ∈ {0.95, 1.0} → buy 09:30, flat 15:55, no stop
   (replaces the VIX ≥ 20 level gate with the term structure).
A filter is ADOPTED only if its spec passes every gate AND its out-of-sample net per trade beats the unfiltered
strategy's (+4.80 cascade / +13.20 weak-day) — otherwise the fleet keeps the current rules.

## Results (appended after the runs — tests unchanged)
| test | OOS trades | net pts/trade | verdict |
|---|---|---|---|
| cascade_ts_fear (vts ≥ 1) | 144 | −1.75 | FAIL walk-forward — the hypothesis was backwards |
| cascade_ts_calm (vts < 1) | 2,085 | +5.13 (vs +4.80 unfiltered) | FAIL controls: random day 97 % (need 99 %); monthly corr 0.86 with the cascade |
| weak_day_ts | 222 | +25.59 | FAIL controls: random direction 94 %; 2024 −912; monthly corr 0.80 with weak_day_long |
No filter adopted; the fleet keeps its rules. Note for the forward data: the cascade has been flat-to-negative on the few
backwardation days (144 trades — not enough to act on).
