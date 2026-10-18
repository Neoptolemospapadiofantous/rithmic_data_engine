# Pre-registration — can Rithmic order flow give a better direction? (written 2026-10-18, BEFORE any run)

Founder: "Direction check if rithmic data can help us with a better direction. and test".

## What Rithmic gives us live (and so the only inputs allowed)
Trades with the aggressor side (buy / sell volume → delta) and top-of-book quotes (BBO). NOT market depth (L2 not
entitled), so the archived 10-bps book fields (`bk`) are excluded — they could not be traded live.
History with aggressor side: Databento MBO 2023-03 → 2026-03-19 (same field the collector records). Development = that
window; the collector's own sessions 2026-09-18 → 2026-10-16 are a forward look at the frozen setting (≈ 20 days, info only).

## The question as a trade
At a fixed decision time take the direction a source gives, enter at the next bar, stop stopk × ATR, target 2R, flat 15:55,
one trade a day. A better direction source = more net per trade than PRICE ALONE on the same days, and it must survive the
edge_lab gates. Grid: stopk ∈ {0.25, 0.35}, threshold f where stated ∈ {0.02, 0.05} (cdelta / cvol).
Decision time 10:00 (after the opening range, before the 10:30 cascade check).

## Tests (family of 8 → controls at 99.4 %, 400 seeds; walk-forward train 6 months, min 60 trades)
1. `rdir_price` — sign(px − open). Reference.
2. `rdir_delta` — sign of the RTH cumulative delta, when |cdelta| ≥ f × cvol.
3. `rdir_price_delta` — price direction only when delta agrees (|cdelta| ≥ f × cvol on the same side).
4. `rdir_price_delta_compl` — price direction when delta does NOT agree (complement of 3).
5. `rdir_ondelta` — sign of the overnight delta (18:00 → 09:30), when |delta| ≥ f × overnight volume.
6. `rdir_m1` — 1-minute momentum direction (er(1,45) ≥ 0.3, VWAP + open side). Reference for the momentum rule.
7. `rdir_m1_delta` — 1-minute momentum direction only when the last 45 minutes' delta agrees (≥ f × their volume).
8. `rdir_absorb` — price moved one way since the open but delta the other way (|cdelta| ≥ f × cvol) → trade AGAINST price.

## Adoption
Flow is "a better direction" only if test 3 or 7 passes every gate AND beats its reference (1 or 6) AND its complement on
walk-forward net per trade AND is not negative on the 2026-09/10 forward sessions. Otherwise: no.

## Results (appended 2026-10-18 after the runs — tests unchanged)
Development 2023-03 → 2026-03-19 (~640 flow days, decision 10:00, stop / 2R / 15:55).
| test | walk-forward net/trade (trades, /yr) | verdict |
|---|---|---|
| rdir_price (reference) | +7.03 (433, 124/yr) | FAIL controls (random day 45 %) |
| rdir_delta | +14.80 (170) | FAIL controls (day 86 %) |
| rdir_price_delta (delta agrees ≥ 2 % of volume) | **+16.36 (158, 45/yr)** | FAIL controls (time 96 %, day 87 %) |
| rdir_price_delta_compl | +8.15 (391) | FAIL controls |
| rdir_ondelta (overnight delta) | −6.61 (77) | FAIL screen |
| rdir_m1 (1-minute momentum, reference) | **+34.81 (90, 26/yr, win 54 %, 4/4 years)** | **PASS** (controls 100/100/100 %) |
| rdir_m1_delta | +37.29 (68) | PASS — but in-sample the 29 momentum days delta REMOVES made ≈ +47/trade vs +40 kept → delta adds nothing |
| rdir_absorb (price one way, delta the other) | 1 trade | FAIL — delta ≥ 2 % against price on only 20 of 639 days |
Forward look 2026-09-18 → 10-16 is empty in practice (≤ 4 trades): `atr` needs 14 prior sessions and the data gap
2026-03-20 → 09-18 leaves only ~7 usable sessions. Not judged.

Verdict: not adopted. Delta agreeing with the price move roughly doubles the price direction's result (+16 vs +8 when it
disagrees) but fails the controls on 3 years of data — a hypothesis, consistent with 2026-10-17 test D (cascade + delta +18.5
vs twin +15.0). Overnight delta and absorption give nothing. The best direction on this window is the 1-minute momentum,
and order flow does not improve it.
