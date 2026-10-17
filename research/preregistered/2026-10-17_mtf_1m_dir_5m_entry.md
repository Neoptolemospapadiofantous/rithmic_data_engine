# Direction on 1-minute bars, entry on 5-minute bars (pre-registered 2026-10-17, before any run)

Founder: "so 1m to see the direction and then we take a trade on the 5m."
Direction at each 5-min signal bar's close, from 1-min bars up to that moment (RTH 09:30 → now unless stated):
- D-PLAN  the plan's direction (first cascade check / gap retest) — today's panel
- D-EMA   1-min EMA-9 above / below 1-min EMA-21
- D-EMA50 1-min close above / below 1-min EMA-50
- D-MOM   sign of the last 30 one-minute moves (close − close 30 min ago), only if their efficiency ≥ 0.3 (straight enough)
- D-BOTH  D-PLAN and D-EMA agree
- D-NONE  none (both sides) — the baseline
Entry on 5-min bars: a bar that comes within 0.03 ATR of the 5-min EMA (9 / 14 / 20) and closes back in the direction (NEAR, the
sweep's best pullback type), or a bar that closes beyond the previous 5-min bar's high / low in the direction (BREAK).
Same permission (day's side of VWAP and the open, 09:45–13:55, ≤ 3 a day, done after a loss), stop 0.25 ATR, exits A break-even
at +1R hold / B 0.5R target, cost 1.6 pts, 2010-2024. 6 directions × 4 entries × 2 exits = 48. R per trade by era; walk-forward
pick; 2025-26 one look. Counts only if it beats D-PLAN and D-NONE in all three eras.

## Results (2026-10-17; scratchpad emamtf.cpp, sensitivity emamom.cpp) — R/trade 2010-24 (eras 10-18 / 19-22 / 23-24), BE+1R hold
- Plan direction + 5-min EMA near: −0.001 (EMA-9) / −0.010 (EMA-14) / −0.031 (EMA-20); 1-min EMA-9/21 or EMA-50 direction:
  −0.02 to −0.04; no direction at all: −0.03 to −0.04. The 0.5R target is negative everywhere (−0.07 to −0.10).
- 1-min MOMENTUM direction (last 30 one-minute moves, efficiency ≥ 0.3) + 5-min EMA-20 near: +0.082 (+0.057 / +0.085 / +0.173,
  n 930), 2025-26 +0.176 (n 71) — the ONLY cell meeting the pre-registered rule (beats D-PLAN and D-NONE in every era).
  Walk-forward picked D-MOM cells 10 of 12 years: +0.065 R/trade (n 1,207).
- BUT the post-hoc checks take most of it away: (1) it is a narrow bump — window 20 / 45 / 60 min or efficiency 0.2 / 0.4 swing
  between −0.07 and +0.18 with tiny samples at the edges; (2) the TWIN that enters the same 1-min momentum direction at a RANDOM
  5-min bar (no pullback) does +0.039 to +0.048 (W30, all eras positive at q0.2, 2025-26 +0.16) — about as well as the EMA entry.
VERDICT: the 1-min momentum direction (a straight last 30 minutes, on the day's side of VWAP and the open) carries a small edge,
~+0.04–0.05 R per trade; the 5-min EMA pullback adds nothing measurable on top of it. Forward-paper candidate, not a rule.
