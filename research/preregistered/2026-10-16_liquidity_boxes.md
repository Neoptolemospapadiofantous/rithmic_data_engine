# Pre-registration — liquidity boxes × order-flow entries (written 2026-10-16, BEFORE any run)

Founder: "what about creating a box between liquidities or orb or levels and try to find entries with orderflow".
Stops cluster just beyond obvious levels; a run through them is either real demand (follow) or a stop hunt that traps
the aggressors (fade). Order flow is meant to tell the two apart.

## Family: 6 tests (3 boxes × 2 entries), controls at 99 %, 4 variants each
Boxes (H / L): `orb15` = rhi(15) / rlo(15), scan 09:50–12:00 · `onbox` = overnight high / low (onh / onl), 09:35–12:00 ·
`pdbox` = yesterday's RTH high / low (ph / pl, not in the roll window), 09:35–12:00. Completed 5-minute bars, one trade a
day, flat 15:55.
- **sweep (fade):** the bar trades through H with aggressor delta ≥ +a × its volume and closes back below H → short
  (mirror at L → long).
- **break (follow):** the first 5-minute close beyond H (previous 5-minute close inside) with delta ≥ +a × volume →
  long (mirror at L → short).
Settings: a ∈ {0.05, 0.15}, stop k × ATR, k ∈ {0.15, 0.25}, target 2R. Development 2023-03-01 … 2026-03-19, 6-month
training. Holdout = collector sessions 2026-09-18 on (sanity only).

## Reference (not candidates, not counted)
The same 6 rules WITHOUT the delta condition on 2010–2026 (normal gates) — the base rate the flow has to improve on.

## Pass
All edge_lab gates with control_pct 0.99. A pass is a forward-paper candidate, nothing more.

## Results (appended after the runs — tests unchanged)
| test | OOS trades | net pts/trade | verdict |
|---|---|---|---|
| orb15 sweep (fade) + flow | 43 | +3.57 | FAIL screen (too few trades) |
| orb15 break (follow) + flow | 434 | +2.77 | FAIL controls |
| overnight-box sweep + flow | 122 | +3.31 | FAIL controls |
| overnight-box break + flow | 189 | −2.55 | FAIL screen |
| yesterday-box sweep + flow | 40 | −6.14 | FAIL walk-forward |
| yesterday-box break + flow | 123 | −5.78 | FAIL walk-forward |
Price-only reference 2010–26: sweeps −2.5 / −0.6 / −2.1, breaks +2.4 / +0.5 / +0.3 pts/trade — all fail.
Verdict: no liquidity-box entry has an edge, with or without order flow. The fade (stop-hunt) idea loses on price
alone over 16 years; flow does not rescue it.
