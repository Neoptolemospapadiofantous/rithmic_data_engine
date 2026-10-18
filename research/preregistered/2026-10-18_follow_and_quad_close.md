# Pre-registration — NQ leads ES, and the quarterly-rebalance close (written 2026-10-18, BEFORE any run)

Founder: "more tests". Most calendar/flow ideas were already tested and FAILED (pre_holiday, fomc_predrift, fomc_fade, news_fade,
cpi_fade, nq_es_diverge, opex_fade, opex_friday_short, month_end_rebalance, turn_of_month — research/README.md). Flipping a fade
whose own result was positive (fomc_fade +10.3, news_fade +6.0 net/trade) is a loss by arithmetic, so only two new specs:
1. `nq_es_follow` — the flip of nq_es_diverge (which lost −3.04/trade, t −2.98): when NQ's move from the open (own ATR) runs more
   than z ahead of ES's (`xs`), trade NQ in the direction it leads. Same params / window / stop / 1.5R target as nq_es_diverge.
   Why: if the spread does not close, the leader is the one with the order flow.
2. `quad_close` — quarterly index-rebalance days (3rd Friday of Mar / Jun / Sep / Dec, `opex && month % 3 == 0`): at 15:00 trade
   in the direction of the 14:00 → 15:00 move if it exceeds k × ATR, flat 15:55; k ∈ {0.05, 0.1}, stop {0, 0.2} ATR.
   Why: index funds must trade the rebalance at the close; the hour before shows which side the imbalance is.
Both end 2024-12-31 (holdout sealed); full pipeline, default gates (`min_trades` 30 for quad_close — ~60 such days 2010–24).
A PASS gets one holdout look (2025+, ≥ 10 trades, net > 0 for quad_close; ≥ 30 for nq_es_follow).

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab, full pipeline).
1. `nq_es_follow`: **FAIL at CONTROLS** — screen best stop 0.25 ATR, z 0.3: +0.87 net/trade on 1,371 (t 0.85); z 0.5: +2.49 on 454
   (t 1.56); walk-forward 587 trades +2.36, PF 1.19, but random direction / time on the same days does as well. Neither fading
   (nq_es_diverge −3.04) nor following NQ's lead over ES is an edge: the divergence days are trend days, the cascade's own days.
2. `quad_close`: **NOT TESTABLE with the owned data** — 0 trades in every variant. Cause (verified): the research cache's NQ is
   Databento `NQ.c.0`, a calendar roll that maps to the EXPIRING contract until the Sunday after expiry (e.g. 2019-03: 15657 to
   03-17, then 9166). NQ futures stop at 09:30 ET on the third Friday, so every quarterly expiry Friday has 0 RTH bars in the cache
   (checked 2019-03-15, 2019-06-21, 2023-12-15, 2024-03-15), and the days before it trade on a thinning contract (2019-03-14: 293
   of 390 RTH bars). Testing the rebalance close needs the next contract's bars (NQ.c.1 or a volume-roll series).
**Side finding (applies to every study):** quarterly expiry Fridays are absent from all edge_lab results, and expiry-week days are
on the expiring contract unless a spec excludes `rollwin`. Specs that do not exclude it (e.g. the cascade) include those thin days.
