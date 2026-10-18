# The every-day book of the confirmed setups — 1 NQ, points per session (2026-10-18, description, no selection)

Founder: "we need to be getting 20-50 points a day with 1 NQ daily not one big trade once in a while". Not a new idea: every setup
below was confirmed earlier; this measures what they make together, every day, on one contract. Nothing was tuned; there is
no gate verdict to adopt — it answers "how many points a day does the tested edge give".

Book (edge_lab, one position at a time, 5-minute scan 09:30–13:55, priority when two fire on one bar: weakness > gap > momentum >
cascade > retest; 1 NQ; real cost 1 pt): weakness long at 09:30 (prior IBS ≤ 0.3, VIX ≥ 20, no stop, flat 15:55) · the 10:00
gap rule and 10:00 momentum rule (straight path ≥ 0.55; stop 0.25 ATR, 2R) · the cascade (09:50 > 0.25 ATR, then 10:30 / 11:00 /
11:30 > 0.30 ATR, first that fires only; 0.25 ATR, 2R) · the gap retest (OR15 edge on the gap side; 0.35 ATR, 2R). Two forms:
max 1 trade a day (`daily1`) and max 3 (`daily3`). Unfiltered screen per calendar year (sessions = session_stats ok days):

| year | sessions | daily1 trades · pts/trade · **pts/session** | daily3 trades · pts/trade · **pts/session** |
|---|---|---|---|
| 2019 | 245 | 206 · +4.0 · **+3.3** | 240 · +2.5 · **+2.4** |
| 2020 | 245 | 209 · +16.3 · **+13.9** | 227 · +15.7 · **+14.5** |
| 2021 | 247 | 202 · +10.4 · **+8.5** | 236 · +11.7 · **+11.2** |
| 2022 | 246 | 229 · +11.2 · **+10.4** | 248 · +9.9 · **+10.0** |
| 2023 | 244 | 202 · +3.9 · **+3.3** | 229 · +5.2 · **+4.9** |
| 2024 | 245 | 203 · +17.5 · **+14.5** | 238 · +12.8 · **+12.5** |
| 2025 | 243 | 200 · +26.4 · **+21.7** | 242 · +21.2 · **+21.1** |
| 2026 (to 10-16, data gap 03-20 → 09-18) | 71 | 52 · +36.9 · **+27.1** | 63 · +40.5 · **+36.0** |
Walk-forward (stands aside in a month after a losing trailing year) is lower in weak years: 2013 −167, 2017 −251, 2018 −259
(daily3); 2010–2018 the book makes ≈ 0–2 pts/session.

Reading: the book trades on ~85 % of sessions. In 2025–26 it reaches the founder's 20–50 pts/session band; in 2019–24 it averages
~9–10 pts/session (≈ 3–15 by year). Points scale with NQ's level and volatility (ATR ~100 pts in 2019, ~300–400 in 2025–26), so
the same edge is worth more points now. A second and third trade a day add little (daily3 ≈ daily1). The per-day distribution
(share of days ≥ +20, worst day) is not in edge_lab's reports — not measured here.

## Per-day statistics and real costs (2026-10-18, founder: "do 1 and 3")
`edge_lab run … --trades` (new flag: writes reports/<id>.trades.csv — every screen trade of the best variant) on `daily1`,
sessions from session_stats (no-trade sessions count as 0). Cost per round trip: 1.0 pt (edge_lab default), **2.0 pt (measured
live: entry slippage 4.6–5.7 ticks + ~2 ticks on exit + commission)**, 3.0 pt (pessimistic). 2019-01 → 2026-10, 1,786 sessions.

| cost | avg pts/session | days ≥ +20 | days ≤ −20 | traded | max drawdown | longest run of losing trade days |
|---|---|---|---|---|---|---|
| 1.0 | +11.4 | 36 % | 39 % | 84 % | 1,149 pts | 9 |
| **2.0** | **+10.6** | 36 % | 39 % | 84 % | 1,156 pts | 9 |
| 3.0 | +9.8 | 36 % | 40 % | 84 % | 1,177 pts | 9 |

At 2.0 pt, by year (mean / mean without the single best day / median day): 2019 +2.5 / +1.8 / 0 · 2020 +13.0 / +11.6 / 0 ·
2021 +7.7 / +6.8 / 0 · 2022 +9.5 / +5.8 / 0 · 2023 +2.4 / +1.5 / 0 · 2024 +13.7 / +12.6 / 0 · 2025 +20.9 / **+12.2** / 0 ·
2026 +26.3 / +19.3 / 0. Best / worst day: 2025 +2,117 (2025-04-09, the weakness long held from the open with no stop through
the tariff-pause rally) / −550; 2022 best +895 (2022-02-24); worst days −380 … −550 (2021, 2022, 2025).
**Reading:** the honest figure is ≈ +10 pts/session at real cost (2019–26), ≈ +12 in 2025 without its one outlier day. The
median day is 0: a typical week is a few +20…+300 days against a few −20…−300 days. Costs matter little (≈ 0.8 pt/session per
extra point of cost). The 1,150-pt drawdown and −550-pt days at 1 NQ are ~$23k and ~$11k — far beyond a 25K/50K prop account's
$1–2k trailing drawdown: 1 NQ of this book does not fit a prop account; 1–2 MNQ does (the earlier prop sims).
