# Pre-registration — more setups side by side (written 2026-10-18, BEFORE any run)

Founder: "more tests". Adopted 2026-10-18_multi_trade_day F: cascade, gap retest and 10:00 gap rule run side by side (overlap
allowed), +15.05 pts/session 2019–24. Two confirmed rules are not in F:
- M — the 10:00 momentum rule (er(1,45) ≥ 0.3, momentum agrees with the VWAP and open side, path ≥ 0.55; 0.35 ATR / 2R).
- W — the weakness long (prior IBS ≤ 0.3, VIX ≥ 20, buy 09:30 open, flat 15:55). It has no stop, which the Vest $150 daily limit
  forbids, so here with a stop s ∈ {0.35, 0.5, 0.75} ATR, no target. s is chosen ONCE on 2010–18 (highest W-alone pts/session).
Candidates (each a separate position, at most one trade per rule per day; 2 pt cost): F+M · F+W(s) · F+M+W(s).
Measured: pts/session 2010–18 / 2019–24 / 2025–26, max drawdown 2019–24, worst day, Vest $5K eval at $50/trade.
Adopt a candidate only if pts/session > F in BOTH 2010–18 and 2019–24 AND max DD 2019–24 ≤ 1.3 × F's (1,247). If several pass,
the one with the highest 2019–24 pts per point of drawdown. 2025–26 reported once.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--quick --trades`; 2 pt cost). Alone, per trade 2010–18 / 2019–24 / 2025–26: M −0.32 / +23.74 /
+54.35 (266/228/53 trades); W with stop 0.35 / 0.5 / 0.75 ATR −0.20 / +1.70 / +0.97 in 2010–18 → **s = 0.5** (+14.96 / +115.08 later).
| book | pts/session | trades/session | ≥ +20 | worst day | max DD 2019–24 | Vest $50 |
|---|---|---|---|---|---|---|
| F (current) | +0.15 / +15.05 / +13.86 | 1.26 | 35 % | −418 | 1,247 | 62.9 %, 24 days |
| F+M | +0.11 / +18.73 / +23.03 | 1.42 | 35 % | −449 | 1,286 | 43.0 %, 14 days |
| F+W(0.5) | +0.26 / +17.28 / +24.12 | 1.41 | 36 % | −636 | 1,854 | 42.8 %, 16 days |
| F+M+W(0.5) | +0.22 / +20.96 / +33.29 | 1.57 | 36 % | −636 | 1,958 | 35.3 %, 12 days |
**Nothing adopted.** F+M misses only on 2010–18 (+0.11 vs +0.15 — a difference inside the noise, but the rule is the rule);
F+W and F+M+W raise points but their drawdown (1,854 / 1,958) is over the 1.3 × cap (1,621). On Vest every add makes passing
LESS likely at $50 (more trades a day → the $150 daily limit is reached more often), though faster. F stays.
Description only: F+M is the near miss to watch forward — +3.7 pts/session 2019–24 for +39 pts of drawdown.
