# Can it be traded by hand? Sprint execution tests (pre-registered 2026-10-17, before any run)

Founder: "do all" (the list: late entries / missed signals, news days, sprint management). Base = the sprint as on the Plan page:
the day's first signal (09:30 weak-day or Monday long when on, else the first cascade check or gap retest), stop 0.25 ATR (gap
retest 0.35), target 0.5R, one trade a day, sized for Tradeify Lightning 25K at $600 risk, cost 1.5 pts. NQ 2019-01 → 2024-12.
Scored: R per trade, and P(first payout ≤ 7 / 10 / 20 trading days) + P(blown ≤ 7 days) with the confirmed Lightning rules
(payout 1 at +$1,500, best day ≤ 20 %, EOD trailing $1,000; Tradeify help centre 2026-10-17).
1. LATE ENTRY: enter 0 / 1 / 2 / 3 / 5 minutes after the signal at that minute's price (stop and target re-set from the fill).
2. WORSE FILL: entry 0 / 5 / 10 / 20 pts worse than the signal price, stop and target from the fill.
3. MISSED SIGNALS: skip 10 / 20 / 30 % of signal days at random (20 seeds), and "skip the day after a losing day".
4. NEWS DAYS (CPI / PPI / NFP / FOMC from the calendar): trade as normal vs skip those days.
5. BREAK-EVEN at +0.25R (stop to entry + cost) vs none, on the 0.5R target.
6. SECOND CHANCE: after a stop-out, take the next plan setup in the same direction the same day (≤ 2 trades).
A rule is worth adopting only if it raises P(payout ≤ 20 days) without raising P(blown ≤ 7 days).

## Results (2026-10-17; scratchpad sprint4.cpp) — Lightning 25K, R/trade · payout ≤ 7 / 10 / 20 days · blown ≤ 7 days
| Variant | R/trade | ≤7d | ≤10d | ≤20d | blown ≤7d |
|---|---|---|---|---|---|
| BASE (Plan page sprint) | +0.036 | 6.7 % | 12.8 % | 24.6 % | 33.6 % |
| enter 1 / 2 / 3 / 5 min late | −0.000 / +0.000 / +0.001 / −0.014 | 3.0–5.7 % | 7.2–10.5 % | 19.0–21.7 % | 35–37 % |
| fill 5 / 10 / 20 pts worse | −0.079 / −0.192 / −0.401 | 2.7 / 1.6 / 0.1 % | 5.2 / 2.6 / 0.3 % | 10.1 / 5.2 / 0.5 % | 48 / 58 / 75 % |
| skip 10 / 20 / 30 % of days | +0.035 / +0.034 / +0.033 | 4.4 / 2.7 / 1.5 % | 10.1 / 7.9 / 5.7 % | 23.2 / 21.0 / 18.6 % | 30 / 26 / 23 % |
| skip the day after a loss | +0.019 | 5.4 % | 9.2 % | 18.3 % | 20.1 % |
| skip CPI / PPI / NFP / FOMC days | +0.031 | 3.0 % | 9.4 % | 20.8 % | 27.2 % |
| break-even at +0.25R | +0.041 | 0.8 % | 4.0 % | 19.5 % | 16.6 % |
| second chance after a stop-out | +0.038 | 6.7 % | 13.4 % | 26.1 % | 35.4 % |
Nothing passes the adoption rule (more payouts ≤ 20 days without more blown accounts); the second chance is +1.5 pts of payout
odds for +1.8 pts of blown — not adopted. THE FINDING: execution decides it. One minute late erases the per-trade edge (+0.036 → 0 R)
and a fill 5 pts worse turns it negative (−0.079 R, payout ≤ 20 days 24.6 % → 10.1 %). The 0.5R target is only ~20–40 pts, so 5 pts
of slippage is 12–25 % of the win. ⇒ the alert must reach the founder within the signal minute, entry must be a bracket order placed
at once, and a missed entry is skipped, never chased. Break-even at +0.25R halves blown accounts but halves the 10-day payouts too.
