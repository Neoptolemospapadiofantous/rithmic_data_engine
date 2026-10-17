# Fleet: are the entries or the exits the problem? (2026-10-17 — diagnostic, NOT pre-registered)

Founder: "I have a feeling the entries and exits are not good for any strategy so far — the fleet."
Data: every nq_hist fleet trade (hist_trades, 2024-03 → 2026-10-09, 8.28 M MNQ trades, 8,935 ids). Prices from the NQ 1-minute cache,
measured from the open of the first minute after entry. Programs: scratchpad fleetq.cpp, fleetq2.cpp.

1. Raw (all trades, overlapping): paper P&L −1.52 pts/trade; the same trades' own exits replayed on clean prices −0.04 before cost
   → the fleet's whole loss is the round-trip cost (~1.6 pts). Direction × move after entry: +0.23 / +0.39 / +0.97 / +1.50 pts at
   +5 / +15 / +30 / +60 min — the entries point the right way more often than not, the exits close long before it pays.
   Only 10 of 6,383 ids (≥300 trades) average more than the cost.
2. Strict (one position at a time per id, cost 1.6, median over ids):
   | Exit | Fleet entries | Same moment, direction = sign(price − 09:30 open) | Same time & direction, random other day |
   |---|---|---|---|
   | hold 60 min | −0.43 (46 % of ids > 0) | +1.78 (74 %) | −1.85 |
   | hold 120 min | +0.02 (50 %) | +3.19 (76 %) | −1.61 |
   | hold to 15:55 | +4.69 (68 %) | +7.22 (89 %) | −2.00 |
   Hold to 15:55 by year (pooled): fleet +5.28 / +6.56 / −4.41 and open-direction +5.24 / +12.63 / −10.50 for 2024 / 2025 / 2026.
3. Reading: the information in the fleet's entries is "which way has today moved" — the open-direction twin beats the fleet's own
   signals; fades (mr_vwap_fade, mr_band_fade, fail_bo, thrust_fade, delta_div, mtf) are 9–29 pts/trade worse than going with the
   day. Short holds / tight stops / trails throw the day's move away. Holding to the close in the day's direction is the cascade
   idea again (long history: PASS, weak 2013-18); 2026's months are negative for it, so this is a trending-day bet, not a new edge.
   Families whose signal beat the open-direction twin (fib_pb +4.3, fibr_e +5.3, trend_ema_pb +4.3, trend_rs_es +18.7 on 22 ids)
   are post-hoc picks from one 2.5-year window — candidates for a forward paper test only.
