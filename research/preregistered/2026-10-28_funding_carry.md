# Funding carry ("collect the funding premium") — pre-registration, 2026-10-28

Founder, 2026-10-28: "2 test funding premium". Written BEFORE any funding data was downloaded or looked at.

## Question
Can we earn the perpetual-futures funding payment with the price risk hedged away, net of every cost?

## Strategy F1 — cross-venue carry (delta-neutral)
For one coin, hold the same notional SHORT on the venue paying the higher funding to shorts and LONG on the other.
Income per hour = funding received on the short leg − funding paid on the long leg. Price moves cancel (both are
perps on the same coin), apart from the gap between the two venues' prices (basis), which is a cost at entry/exit.

Pairs: Lighter–Hyperliquid, Lighter–Binance, Hyperliquid–Binance.

## Data (public history endpoints, no keys)
* Lighter `GET /api/v1/fundings` (1 h), Hyperliquid `fundingHistory` (1 h), Binance `fapi/v1/fundingRate` (8 h, some 4 h).
* Coins: every coin listed as an active perp on ALL THREE venues on 2026-10-28 (90 coins) — no hand-picking.
* Window: 2025-10-28 00:00 UTC → 2026-10-28 00:00 UTC. Funding is accrued on the venue's own schedule.

## Decision rule (causal: only funding already paid is used)
Every hour, per coin and pair: trailing mean of the hourly funding difference over L hours, annualised.
Enter (choosing the direction from the sign) when |trailing diff| ≥ θ_in; exit when it falls below θ_out = θ_in / 2
or flips sign. One position per coin-pair; notional fixed.

Grid (chosen on the TUNING half, judged once on the TEST half):
* L ∈ {8, 24, 72} hours; θ_in ∈ {10, 20, 40, 80} % APR.

## Costs (per leg, each way)
* Fee: Lighter Standard 0; Hyperliquid taker 4.5 bps; Binance taker 5.0 bps.
* Spread + slippage: 2 bps per leg each way (ALL coins — conservative for majors, possibly light for thin coins).
* Basis: 5 bps per round trip per position (entry + exit price gap between venues) unless measured otherwise.
* Capital: each leg margined at 3× → capital = 2 × notional / 3. Returns reported per notional AND on capital.

## Halves
* Tuning: 2025-10-28 → 2026-04-28. Test: 2026-04-28 → 2026-10-28. The test half is read once, for the chosen cell.

## Pass (test half, chosen cell)
1. Net return > 0 after all costs, on ≥ 60 % of coins with at least one trade.
2. Net ≥ 10 % APR on capital across the portfolio (equal notional per coin-pair position).
3. Positive in ≥ 4 of the 6 test months.
4. Not carried by ≤ 3 coins: without the best 3 coins net is still > 0.
Also reported: trades, average hold, worst month, the funding share each venue contributes, and how often the
two legs' venues disagree in sign (the source of the income).

## Known gaps (stated, not fixed)
* No liquidation model: a large move with 3× legs on two venues needs margin moved between them; reported as the
  largest one-day move per coin during holds, not as P&L.
* Basis is a flat assumption unless measured from mark prices.
* Single-venue carry (perp vs spot) is not tested here: Lighter has no deep spot leg for these coins.

## Addendum, 2026-10-28 (before any result was computed; data semantics checked on 0G and 2Z only)
* Units: Lighter `rate` is % per hour with `direction` "long" = longs pay (sign agrees with Hyperliquid on 96 % of
  0G hours); Hyperliquid `fundingRate` is a fraction per hour; Binance `fundingRate` is a fraction per settlement.
  Duplicate timestamps (window edges) are dropped.
* "APR on capital" = net P&L ÷ time-weighted average capital in use, annualised (primary, used for pass rule 2);
  P&L ÷ peak concurrent capital is reported beside it.
* The tuning-half cell is chosen by the highest tuning-half net APR on capital (ties: fewer round trips).
* The three pairs of one coin are independent positions (each with its own capital).
* Hold-period price risk: largest absolute daily move (Hyperliquid daily candles) while a position is open, per coin.
* "Test months" = the test half cut into 6 equal blocks (30.5 days each), not calendar months.
