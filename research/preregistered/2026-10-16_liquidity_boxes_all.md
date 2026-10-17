# Pre-registration — every other liquidity box × order-flow entry (written 2026-10-16, BEFORE any run)

Founder: "test everything" (after the list of box definitions not yet tested). Same two entries as
2026-10-16_liquidity_boxes.md, on 10 more boxes. Nothing is added, dropped or re-tuned after the runs.

## Entries (unchanged, completed 5-minute bars, one trade a day, flat 15:55)
- sweep (fade): the bar trades through H with aggressor delta ≥ +a × its volume and closes back below H → short;
  mirror at L → long.
- break (follow): first 5-minute close beyond H (previous close inside) with delta ≥ +a × volume → long; mirror at L.
Settings (4 variants): a ∈ {0.05, 0.15}; stop k × ATR, k ∈ {0.15, 0.25}; target 2R.

## Boxes (H / L — all known before the bar; NQ)
1. `eqbox`   equal highs / lows: the highest 5-minute high of the last 24 bars (2 h) when another high of those bars is
            within 0.03 ATR of it (likewise the lowest low); scan 10:00–12:00.
2. `asiabox` Asia range 20:00–03:00 high / low; scan 03:05–11:30.
3. `ldnbox`  London range 03:00–08:30 high / low; scan 08:35–12:00.
4. `vabox`   yesterday's RTH value area (70 % of volume around the busiest price) high / low; scan 09:35–12:00.
5. `npocbox` nearest naked busiest price (POC of one of the last 20 sessions, not traded through since) above / below
            today's open; scan 09:35–12:00.
6. `vwbbox`  VWAP ± 2 standard deviations; scan 10:00–12:00.
7. `wkbox`   last week's RTH high / low; scan 09:35–12:00.
8. `mobox`   last month's RTH high / low; scan 09:35–12:00.
9. `rnbox`   the 100-point band containing the 09:30 open (multiples of 100); scan 09:35–12:00.
10. `cnbox`  consolidation: the last hour's 5-minute high / low when that hour's range < 0.25 ATR; scan 10:30–12:00.
Levels compared across days (4, 5, 7, 8) skip the roll window.

## Family and pass bar
20 order-flow tests (10 boxes × 2 entries), development 2023-03-01 … 2026-03-19, 6-month training. 0.05 / 20 →
controls must clear 99.75 % of 400 seeds. Reference (not counted): the same 20 rules without the delta condition,
2010–2026, normal gates. A pass is a forward-paper candidate only.

## Results (appended after the runs — tests unchanged except the bug fix below)
**Bug found while running:** `hh(tf, n)` / `ll(tf, n)` always returned NaN (loop never ran), so `cnbox` first made 0
trades. Fixed + test added; `cnbox` re-run with the same spec. No other test in this or earlier families used hh/ll.

Flow versions (2023-03 … 2026-03, need ≥ 99.75 % on every control; OOS net pts/trade, trades):
| box | sweep (fade) | break (follow) |
|---|---|---|
| equal highs/lows | −11.9 (91) | +5.9 (321) — controls: direction 94 % |
| Asia range | **+10.4 (382) — direction 96 %**, time/day 100 %, positive 2023–26 | −7.3 |
| London range | −8.3 | −3.1 |
| yesterday's value area | −14.2 | −9.4 |
| naked POC | +1.2 (55) | −5.2 |
| VWAP ± 2 sd | −2.1 | +3.0 — direction 82 % |
| last week | −0.7 (12) | −6.2 (45) |
| last month | 2 trades | +18.3 (27 — too few) |
| 100-point band | −17.1 | **+12.4 (333) — direction 99 %** (need 99.75), time/day 100 % |
| consolidation (re-run) | +6.6 (48) | +7.1 (56) — too few |
Price-only references 2010–26: every box between −5.3 and +4.0 pts/trade, none passes.

**Verdict: nothing passes.** Closest: 100-point-band break and Asia-range sweep, both with flow. Caution: their
walk-forward figures are about twice their whole-period best variant (+5.4 / +5.6 pts/trade, t ≈ 1.3), i.e. the
monthly variant choice was lucky; the honest size is ~+5 pts/trade and not significant on 3 years. Hypotheses for a
forward test, nothing more. Asia sweep without flow loses on 16 years (−0.5), so the claim rests on the flow condition.
