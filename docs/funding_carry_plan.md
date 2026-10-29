# Funding carry — plan, requirements audit and KPIs (2026-10-28)

Founder, 2026-10-28: "test funding premium" → "create a professional necessary list, audit it, keep KPIs for it and
let's build it". This file is the operating plan. Research rules live in the pre-registration
`research/preregistered/2026-10-28_funding_carry.md`; nothing here may loosen them.

## 1. What the strategy is

Perpetual futures pay **funding** every hour (Lighter, Hyperliquid) or every 8 h (Binance) between longs and shorts.
**Carry** = hold the same coin SHORT on the venue paying the most to shorts and LONG on the venue paying the least,
so price moves cancel and the funding difference is kept. Income is slow (a few to ~20 % a year on capital in good
periods); the risks are funding flips, costs, one leg being liquidated in a big move, and venue failure.

**Prop firms are not usable for this** (2026-10-28): carry needs two venues at once, prop rules (e.g. Tradeify FTA
§6.7) ban opposite positions against any external account, each prop account judges its own drawdown so one leg
breaches on an ordinary move, and most firms pay their own swap, not exchange funding. Own capital only.

## 2. Stage gates

| Stage | What runs | Money | Passes when | Who decides |
|---|---|---|---|---|
| **F0 Research** | `funding_lab` on 12 months × 90 coins × 3 venue pairs | none | the pre-registered pass rules on the test half (§5 F0) | the numbers |
| **F1 Paper** | paper carry engine on live funding + live books (entry/exit at real spreads) | none | §5 F1 for 30 days | the numbers |
| **F2 Pilot** | two real accounts, majors only, ≤ 2× leverage, small notional | ≤ $200 | §5 F2 for 30 days, zero hedge incidents | the founder |
| **F3 Scale** | more coins/notional within the risk limits | founder's call | — | the founder |

No stage is skipped. Going from F1 to F2 needs the founder's explicit go, like every live start.

## 3. Necessary list — audited

Status: ✅ have · 🟡 partial · ❌ missing. "Blocks" = the first stage that cannot start without it.

### A. Data
| # | Requirement | Status | Blocks | Notes |
|---|---|---|---|---|
| A1 | 12-month hourly funding history, all 3 venues, all common coins | 🟡 | F0 | `scripts/funding_history.sh` → `rithmic_dex/funding/`; Binance done, Lighter/Hyperliquid downloading |
| A2 | Daily prices for hold-period risk | ❌ | F0 | Hyperliquid daily candles, same script |
| A3 | Live funding (current + predicted) per venue | 🟡 | F1 | Lighter `market_stats` already archived by the recorder; Hyperliquid/Binance not yet |
| A4 | Live top-of-book on every venue for real entry/exit cost | 🟡 | F1 | Lighter + Binance recorders exist; Hyperliquid recorder exists for the MM coins only |
| A5 | Cross-venue basis (mark price gap) history | ❌ | F1 | replaces the flat 5 bps assumption |
| A6 | Delisted coins over the year (survivorship) | ❌ | F2 | today's listings only → results biased upward; stated in every report |

### B. Research
| # | Requirement | Status | Blocks | Notes |
|---|---|---|---|---|
| B1 | Pre-registration written before results | ✅ | F0 | incl. addendum fixing APR definition before results |
| B2 | Deterministic backtest engine with tests | 🟡 | F0 | `funding_lab` (this build) |
| B3 | Causal signal (only funding already paid) | 🟡 | F0 | enforced and tested in `funding_lab` |
| B4 | Tuning/test split, test read once | 🟡 | F0 | enforced by the tool: the test half is scored only for the chosen cell |
| B5 | Cost model per venue (fee, spread, basis) | 🟡 | F0 | assumed; measured in F1 |
| B6 | Robustness: without best 3 coins, per month, per pair | 🟡 | F0 | in the report |

### C. Execution (F1 paper, F2 live)
| # | Requirement | Status | Blocks | Notes |
|---|---|---|---|---|
| C1 | Accounts: Lighter (have) + one hedge venue | 🟡 | F2 | Hyperliquid (wallet, USDC on Arbitrum) or Binance (KYC; check futures availability for Cyprus residents) |
| C2 | Order path per venue: place, cancel, IOC, reduce-only | 🟡 | F2 | Lighter ✅ (`mm_live` signer/sender); Hyperliquid/Binance ❌ |
| C3 | Two-leg entry: second leg within N s or unwind the first | ❌ | F2 | the "one leg filled" risk |
| C4 | Hedge-ratio check: \|net delta\| ≤ 1 % of notional | ❌ | F2 | reconcile both venues every 10 s |
| C5 | Funding-schedule awareness (never close 1 min before a payment you are owed) | ❌ | F1 | Binance 8 h vs hourly venues |
| C6 | Separate Lighter API key / account from the market maker | ❌ | F2 | the MM's request budget and nonce stream stay untouched |

