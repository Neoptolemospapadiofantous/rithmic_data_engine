# Lighter market-making plan

Written 2026-10-25. Companion to `docs/market_making_system.md` (how it works). This file holds the objective,
the stage gates with their pass and fail numbers, the capital tied to each gate, the cadence and the decision
log. The numbers are set before the data arrives and change only through the log.

## 1. Objective and constraints

Prove, on paper first, that the frozen setting earns a positive return on Lighter after real costs; then put the
smallest real money on it; then scale with capital, never leverage.

- Venue: Lighter, Standard account only (0 fees). The edge is about +0.5 bps per $ traded; a 0.5 bps maker fee
  erases it (`mm_robust`, 2026-10-25).
- Capital: the founder's own. Prop accounts do not trade crypto.
- Size: $100 quotes, $300 cap per market, 24 markets (the Lighter perps also tested on Hyperliquid history).
- Nothing goes live until its gate passes; the founder decides at every gate.
- Any change to the frozen setting is pre-registered: written here with its expected effect, then holdout-tested
  (`mm_opt` / `mm_robust`), then paper, never straight to live.

## 2. What the evidence says so far (Hyperliquid books, one sample day per month)

| Set | Days | Total | Mean day | Positive days | Worst day | Max DD | Daily Sharpe |
|---|---|---|---|---|---|---|---|
| 30 tuned coins, all days | 24 | +$1,090 | +$45 | 19/24 | −$37 | $63 | 0.81 |
| 30 tuned coins, holdout year | 12 | +$446 | +$37 (95%: +$9 … +$68) | 8/12 | −$37 | $63 | 0.68 |
| 15 coins never used in tuning | 12 | +$341 | +$29 | 10/12 | −$41 | $41 | 0.72 |

Worst simulated day in any test: −$492 (one-setting walk-forward). On ~$7,000 of capital the holdout mean is
≈ 0.5% a day and that worst day ≈ 7%. Caveats: 24 sample days, Hyperliquid books as a stand-in for Lighter, no
funding payments, no market impact. The gates exist to find out whether this survives contact with Lighter.

## 3. Stage gates

Status is computed every day at 00:10 UTC by `scripts/mm_gates.sh` (`mm-gates.timer`) into
`data/mm_gates/gates.json` and `report.md`, with one Telegram line. The dashboard reads the same file.

| Gate | What must be true | Pass | Fail → stop and rethink |
|---|---|---|---|
| **G1 · Data** | Lighter's own books re-test the setting (flat-to-flat replay, Hyperliquid tick unit, Lighter grid) | ≥ 10 complete recorded days (≥ 23.5 h of books); net ≥ +0.25 bps; 10 s markout ≥ −1.0 bps | 10 s markout < −1.5 bps over ≥ 10 days: the edge is not on Lighter |
| **G2 · Paper** | The 24/7 paper engine on live Lighter | ≥ 20 complete paper days (a day with an engine restart does not count — a start resets the engine); cumulative P&L > 0; ≥ 55% positive days; mean day inside +$9 … +$68; loss stop hit ≤ 1 time | cumulative < −$300, or ≥ 2 loss stops |
| **G3 · Pilot** | $250 live: $10 quotes, 15 markets, 2 weeks | real fills within 20% of the paper engine's; real slippage ≤ 1 bps; P&L sign agrees with paper | fill rate under half of paper, or markout 2× worse than paper |
| **G4 · Scale** | $2,000 → $5,000 → $10,000 | each step holds G3's numbers for 2 weeks before the next | markout degrades with size: the strategy is at capacity |

G1 and G2 run in parallel from today. G3 needs Phase 4 (account, key, order signing, server) built during G2.

## 4. Workstreams

