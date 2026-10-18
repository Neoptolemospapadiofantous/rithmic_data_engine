# Pre-registration — ORB 5 / 15, then TREND or REVERSE by REGIME (written 2026-10-18, BEFORE any run)

Founder: "I want 5m orb 15m orb and a trend or reverse trades. If regime is good (regime must be verified it works) whichever regime
we are in we should be taking trades."

## Two trades on the 15-minute opening range (09:30–09:45), NQ, completed 5-minute bars, one trade a day, flat 15:55
- TREND  `T`: 09:50–11:30, first 5-min close beyond the range → that way; stop = the range midpoint; target rr ∈ {2, 0 = hold}.
- REVERSE `F`: 09:50–13:00, a 5-min bar pokes beyond the range and closes back inside → fade toward the range; stop stopk ∈ {0.15, 0.25} ATR;
  target 2R.
Both already FAIL unconditionally (orb15 / orb15_sweep_price). The question is whether a regime known at the decision bar separates
the days on which each works.

## Seven regime reads, each known at the decision bar (R = "trend regime")
| id | R (trend) | source |
|---|---|---|
| gap | abs(gap) >= 0.1 ATR | overnight gap |
| orw | 15-min range width <= 0.35 ATR (narrow open) | opening range |
| onr | overnight range (18:00–09:29) >= 0.8 ATR | overnight |
| vix | prior VIX close >= 20 | VIX file |
| eff | er(1,45) >= 0.3 at the decision bar (last 45 one-minute bars straight) | tape |
| acr | acr(1,60) > 0 (trending 1-minute autocorrelation) | tape |
| pdt | abs(pret) >= 0.5 ATR (yesterday closed far from the day before) | prior day |

## Tests: 7 regimes × {T on R-days, T on not-R, F on R, F on not-R} + the two unconditional references = 30 ideas
NQ 2010-07 → 2024-12, ES confirm, 1.0 pt; controls at 99.8 % (0.05 / 30), 400 seeds; 2025+ judged once at frozen settings.

## "Regime verified" — all four must hold for a regime
1. T on R-days passes every gate and beats BOTH the unconditional T and T on not-R-days per trade (walk-forward).
2. F on not-R-days is positive walk-forward and beats F on R-days.
3. The combined system (T on R, F on not-R; pre-registered follow-up `rgm_combo_<id>`) passes every gate.
4. Frozen 2025+ of the combined system is positive.
Anything less = the regime does not separate trend from reversal days; the result is reported as such.
Follow-up if a regime verifies: the same with the 5-minute range (09:30–09:35), scan from 09:40.

## Results (appended 2026-10-18 — tests unchanged)
Walk-forward net pts/trade (trades). References: TREND all days +3.77 (1,499) FAIL controls; REVERSE all days −2.77 (1,210) FAIL screen.
| regime | TREND on R | TREND on not-R | REVERSE on R | REVERSE on not-R |
|---|---|---|---|---|
| gap ≥ 0.1 ATR | +4.20 (1,603) ctrl: time 84 % | **−11.25** (160) | −2.18 | +0.33 (284) FAIL WF |
| narrow OR (≤ 0.35 ATR) | +1.67 | +5.67 (701) ctrl: dir 90 % | −2.69 | +2.07 (557) ctrl: dir 68 % |
| wide overnight (≥ 0.8 ATR) | +0.65 | +2.60 | −1.72 | −2.37 |
| VIX ≥ 20 | +1.13 | +2.41 | +0.45 FAIL WF | −2.42 |
| 1-min straight (er ≥ 0.3) | **+6.01** (1,365; 2025+ +18.3 on 161) ctrl: time 29 % | +2.62 | −4.63 | −1.54 |
| 1-min autocorr > 0 | +4.20 | +3.91 | +1.13 ctrl: dir 86 % | −1.23 |
| prior-day move ≥ 0.5 ATR | −2.78 | +5.54 ctrl: dir 97 % | −2.57 | −1.02 |
No regime meets the verification rule: (1) the trend trade on R-days never passes controls at 99.8 % — on `eff` days a RANDOM entry
time did as well 71 % of the time, i.e. the day's direction is the edge, not the ORB break; (2) the REVERSE trade is negative or
fails on EVERY not-R set — no regime read turns the failed-break fade into a trade. Rule 3/4 (combo, 5-min follow-up) therefore not run.
Verified separators for the TREND side only: gap (trend +4.2 vs −11.3 without a gap) and 1-min straightness (+6.0 vs +2.6) — the two
reads already behind gap_or_retest and the momentum rule. On non-trend days the verified action is NO trade.
