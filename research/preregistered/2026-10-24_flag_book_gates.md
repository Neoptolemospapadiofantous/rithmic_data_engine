# 2026-10-24 — order-book / flow gates on mtf_flags_v5 (ideas from the cloud session), tested on real data

Source of ideas: a cloud Claude session that built SQL gates and tested them on synthetic data only.
Tested here on the real history: 1,500 base `mtf_flags_v5` trades, label `nq_hist`
(Databento MBO replay 2024-03 → 2026-03 + collector ticks 2026-09/10), 485 sessions.
Features from the closed minute before entry (entries fire at the bar close, no look-ahead):
book_1m imbalance / heavy-side share / spread, bars_1m trade delta 1m + 5m, mbo_1m top-10bps depth
imbalance + cancel/add ratio. Gates = thirds or signs. Stats: 4,000 random same-size subsets (one-sided),
Holm over 15 gates, day-clustered bootstrap of gate−rest, holdout split 2025-07-01,
Tradeify 1 NQ (target $3,000, EOD trail $1,000, day capped −$500, best day ≤30%, 60 sessions).

Result: NO gate is significant (best raw p 0.15, every Holm p = 1.000), no day-clustered CI excludes 0,
and every gated set still loses money in both halves. Base −$4.69 per MNQ trade, Tradeify pass 0.9%.
Best direction = fade the book (`__inv`): −$3.54/trade, IS −4.50, OOS −2.09, pass 2.7% — consistent with the
fleet's __inv note but too weak to rescue a losing entry. Costs are ~$3.3 of the −$4.69, so the strategy is
negative before costs too. Engine's own tick-level gated variants agree (nq_hist: __imb −$3.17,
__all −$2.37, __sg −$4.16, __micro −$5.89 per trade). Verdict: the ideas are not worth building on this strategy.
Scripts: research/programs/flag_book_gates/; output research/programs/out/flag_book_gates.txt.
Scratch replay rows left in paper_trades under labels flagtest (58 rows); not deleted.