| Stream | State | Next |
|---|---|---|
| Data | `lighter-recorder-local`, `hl-recorder-local` running; since 2026-10-25 evening also `extended-`, `aster-` and `paradex-recorder-local` (`dex_recorder`, 32 / 37 / 23 markets) — the three other venues that pass the fee filter | `mm_venues.sh` re-tests the setting on each venue daily (in `report.md`); `mm_opt` / `mm_robust` take the tick-rule option for Lighter archives |
| Engine and risk | `mm-paper-local` running (Hyperliquid tick unit, 300 ms, kill file, loss stop) | first UTC day roll 2026-10-26 00:00 |
| Overview | `mm-gates.timer` 00:10 UTC → Telegram + `data/mm_gates/`; gates dashboard | daily |
| Hardening (Phase 3) | not started | BTC-move pause (Binance fan-out :8197), BTC/ETH hedge, 60–100 markets — each through holdout → paper |
| Venue choice | Extended / Aster / Paradex archives accumulating; Paradex's API book is a shell (its tight book is the retail/RPI tier API orders cannot touch) | after ≥ 10 complete days per venue: rank by `venues.json` net bps and markout at each venue's real maker fee; the best zero-fee venue with no imposed delay gets a paper engine next |
| Regime filters | `mm_regime` run on the Tardis history (2026-10-25) — see the decision log | a filter enters the frozen setting only through pre-registration → holdout → paper |
| Live path (Phase 4) | not started | Lighter account + API key; order signing (Lighter ships Python/Go SDKs: a Go sidecar or a C++ port — founder's call); server near Lighter (measure ping first) |

## 5. Capital tied to gates

| Stage | Capital | Quote | Markets | Worst simulated day |
|---|---|---|---|---|
| G1–G2 | $0 | $100 (paper) | 24 | — |
| G3 | $250 | $10 | 15 | ≈ −$25 (scaled) |
| G4 step 1 | $2,000 | $25 | 24 | ≈ −$125 |
| G4 step 2 | $5,000 | $50 | 24 | ≈ −$250 |
| G4 step 3 | $10,000 | $100 | 24 | −$492 |

Leverage is never used to reach a size: losses scale with position size, not margin.

## 6. Cadence

- Daily, 00:10 UTC: gates recomputed, Telegram line, `report.md`.
- Weekly (Monday): review `report.md` and the dashboard; agenda = gate status, risk events (`mm_paper_events`),
  proposed changes with their pre-registration, decision log entry.
- At every gate: founder decision, logged below.

## 7. Risks

- Lighter's books may not carry the edge measured on Hyperliquid (G1 decides).
- Funding payments and our own market impact are not modelled (G3 measures both for real).
- Lighter's Standard-account delay is 200 ms by design; a close server removes only the network part.
- Regulatory: unlicensed decentralized perps are a grey area for an EU resident; Cyprus is not on Lighter's
  restricted list (terms read 2026-10-25). Profits are taxable.
- Operational: one box, one connection; the recorders refuse to start without the second drive mounted.

## 8. Decision log (append only)

- 2026-10-27 — **Founder: "move the boundary now and fix the paper limiter".** (1) Judge epoch 2: 25 Oct is excluded on
  every venue (`data/mm_lab/excluded_days.json`, damaged archive), which left Lighter's epoch-1 in-sample at ~3 h; every
  venue (lighter, extended, aster, paradex) is re-frozen with the same 30 % time rule from the first clean day:
  in-sample 26 Oct 00:00 → 23:00 UTC, holdout from 26 Oct 23:00. Epoch-1 values are kept in each entry (`epoch1`) and in
  `holdout_boundaries.epoch1.json`; the registry (alpha spending) carries on, k is not reset. Disclosed contamination:
  26 Oct was seen descriptively before the move (G1 +2.65 bps; four exploratory scratch screens on it after), and the
  paper engines' 27 Oct P&L (holdout period) was read. (2) Paper now runs Lighter's request limit: one
  `hl::RequestGate` (moved from mm_lab, same code as the judge) shared by all 24 markets — 60 requests/min, ≤ 15 tx each —
  and the measured Standard delays 145 ms place / 445 ms cancel on `paper` and `paper_hl` (`paper_220` keeps 220/220 as
  the near-server scenario). Before, paper changed quotes ≥ 306 times/min unlimited; the judge measured the limit at up to
  ~1 bp. The start line carries the new setting, so G2 voids 27 Oct and its count starts 28 Oct. Same day: a G2 day now
  counts with ≤ 2 restarts that do not change the setting (P&L stitched across runs).
- 2026-10-26 — Gates re-based to the $11 setting (consequence of the decision below, proportional ×0.11): G2 mean-day
  range +$9…+$68 → **+$0.99…+$7.48**, G2 fail line −$300 → **−$33**, paper daily loss stop $150 → **$16.50**; G1 replays at
  $11 with Lighter's size grid/minimums. Paper day rows are mark-to-market (carried inventory); a restart day stays
  excluded and a discarded carried position is now recorded as an event. Day count for G2 restarts on the new setting.

- 2026-10-26 — **Founder: "apply both now and build the HL shadow".** The frozen setting changes for paper: quote size
  $100 → **$11** (the live pilot's size; on the mm_lab starter run the loss fell −$44.77 → −$4.47 and −4.37 → −3.30 bps
  in-sample, and $10 quotes were the only non-negative cell on history) and **no 00:00 UTC flatten** — inventory is
  carried and marked to mid instead (the flatten was accounting only: −$2.59 on 10-25; the live engine never had it).
  Daily rows become mark-to-market rather than flat-to-flat. A third paper engine `paper_hl` quotes around a
  Hyperliquid-anchored fair (k = 1; in-sample −0.94 bps vs −3.67 for the frozen setting on the same 15 coins) as a
  SHADOW only. G2's day count restarts with the new setting; skew is not adopted (worse in-sample). Nothing here has
  passed mm_lab or a holdout — these are cost cuts on paper, measured on future days.

- 2026-10-26 — **The +0.5 bps edge was a simulator artefact.** An independent audit found four optimistic flaws in
  `hl::Simulator` (a trade through our price filled the whole order whatever its size; arrival was checked against a
  later book; the end-of-day close was free; no fills when the book ran over our quote). Fixed as `hl::FillModel`
  (env `MM_FILLS`; default NoCross; `legacy` reproduces every old number exactly; `full` is a stress bound only), and
  reviewed by a second independent agent. Under NoCross the frozen setting loses on every dataset: HL history holdout
  −$399 (−0.98 bps, 3/12 days up) / never-tuned coins −$155 (−0.67 bps); Lighter Standard rate limit −$901 to −$1,124;
  Extended/Aster/Paradex negative. The only non-negative cell is $10 quotes (+$44 / +$41, ~+0.5–0.7 bps, 9–10/12).
  Same day: two archive-reader bugs fixed (a truncated zstd frame from a recorder restart stopped the whole file — a
  quarter of Lighter BNB 10-25 was never replayed; the day-roll snapshot's repeated update was read as a gap), so
  every Lighter/venue replay before 2026-10-26 read only part of each day. Lighter 10-25 with all fixes: −3.97 bps over
  12.9 h. Founder chose "edge first": the live gateway build is paused (wave-1 specs: signer proven offline, tx and
  account-feed specs, 41-test safety requirements). Lighter testnet is never to be used (founder: prices inaccurate);
  live checks will be far-from-market mainnet probes. A $10 mainnet plumbing test was discussed, not started.
  Lead-lag study (pre-registered): Lighter LEADS Hyperliquid on receive time (~800 ms); an HL-anchored quote guard
  passed its pre-registered rule on ~1 day but the holdout baseline was already positive and the gain is a level
  effect, not timing — to be re-tested on ≥ 5 more recorded days before anything else.

- 2026-10-26 — Neither 2026-10-25 nor 2026-10-26 counts for G1/G2: on 10-25 22:33–22:49 local the box ran out of RAM and
  swap (Chrome, Postgres, Claude sessions, 18 paper engines) and the kernel killed the recorders (OOM score 700) and
  both MM paper engines (500) over and over — they use < 100 MB each, so killing them freed nothing; then the box
  rebooted 10-26 09:48. All five recorders and both MM engines now run at OOMScoreAdjust 100 (the lowest a user unit
  may set, same as the tick collector), so a memory crunch hits the large processes first. The first day that can count is 2026-10-27 UTC.

- 2026-10-25 — Regime-filter study (`mm_regime`, `research/mm_regime/2026-10-25.md`): time of day and volatility carry
  no stable signal; a busy tape (> 20 trades/min) and a spread under 2 bps lose in every period and every coin set.
  The pre-registered gate (quote only while trades/min ≤ 20 and spread ≥ 2 bps) left the unseen coins' dollars
  unchanged (+$344 vs +$341) on 17% less quoting time, 0.64 vs 0.60 bps. Decision: NOT added to the frozen setting
  now; it is the first pre-registered candidate for Phase 3 (expected: same P&L, less time at risk), to be judged on
  Lighter's own books once G1 has its 10 days, then on paper.
- 2026-10-25 — Venue survey (fee ≤ 0.5 bps maker, order book, Cyprus account): the four zero-maker venues are all
  unlicensed DEXs — Lighter, Extended, Aster, Paradex; every MiFID-regulated route (Backpack EU Limassol, OKX X-Perps,
  Kraken EEA) is 1.5–2 bps maker and loses at our edge. Decision: record all three new venues from today
  (`dex_recorder`, same archive shape, same simulator), compare them daily in `report.md` at their real fees, and
  put a paper engine on the best one only after ≥ 10 complete days. Paradex noted: API-visible books are thin
  shells (AAVE 24 h volume 0; SOL API spread 1.8%), so its G1 will likely show few fills — that is the answer, not a bug.
- 2026-10-25 — Shadow paper engine started at 220 ms (label `paper_220`) beside the official 300 ms one (label `paper`);
  G2 is judged on `paper`; the gap between the two is the measured value of the server.
- 2026-10-25 — Latency grid run (CHANGES.md): the delay curve is flat below 100 ms and falls from 200 ms on; decision:
  put the Lighter engines on a VPS in Lighter's region (expected +15–40% over today's 300 ms) and start a Paradex
  recorder on a Tokyo VPS in parallel, because a zero-fee venue with no imposed delay models at about twice today's
  result if its books carry the edge.
- 2026-10-25 — Plan written; gate numbers set from the holdout evidence. Paper engine restarted on the
  Hyperliquid tick unit at 14:55 UTC+2 after validation found Lighter's grid differs on 8/24 markets; the
  paper record starts there. Frozen delay 250 → 300 ms (the backtests' value).

- 2026-10-28 — `paper_100` (unit `mm-paper-100-local`): the optimised setting at **$100 quotes** beside `paper_opt` at $11, same
  measured live delays (314/614 ms) and rate limit. Question: do fills and markout scale with size? ($1–2k/month at +0.5 bps
  needs ~$600k–1.2M filled per day ≈ $100–200 quotes × 30 markets; if fills scale < 50 % the plan's volume doubles.) Judge after
  10 paper days on fills/day, net bps and 10 s markout vs `paper_opt`; a worse markout at $100 means size is adverse selection,
  not income. Paper only, no money.

## RL phase 1 — contextual bandit on counterfactual labels (2026-10-28, founder: "lets build it")
Built: `mm_rl_label` (one row per 10-s window: the state at the window start + the markout-adjusted $ each quoting action — always
quote at 4/5/6/8 ticks, defences off, and the frozen policy — earned from its fills inside the window; honest fill model, Lighter's
grid) and `mm_rl_judge` (per side × action ridge on the state; policy = argmax over actions ∪ pull; walk-forward by day and unseen-coin
splits; a shuffled-train control). Rule stated before the run (chat, 2026-10-28 22:40 EET): the learned policy counts as information
only if its gain over "always width 5" on the test split EXCEEDS the gain of the same policy fitted on shuffled labels by more than the
noise, on BOTH the walk-forward and an unseen-coin split; otherwise the state carries nothing and only the action set does.
**Result (6 Lighter coins × 2026-10-26…28, 145,701 windows):** walk-forward (train 26–27, test 28): learned +$0.08 vs always-5 −$0.72 —
but the shuffled-train control scores +$0.34 (gain +0.80 real vs +1.06 shuffled). Unseen coins CRV/OP: +0.35 vs +0.42; UNI/DOT: +1.36
vs +1.36. The policy's edge is entirely "pick width 8 and pull a third of the time" — the best single action in hindsight is 8 on every
split — and the state features (spread, imbalance, depth, tape, range, 1 s/5 s move, time since jump, hour) add nothing the shuffle
does not. The per-window oracle (perfect foresight) is only +$4.5 on $11k filled ≈ 4 bps — the ceiling for ANY state policy on this
action set. **Verdict: state-dependence NOT demonstrated; the dataset says "quote wider" (8 vs 5 ticks), which is a one-knob change
to test on paper, not a learner.** Re-run when the Lighter archive holds ≥ 20 days and the Binance-gap feature (the only signal the
fair-price lab found, R² ~1 %) is in the row.
**HL history (Tardis, 24 month-first days 2024-11 → 2026-10, AAVE/AVAX/DOT/OP, 829k windows, 1.5 bps maker fee):** every quoting
action loses on the test days (always-5 −$543 = −20 bps of $268k filled; best single action, width 8, −$386; the frozen policy −$62);
the learner's answer is to PULL 94–98 % of the time (reward 0), and the shuffled-train control does exactly the same (+$538 vs +$541
over always-5). The oracle is +$204 on 138k windows ≈ 8 bps — the most ANY state policy could extract. Same reading as the Lighter
days: no state signal in these features; on a venue with a maker fee the right action is not to quote, which the simulator already
said in October. Two artefacts worth knowing: Tardis has no UNI/CRV history (the "CRV,OP" split tested OP alone), and the Tardis
books are 5 levels deep (the depth features are truncated there).

- 2026-10-29 — **Founder: "do all"** after the 10-28 box reset stranded 1.23 AVAX (~20 h, −$0.36): guardian flatten on a
  second machine (`scripts/mm_guardian.sh`), nightly market ranking with a walk-forward check (`scripts/mm_market_rank.sh`,
  adopt rule in the 10-28 pre-registration amendment), E2 wider-quotes paper twin `paper_w7`, Tokyo launch script
  (`scripts/aws_tokyo_launch.sh`; Lighter's origin is AWS Tokyo, 10 ms vs 240 ms from home). Extended live paper engine
  (`mm_paper --venue extended`) in progress. Live `mm-live@avax` enabled on boot.
