# Manual playbook — trading the system's strategies by hand

What the automated side trades, written so a person can do the same thing from a chart. NQ futures (MNQ for size), all
times **New York (ET)**. Nothing here is live in the system yet: every strategy below is on paper or in a dry executor, and
going live — automated or by hand — is the founder's call.

Evidence and rules: `config/working_fleet.json`, `research/README.md`, `data/edge_lab/reports/`. Behaviour maps behind
the "why": 2026-10-17 (scratchpad `behavior.txt`, `behavior2.txt`).

---

## 1. The numbers you need every morning (before 09:30)

| What | How | Used for |
|---|---|---|
| **ATR** | Average of the last **14** cash-session ranges (09:30–16:00 high − low), in points | Every threshold and stop |
| **Yesterday's close position (IBS)** | (yesterday's 16:00 close − its low) ÷ (high − low) | Weak-day long |
| **Yesterday's VIX close** | CBOE VIX at 16:15 the day before | Weak-day long |
| **Gap** | (09:30 open − yesterday's 16:00 close) ÷ ATR | Gap-agree cascade |
| **Overnight range** | High − low from 18:00 to 09:29, ÷ ATR | Expect a wide day when > 0.8, quiet when < 0.4 |
| **News today?** | CPI / jobs / PPI at 08:30, FOMC at 14:00 | Days that run wider and reverse (section 5) |

Compute these with a calculator or a spreadsheet before the open. The automated side uses exactly these definitions.

### Worked example — Monday 2026-10-19

- **ATR:** 14 ranges from 09-29 to 10-16 average **≈ 356 points**.
- **Cascade thresholds:** 0.25 ATR ≈ **89 pts**, 0.30 ATR ≈ **107 pts**.
- **Stop:** 0.25 ATR ≈ 89 pts, which is **$178 per MNQ**. The 2R target is about 178 pts.
- **Friday 10-16:** closed 29,379.25, inside a range of 29,268.75–30,045.75. IBS = 0.14, so **yesterday was weak**.
- **VIX:** Friday's close is not in our file yet. It was 16.46 on Thursday.
  - If it closed **at or above 20**, the weak-day long is on.
  - If below 20, it is off.
- **It's a Monday,** so the Monday long is on.

---

## 2. The four strategies

### A. Strong-open cascade — the main edge (NQ)

| | |
|---|---|
| **Volatility read** | ATR sets everything. No other volatility filter: the ATR stop already scales with the market (2026-10-17 test: edge per unit of risk is the same on quiet and wild days). |
| **Direction** | The way price has moved from the **09:30 open**. Up → long, down → short. |
| **Signal to wait for** | Price far enough from the open at a check time (first one that fires, **one trade a day**): |
| | • **09:50** — at least **0.25 ATR** from the open |
| | • else **10:30**, **11:00**, **11:30** — at least **0.30 ATR** from the open |
| **Entry window** | At the check minute (09:50, 10:30, 11:00, 11:30), market order. No 12:00 check: tested 2026-10-17, it loses in every period. Read the price at that minute — no chasing between checks. |
| **Stop** | **0.25 ATR** from the entry. (Tested 2026-10-17: a stop beyond the morning's opposite extreme is NOT better — the gain is just a wider stop, and with the VWAP/stall exits it is worse.) |
| **Target** | **2R** (twice the stop distance). |
| **Exit early** | (1) a completed **5-minute bar closes back through the session VWAP** (against you) → out at the next bar; (2) **60 minutes** after entry and never **+0.5R** in favour → out. |
| **Flat** | 15:55 at the latest. |
| **Size** | Evaluation 8 MNQ · funded 2 MNQ (Flex 50K plan). |
| **Expect** | ~140 trades a year, wins ~41 %, +4.8 pts/trade after costs base (+5.8 with the VWAP/stall exits). Losing streaks of 6–8 are normal. |

**Gap-agree version (stronger, half the trades):** take the cascade only when the overnight gap points the same way by
at least **0.1 ATR** (long needs gap ≥ +0.1 ATR, short needs gap ≤ −0.1 ATR). +8.6 pts/trade, ~60 a year. On paper as
`so_cascade_2r_gapagree`.

### B. Weak-day long (NQ)

| | |
|---|---|
| **Volatility read** | **VIX at yesterday's close must be ≥ 20.** Fear is what makes the bounce; below 20 there is almost no edge. |
| **Direction** | Long only. |
| **Signal to wait for** | Yesterday was weak — **any one** of: closed in its bottom 30 % (IBS ≤ 0.3) · RSI(2) of daily closes ≤ 20 · two lower closes in a row · yesterday's cash session fell ≥ 1 ATR open to close. |
| **Entry window** | **09:30** open, one entry. |
| **Stop** | None (every stop tested made it worse). That means the size must be small enough to take a 2–3 ATR day against you. |
| **Exit** | **15:55**. |
| **Size** | Small. Paper/dry only so far. |
| **Expect** | ~50 trades a year, wins ~55 %, +13 pts/trade. Lumpy: one year (2020) made ~45 % of the profit. |

### C. Monday long (NQ) — newest, paper since 2026-10-17

| | |
|---|---|
| **Volatility read** | None needed. It earned most on Mondays after a wide overnight or a VIX jump. |
| **Direction** | Long. |
| **Signal to wait for** | It is Monday. |
| **Entry window** | **09:30** open. |
| **Stop** | None. Same warning as the weak-day long. |
| **Exit** | **15:55**. |
| **Expect** | ~45 trades a year. The edge is a post-2019 effect (flat 2010–18). Treat as a candidate until it has forward results. |

### D. ES cascade (as MES) — optional second market

Same rules as A, on ES/MES. Smaller edge (+1.1 ES pts/trade). It moves with the NQ cascade (monthly corr 0.57), so it
adds size rather than diversification.

---

## 3. When two strategies fire on the same day

- Weak-day long and Monday long are both **long at 09:30** — take one position, not two (or size the sum deliberately).
- The **cascade and the gap retest may both trade the same day if they agree in direction** (tested 2026-10-17: on a 25K account more paid and fewer blown accounts). Never opposite.
- **1–3 trades a day, all in the first trade's direction.** A 2nd or 3rd trade only after a **winner**: the next plan setup, or a
  5-minute bar that touches VWAP and closes back in the day's direction (before 13:55, stop 0.25 ATR, 2R). After a loss, done for the
  day. Tested 2026-10-17 (research/preregistered/2026-10-17_reentry.md): re-entries after a win +5.7 pts/trade vs −0.8 for random
  times, +0.9 / +12.7 / +11.4 by era for the VWAP version — real, but on a 25K account they add P&L without making the first payout faster.
- If a **long hold is on** and the cascade signals **short**, **skip the cascade short**. Never hold opposite positions —
  same account or across accounts (Tradeify FTA §6.7 bans opposite positions against any other account).
- A cascade **long** on a long-hold day is allowed (same direction), but it adds to the same risk.

## 4. The daily routine

| ET | Do |
|---|---|
| Night before / 08:00 | ATR, yesterday's IBS, VIX close, news calendar. Decide: weak-day long on/off, Monday long on/off. |
| 09:29 | Note the overnight range and gap. |
| 09:30 | Weak-day / Monday long: buy the open. Write down the 09:30 open price — the cascade measures from it. |
| 09:50 | Cascade check 1 (0.25 ATR). |
| 10:30 · 11:00 · 11:30 | Cascade checks 2–4 (0.30 ATR) if no trade yet. |
| 12:00 | No cascade trade by now = no cascade today (the 12:00 check loses money). |
| While in a cascade trade | Watch each completed 5-min bar vs VWAP; set a timer for entry + 60 min (0.5R check). |
| 15:55 | Flat everything. |
| After the close | Log every trade (time, direction, entry, stop, exit, reason). |

## 5. What the research says about the day (use for judgement, not as signals)

- **Extremes come early.** About 30 % of days set their high or low in the first 30 minutes, and by **10:30** one of the
  day's two extremes is already final 78 % of the time. On a strong morning (> 0.5 ATR from the open at 10:30) the
  morning's opposite extreme holds all day 83 % of the time.
- **Wide or quiet is partly predictable.** An overnight range > 0.8 ATR → the day averages 1.24 ATR; < 0.4 ATR → 0.78. A first hour
  wider than 0.7 ATR → half of those days end above 1.3 ATR.
- **News days.** CPI / jobs / FOMC days are wider. The 08:30 CPI move and the first 30 minutes after FOMC tend to
  reverse, but **none of the fades passed** as a strategy. The cascade's follow-through is weaker on CPI and FOMC days
  (small sample) — trade them smaller, not differently.
- **Drift.** NQ rises ~+7.6 bp a day on average. Shorts need a reason; they are not the mirror of longs.
- **Down days are bigger than up days** (1st percentile −1.9 ATR vs 99th +1.6 ATR) — that is the risk in the no-stop longs.
- **What does not work** (tested, failed): time-of-day direction, month of year, turn of month, Gao last-30-min,
  chart patterns, indicators, price-level fades/breaks, Asia/London session trades, macro (yields, dollar, VIX change),
  NQ/ES lead-lag, CPI/FOMC fades.

## 6. Rules for doing it by hand

1. Same rules as the system — no discretion on entries. Discretion is only ever "skip" (news, size), never "add".
2. Write the plan before 09:30; if a number is missing (e.g. VIX), the trade is off.
3. Stops go in as orders the moment you are filled.
4. Flat by 15:55 (Tradeify: flat by 16:45 ET every day, no overnight holds).
5. Compare your trades with the paper fleet's (`scripts/working_fleet.sh`) once a week — differences are execution, and
   execution is the cost that kills small edges (the measured live cost is ~1.5 pts per MNQ round trip).

## 7. All sessions (the 24-hour trading day, 18:00 → 17:00 ET)

| Session (ET) | Volume | Drift / day | What happens | Tradable edge | By hand |
|---|---|---|---|---|---|
| Asia 18:00–03:00 (Globex reopen, Tokyo 20:00) | 8 % | +3.1 bp | Quiet slow climb, ~0.1 ATR per half hour | None reliable: the 18:00 cascade passed edge_lab but is fragile (settings lose on average); Tokyo cascade, Asia-range breaks/fades, opening drives failed; holding long = the drift, fails the random-day control | No trade; note the Asia high/low |
| London 03:00–08:00 | 8 % | +0.7 bp | 03:00 is the busiest overnight half hour; often runs one side of the Asia range | London cascade / drive / range trades failed. Candidate on paper: `lf_asia_sweep` (fade a run through the Asia high/low that closes back inside with order flow) | No trade until ~40 paper trades |
| US pre-market 08:00–09:30 | 6 % | 0 | 08:30 releases; CPI moves tend to reverse | CPI / jobs fades failed | Read only; record overnight range and gap |
| **New York 09:30–16:00** | **76 %** | **+3.6 bp** | All the volume and range | **All four strategies** | Section 2 |
| After-hours 16:00–17:00 | 3 % | +1.1 bp | Thin; big-tech earnings at 16:00 | Nothing passes | Be flat (Tradeify: by 16:45) |

The overnight sessions are context, not trades.

Published as a page (with a morning calculator): https://claude.ai/artifact/L5tcGniUqD7LWF1GSPLqer

## Momentum + pullback (2026-10-17) — paper candidates, on the Plan page and in Telegram

Founder: "find the direction, the momentum, enter on the momentum with EMA". Tested family (research/README.md ledger):
1. Direction = the PLAN's direction (first cascade check or gap retest) — the box says "plan agrees" when it matches.
2. Momentum = the last 45 one-minute bars moved fairly straight one way (|net| / path ≥ 0.3), price on that side of VWAP and the open.
3. Entry = a 5-min bar that comes within 0.05 ATR of VWAP and closes back in the direction (best entry tested; EMA-20 touch second).
4. Stop 0.25 ATR, break-even at +1R, hold to 15:55; ≤ 3 a day, one at a time, done after a loss; no entries after 13:55.
Engine replay, $ per MNQ per trade (2010-18 / 2019-22 / 2023-24): with plan agreement +4.62 / +15.28 / +36.13; without +3.60 /
+11.62 / +15.34; the 30-min EMA-20 version −1.67 / +5.61 / +14.44. New York only (ES and the night sessions fail). Small edge,
big swings: size so one stop ≈ $600 (3 MNQ at an 82-pt stop). Alerts: "MOMENTUM + VWAP PULLBACK — PLAN AGREES" is the strong one.

## One-week payout sprint (founder 2026-10-17: "we want to make it happen") — simulated, not a tested edge

Searched (scratchpad sprint.cpp / sprint2.cpp, plan setups 2019-24, sizing by $ risk, costs 1.5 pts, days merged pessimistic):
target 0.5–3R × 1 trade/day or again-after-a-win × eval / funded / Lightning risk × Select Daily / Flex / Growth / Lightning 25K–50K,
plus several accounts started on consecutive days.

| Vehicle | Best setting | First payout within 7 / 10 / 20 trading days | Blown within 7 days |
|---|---|---|---|
| Lightning 25K (no evaluation) | 1 plan trade a day, target 0.5R, risk $600 | 4.3 % / 8.8 % / 20.3 % | 33 % |
| Lightning 50K | same, risk $1,200 | 5.2 % / 11.5 % / 22.1 % | 39 % |
| Select Daily 50K | target 1.5R, eval risk $900, funded $1,800 | 2.7 % / 5.5 % / — | 44 % |
| Select Daily 25K | target 1.5R, eval risk $450, funded $1,200 | 1.8 % / 3.5 % / — | 38 % |
| Flex / Growth | — | 0 % within 7 days (too many required days) | |
Several accounts started on consecutive days add little inside one week (later starts run out of days).
Lightning rules used (Tradeify help centre 2026-10-17 + a third-party copy of the payout table): EOD trailing $1,000 / $2,000,
payout 1 at +$1,500 / +$3,000 with the best day ≤ 20 % of the profit, ≤ $1,000 / $1,500 withdrawable, no minimum days.
The 25K result is knife-edged: a 0.5R win at $600 risk ≈ $300 = exactly the 20 % cap; at $500 risk it falls to 0.2 % within 7 days.

Adding the 09:30 weak-day long (weak close + VIX ≥ 20) and the Monday long as sprint signals — same 0.25-ATR stop, 0.5R target —
raises Lightning 25K to 6.7 % / 12.8 % / 24.6 % (≤ 7 / 10 / 20 days), blown ≤ 7 days 34 % (Lightning 50K 8.0 % / 16.2 % / 26.6 %).
Holding the weak-day long to 15:55 instead is worse (2.0 % / 5.7 % / 15.1 %). With 1 pt worse fills: 4.0 % / 9.9 % / 20.6 %.
The Plan page "Payout sprint" panel shows today's sprint trade (source, entry, stop, target, MNQ for the chosen account) and keeps
a win / loss log in the browser (winning days, P&L vs the goal, best day vs the 20 % cap, drawdown room, "request the payout").

Lightning 25K sprint, by hand:
1. Only the day's FIRST signal: the 09:30 weak-day / Monday long when its card is ON, otherwise the first cascade check or gap
   retest (Plan page "Payout sprint" → TRADE NOW). One trade a day, then done.
2. Size: micros = floor(600 / (stop pts × 2)) — at a 82-pt stop that is 3 MNQ (risk ≈ $490); stop = the plan's stop (0.25 / 0.35 ATR).
3. Target 0.5R (half the stop distance) — a win ≈ $250–300, never more than $300 in a day (20 % rule).
4. A loss ≈ the whole day's risk; two losses leave about $0–$100 of drawdown → stop the account.
5. Five winning days → +$1,500 → request the payout (≤ $1,000).
