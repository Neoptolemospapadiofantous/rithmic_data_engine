# Regime-switched trading per session (pre-registered 2026-10-17, before any run)

Founder: "Every day I want to have a plan, and a period where I can trade for example EMA based on what the regime is."
Regime = the tape over the last 60 minutes: acr(1,60), lag-1 autocorrelation of 1-minute moves (validated today: persists in
real NQ first→second half 57–64 %, while 5-minute measures show ~50 %). Trending tape: acr(1,60) > th; reverting tape:
acr(1,60) < −th.
Per session window (5-minute decisions, first signal only, one trade a session):
  NY 10:00–15:00 (flat 15:55) · Asia 20:00–02:00 (flat 02:30) · London 03:00–07:30 (flat 08:00)
  rg_ema_<s>     trending tape → EMA pullback with the 5-min EMA(9)/EMA(21) trend: a 5-min bar touches EMA(9) and closes
                 back on the trend side → enter with the trend
  ema_<s>        the same EMA pullback with NO regime gate (twin: does the regime add value?)
  rg_fade_<s>    reverting tape → a 5-min close beyond VWAP ± 1.5 sd → fade toward VWAP
Exits: stop stopk × ATR (NY 0.15/0.25, overnight 0.1/0.15), target 1.5 R. th ∈ {0, 0.05} for the gated ideas.
Data 2010-07 → 2024-12, 2025+ held back. "Regime adds value" only if a rg_ idea PASSES and beats its ungated twin per trade.
9 ideas counted in the registry.

## Results (2026-10-17)
All nine FAIL (screen or walk-forward); the best in-sample setting of every gated idea loses or is ~0 after cost. The regime gate
does not make the EMA pullback work (NY gated −0.08 vs ungated +0.08 pts/trade walk-forward) and fades lose in every session.
The 1-minute tape regime is real and persistent, but it does not turn EMA or VWAP-fade entries into an edge.
