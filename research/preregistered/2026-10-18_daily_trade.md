# Pre-registration — a trade EVERY day (written 2026-10-18, BEFORE any run)

Founder: "I want at least 1 trade a day, if not more."
Tested setups (cascade, gap retest, momentum scan) fire on roughly half of days. The question: what is the best trade on the
OTHER days, and does trading them cost money? Already known: fades on no-direction days lose (nd_open / nd_vwap / nd_range).

Tests (NQ 2010-07 → 2024-12, ES confirm, 1.0 pt, stop stopk × ATR, 2R, flat 15:55; family of 4, controls 99 %):
1. `daily_1005` — EVERY day at 10:05: trade the way price has moved since 09:30 (no threshold). stopk ∈ {0.25, 0.35}.
2. `quiet_1135_open` — only days the cascade did NOT fire (09:50 < 0.25 ATR, 10:30 / 11:00 / 11:30 < 0.30 ATR from the open):
   at 11:35 trade the way price is vs the 09:30 open. stopk ∈ {0.15, 0.25} (quiet days, smaller ranges).
3. `quiet_1135_vwap` — the same days, direction = side of VWAP.
4. `quiet_1135_m1` — the same days, direction = 1-minute momentum (er(1,45) ≥ 0.2), no trade if none.
A daily fallback is acceptable only if it is not negative after cost (walk-forward) — it need not pass every gate, but its
cost to the account (net and worst-run) is reported either way.

## Results (appended 2026-10-18 — tests unchanged)
| test | result |
|---|---|
| daily_1005 (every day, go with the move since 09:30) | walk-forward +4.52 on 1,401 (it stood aside in months whose prior year was negative); FAIL controls (time 72 %, day 55 %); 6/11 years +; a 35-month run without a positive month |
| quiet_1135_open (no-cascade days) | −0.00 per trade, 3/7 years + — FAIL |
| quiet_1135_vwap | +0.11, 2/10 years + — FAIL |
| quiet_1135_m1 | +2.72 on 8 trades/yr — FAIL controls |
Verdict: on days with no tested setup there is no trade with an edge; a forced daily trade there is ≈ 0 before slippage, i.e.
a small loss after real fills. Fallback not adopted.
