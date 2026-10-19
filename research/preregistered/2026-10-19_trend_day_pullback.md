# Pre-registration — pullback entries on a confirmed trend day (written 2026-10-19, BEFORE any run)

Founder: "on trend days with a pullback strategy" (after: full size on trend days to pass the eval in a day).
Trend day = known at 09:50: |09:50 price − 09:30 open| > 0.25 ATR (the cascade's first check); D = its side.
Entry: scan 5-minute closes 09:55–13:55; the bar touches the level L (low ≤ L + 0.05 ATR for longs, high ≥ L − 0.05 ATR for
shorts) and closes back on D's side of L; price still on D's side of the open; optional straight path
|px − open| / (hod − lod) ≥ p. L ∈ {VWAP, 5-min EMA 20}, p ∈ {0, 0.55}, stopk ∈ {0.25, 0.35} ATR → 8 variants. 2R, flat 15:55,
max 3 trades a day, one position at a time. edge_lab 2010-07 → 2024-12 (walk-forward), ES confirm, all gates (controls 99 %).
Then, whatever the verdict, as an ADD to book F (the 2026-10-18 rule: pts/session up in BOTH 2010–18 and 2019–24, pts per
point of max DD 2019–24 not lower) and through the Vest $5k sim at $50 / $100 risk per trade. 2025–26 reported once.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-19 (build-exp/edge_lab; walk-forward 2010-07 → 2024-12).
- edge_lab: **FAIL at CONTROLS** — 787 OOS trades, +1.98 pts/trade net, PF 1.10, t 0.96; beats random direction in 73 % (need 99 %),
  random time 86 %, random day 98 %. Robustness 5/7 but 2023–26 −6.44 pts/trade (2024 −948 pts) and ES −160 pts. Best variant
  (EMA 20, straight path ≥ 0.55, 0.35 ATR) +3.87 in-sample, t 2.03 < 2.73 needed for 8 variants.
- That variant at 2 pt: −0.77 (476) / +7.88 (345) / +8.65 (91) pts/trade in 2010–18 / 2019–24 / 2025–26; daily corr with F 0.36.
- As an add to F: pts/session +0.15 → −0.03 (2010–18), +15.05 → +16.90 (2019–24), +13.86 → +16.37 (2025–26); max DD 2019–24
  1,247 → 1,754; pts per DD 0.0121 → 0.0096 → **fails the add rule** on both counts (2010–18 down, return per DD down).
- Vest $5k: $50/trade pass 62.9 % → 34.1 % (median 24 → 14 days); $100/trade 22.2 % → 19.1 % (5 → 4 days).
**Reading:** pullbacks after the 09:50 trend check trade the same days as F (corr 0.36) and add drawdown faster than points;
the edge on a trend day is already captured by F's first entry. Faster passes, but roughly half as many. Closed door.
