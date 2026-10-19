# Pre-registration — follow the EMAs in the Globex session (written 2026-10-19, BEFORE any run)

Founder: "we simply want to follow the emas" (after passing a Vest eval on one Globex trade). EMA rules in RTH are a closed door
(3,300+ variants ≈ breakeven); Globex was tested only behind a BOS filter (2026-10-18_bos_ema, −1.29).
Rule: EMA stack on tf-minute bars — long when EMA9 > EMA20 > EMA50, short when EMA9 < EMA20 < EMA50; entry on a completed
5-min bar that touches the 5-min EMA20 (within 0.05 ATR) and closes back on the stack's side. Scan 18:05 → 09:20 ET, max 3 trades
per trading day, one position at a time, flat 09:25. Grid tf ∈ {5, 15}, stopk ∈ {0.25, 0.35} ATR, rr ∈ {2, 3} → 8 variants.
edge_lab 2010-07 → 2024-12, all gates (controls 99 %). Then the fixed walk-forward pick through the Vest $5k sim at $50 / $100 /
$150 per trade (strict + lenient daily-loss models). 2025–26 reported once.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (build-exp/edge_lab). **FAIL at SCREEN** — every one of the 8 variants loses in-sample (best −1.08 pts/trade, t −2.11);
walk-forward 2,066 trades −0.57 pts/trade, PF 0.98, 3/9 years positive, 26/121 months positive; random direction beaten in only
59 % of seeds; robustness 1/7 (only 2019–22 +0.21); ES −925 pts. Gross +0.43 pts/trade: the EMA stack overnight is a coin flip
before costs. The Vest sim step was skipped: a strategy that loses at cost passes a full-size eval only by luck.

## Follow-up, pre-registered 2026-10-19 BEFORE running (founder: "i think you are incorrect")
The fixed 2–3R target caps the trend a follower is meant to ride. `gx_ema_run` = the same entries, NO target: once +1R the
stop trails the best price by trail_r × R; flat 09:25. Grid tf ∈ {5, 15}, stopk ∈ {0.25, 0.35}, trail_r ∈ {1, 2} → 8 variants,
max 3/day. `gx_ema_run1` = identical, max 1 trade per night (the first stacked pullback). Same gates.
Result of the follow-up (2026-10-19): both **FAIL at SCREEN**. gx_ema_run −0.26 pts/trade walk-forward on 2,564 (gross +0.74, win 47 %,
3/10 years +, random direction beaten 62 %); gx_ema_run1 −0.43 on 1,775 (gross +0.57, 4/11 years +, 57 %). Letting winners run
does not change the reading: the overnight EMA stack has no direction edge.
Vest $5k from 2019 (fees per pass at $40): run1 at $150/trade pass 25.3 % ($158/pass, median 10 trade days); run at $50 20.5–24.7 %
($162–195); follow at $50 28.8 % ($139). Book F at $50: 62.9 % ($64). A losing strategy still passes ~1 eval in 4 at full size —
that is the eval's lottery shape, not an edge.
