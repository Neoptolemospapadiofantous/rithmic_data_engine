# Market maker under Lighter's real execution rules — pre-registered 2026-10-25

Founder: "do all" → launch the $250 pilot as soon as possible; "yes" to replaying with the rate limit first. Written BEFORE
the run. Found while preparing the live path (apidocs.lighter.xyz rate limits, docs.lighter.xyz trading fees, 2026-10-25):
a Standard account allows 60 sendTx/sendTxBatch requests per rolling minute (≤ 15 tx per batch), places post-only orders
with 0 ms delay and delays cancels/modifies by 300 ms. `research/programs/mm_quote_rate.cpp` measured the frozen setting
at ~410 transactions a minute over 24 Lighter markets (p95 ~570) — 7–10× the unbatched limit.

Program `research/programs/mm_rate_limit.cpp`; simulator hook `hl::Params::place_latency_ms / cancel_latency_ms` and
`Simulator::set_send_gate` (additive; unset = identical output, checked with mm_sim on tardis_extra).
Scenarios (all coins of a day replayed together, one batching limiter per account, coins split round-robin):
backtest assumption (300/300, no limit) · this box (place 145, cancel 445) with no limit / 60 req/min on 1 account /
5 accounts (main + 4 sub-accounts — Lighter does not say whether the limit is per account or per IP) · a server beside
Lighter (place 5, cancel 305) with the same three. Data: Tardis history (30 tuned coins, holdout from 2025-11-01; 15
never-tuned coins) and Lighter's own recording (partial days).

Read (decision rule for the live path):
- If "this box + 60 req/min, 1 account" keeps ≥ 70 % of the no-limit holdout P&L on BOTH history sets → build the live
  path for one Standard account with the batcher.
- Else, if the 5-account version does → build it for main + 4 sub-accounts (and confirm with Lighter support that the
  limit is per account).
- Else Standard cannot carry the strategy at 24 markets: report how many markets fit, and the Premium alternative
  (0.4 bps maker fee) is judged from mm_robust's fee rows.
The Lighter recording is reported but too short to decide anything.

## Results (run 2026-10-25 ~23:00 Cyprus, after the text above)
Books are stored with their top 5 levels so a whole day of coins fits in memory. On Tardis this is NOT neutral: with 25
levels the program reproduces mm_robust exactly (2 coins: −$52 = −$52), with 5 it reads −$39, and the never-tuned
baseline reads +$307 instead of +$341. Every scenario below uses the same 5-level books, so they compare like for like;
absolute dollars differ from mm_robust's.

| scenario | tuned 30 coins: holdout $ (bps) | never-tuned 15: $ (bps) | Lighter partial day $ | mean wait |
|---|---|---|---|---|
| backtest assumption 300/300, no limit | +397 (0.47) | +307 (0.55) | −26 | — |
| this box 145/445, no limit | +276 (0.30) | +239 (0.39) | −36 | — |
| this box + 60 req/min, **1 account** | **−112 (−0.12)** | **−29 (−0.05)** | −45 | 683 / 520 ms |
| this box + 60 req/min, **5 accounts** | **+338 (0.37)** | **+237 (0.39)** | −39 | 68 / 10 ms |
| near server 5/305, no limit | +392 (0.44) | +296 (0.50) | −38 | — |
| near server + 60 req/min, 1 account | −2 (0.00) | −63 (−0.10) | −36 | 697 / 527 ms |
| near server + 60 req/min, 5 accounts | +346 (0.38) | +303 (0.51) | −42 | 71 / 11 ms |

Read by the rule: one Standard account keeps < 70 % (it loses) on both history sets → FAIL. Five accounts (main + 4
sub-accounts, coins split round-robin) keep ≥ 70 % on both (122 % / 99 % of no-limit at this box) → **build the live path
for main + 4 sub-accounts**, and confirm with Lighter support that the 60/min limit is per account, not per IP or per L1
address. A server beside Lighter adds +2 % to +28 % at 5 accounts. Lighter's own books lose in every scenario (one
partial Sunday) — the rate limit is not why.
