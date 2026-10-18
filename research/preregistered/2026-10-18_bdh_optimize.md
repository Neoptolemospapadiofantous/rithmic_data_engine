# Pre-registration — optimising the BDH 15-min ORB on the gap side (written 2026-10-18, BEFORE any run)

Founder: "Can we optimize". Base: bdh_orb15_gap (+6.37 walk-forward, FAIL controls by a hair, 2025+ not confirmed).
Grid (72 variants), NQ 2010-07 → 2024-12, ES confirm, 1.0 pt cost, one trade a day, last entry 11:00, flat 15:55:
- g  minimum gap ∈ {0.05, 0.1, 0.2} ATR (break must be on the gap's side)
- s  stop: 0 = range midpoint, 1 = far side of the range
- k  break confirmation: 1 = 1-min close beyond the range, 5 = 5-min close beyond it
- t0 first entry: 09:45 or 10:00
- rr target: 0 = hold to 15:55, 2R, 3R
Decision rules (set now):
1. Walk-forward (each month the best variant of the prior 12 months) is the result; all edge_lab gates at 99 %.
2. Plateau: the best whole-period variant must not beat the mean of its one-step neighbours by more than 50 %, else "narrow peak".
3. Adopt only if it beats `gap_or_retest` (+9.03 walk-forward) per trade AND the frozen setting is positive on 2025+.

## Results (appended 2026-10-18 — rules unchanged)
- Walk-forward: **+6.32 pts/trade, 1,205 trades (73/yr), win 41 %, 13/14 years +, PASS every gate** (controls 100/100/100 %, 7/7).
- Plateau: all 72 variants positive 2010–24 (mean +5.39); best variant only 1.10–1.16× its neighbours — a broad plateau, not a
  spike. Bigger gap (0.2 ATR +6.33 avg), 5-min close confirmation (+5.72 vs +5.06) and entries from 10:00 (+5.67 vs +5.12) help;
  stop at the midpoint or the far side makes no difference.
- 2025-01 → 2026-10-16, frozen: **all 24 variants tested NEGATIVE** (−0.5 … −18 pts/trade, 77–138 trades each, t −0.05 … −1.6).
  `gap_or_retest` on the same period: +7.15 (135, PF 1.13).
- Rule 3 not met: it does not beat `gap_or_retest` per trade (+6.32 vs +9.03) and its frozen setting loses on 2025+.
Verdict: NOT adopted. The optimised BDH ORB is a robust 2010–24 result that has not held up since 2025; the gap retest — the same
gap-side idea entered on the range edge with a 0.35 ATR stop — has. (2025+ has been looked at many times; it is a warning, not proof.)