### D. Risk and margin
| # | Requirement | Status | Blocks | Notes |
|---|---|---|---|---|
| D1 | Leverage cap per leg (≤ 2× pilot, ≤ 3× later) | ❌ | F2 | config-enforced |
| D2 | Liquidation distance ≥ 3× the coin's largest daily move in 12 months | ❌ | F2 | per coin, refuses coins that fail |
| D3 | Margin rebalance procedure between venues + its measured duration | ❌ | F2 | hand procedure first, timed |
| D4 | Auto-unwind both legs if one venue's API / stream is down > 60 s | ❌ | F2 | |
| D5 | Kill switch: flatten both legs | ❌ | F2 | same KILL-file convention as `mm_live` |
| D6 | Loss stops: daily and total, on combined equity | ❌ | F2 | |
| D7 | Venue concentration: ≤ 50 % of carry capital on one venue | ❌ | F3 | |
| D8 | Coin filter: min open interest / volume on both venues | ❌ | F1 | thin coins excluded |

### E. Monitoring and operations
| # | Requirement | Status | Blocks | Notes |
|---|---|---|---|---|
| E1 | Status file + dashboard card (/mm) | ❌ | F1 | |
| E2 | Telegram alerts: entry/exit, flip, margin < threshold, hedge drift, venue down | ❌ | F1 | via grid-notify |
| E3 | Daily KPI report (§5) | ❌ | F1 | timer, like `mm-gates` |
| E4 | Journal of every decision and order | ❌ | F1 | append-only JSONL |
| E5 | Runbook: start, stop, rebalance, venue outage, delisting | ❌ | F2 | |

### F. Security, accounting, legal
| # | Requirement | Status | Blocks | Notes |
|---|---|---|---|---|
| F1 | Keys only in `~/.config/<venue>` (0600, outside git), never `.env`; no withdraw/transfer permission | 🟡 | F2 | Lighter key ✅; others ❌ |
| F2 | Funding received reconciled with venue statements | ❌ | F2 | |
| F3 | Tax/accounting record of funding income per venue | ❌ | F3 | founder |
| F4 | Venue terms: residency, hedging, API use | ❌ | F2 | founder to confirm per venue |

## 4. Risks it must survive (from the 2026-10-28 review)

Funding flip and whipsaw · costs on small gaps · basis at entry/exit · one leg liquidated in a big move ·
margin not movable in time · auto-deleveraging · venue outage / delisting / rule change · settlement-time mismatch ·
one leg filled · shared Lighter account with the MM · survivorship and one-regime bias · crowding.
Each maps to a requirement above (B5, A5, D2–D4, C3–C6, A6, D7).

## 5. KPIs and thresholds

### F0 Research (from the pre-registration; test half, chosen cell)
| KPI | Threshold |
|---|---|
| Net APR on capital (time-weighted) | ≥ 10 % |
| Coins net > 0 (of coins with a trade) | ≥ 60 % |
| Test months positive | ≥ 4 of 6 |
| Net without the best 3 coins | > 0 |
| Reported (no threshold) | round trips, average hold, worst month, APR on peak capital, largest daily move during holds |

### F1 Paper (30 days, live data)
| KPI | Threshold |
|---|---|
| Net APR on capital, after measured costs | ≥ 10 % and ≥ 50 % of the F0 test-half figure |
| Measured round-trip cost vs F0 assumption | ≤ 1.5× |
| Funding captured vs funding expected at entry | ≥ 60 % |
| Hours held while the gap was against us | reported |
| Data freshness (any venue stale > 2 min) | 0 unexplained |

### F2 Pilot (30 days, real money)
| KPI | Threshold |
|---|---|
| Net P&L after fees and funding | > 0 |
| Live vs paper twin, same days | within 30 % |
| Max \|net delta\| | ≤ 1 % of notional |
| Seconds with one leg unhedged | ≤ 30 per entry/exit, 0 otherwise |
| Minimum margin buffer to liquidation | ≥ 3× the day's move |
| Funding received vs venue statement | ± 1 % |
| Hedge / venue incidents | 0 |

## 6. Decision log
- 2026-10-28: plan created; pre-registration written before data; prop firms ruled out; F0 build started
  (`scripts/funding_history.sh`, `funding_lab`).
- 2026-10-28: **F0 FAIL** (`data/funding_lab/report.md`). Chosen cell L 72 h / θ 80 % (tuning APR 34 %) made
  4.0 % APR on the test half; 11/22 coins positive, 4/6 blocks, negative without the best 3 coins. 11 of 12 tuning
  cells lost money (fast signals churn into costs). Only Lighter–Hyperliquid was positive on the test half — a
  post-hoc observation, not a result; it may only be tested on data after 2026-10-28 under a new pre-registration.
  Largest held move 128 % (SKR): alt-coin legs at 3× would have been liquidated. F1 does not start.
