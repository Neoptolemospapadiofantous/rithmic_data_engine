# The book's families on other markets — pre-registered 2026-10-28 (runs when the data lands)

Founder: "we need more strategies … what other uncorrelated families … 3–5 trades a day with winners". Measured first
(session scratchpad, walk-forward trades 2019 → 2026-10): the four NQ families are ≈ uncorrelated with each other
(monthly pairwise −0.09 … +0.25) and every remaining NQ PASS idea is +0.25 … +0.54 correlated with the book — NQ is
mined out. ES mirrors run at +0.22 … +0.53. The remaining diversification is OTHER MARKETS with their own flows:
daily correlation with NQ ≈ 0–0.3 for rates / metals / energy. Written BEFORE any data is bought or any run.

## Data (founder's purchase)
Databento **GLBX.MDP3**, schema **ohlcv-1m**, stype continuous **`.c.0`** (calendar roll, exactly as the owned NQ / ES
files: `dataset=GLBX.MDP3 schema=6 symbol=ES.c.0`), monthly DBN+zstd files, **2010-07-01 → today**, one directory per
symbol under `/home/theone/Desktop/bot/data/raw/<sym>-ohlcv-1m/` (read-only, like the others):

| symbol | why | micro traded | $/pt (micro) | tick | initial cost/contract (4 ticks + $1.82, in points) |
|---|---|---|---|---|---|
| **GC** | metals flow, corr ≈ 0 | MGC | 10 | 0.10 | 0.42 |
| **CL** | energy flow, corr ≈ 0 | MCL | 100 | 0.01 | 0.042 |
| **ZN** | rates flow, corr ≈ −0.2 … 0 | (10Y micro-yield is a different contract — research only, trade later) | — | 1/64 | 0.064 |
| **RTY** | small caps, corr ≈ 0.7 but its own open flow | M2K | 5 | 0.10 | 0.44 |
| **YM** | corr ≈ 0.8 — lowest priority, cheapest | MYM | 0.5 | 1.00 | 4.4 |
| 6E | FX, corr ≈ 0 — if the budget allows | M6E | 1.25 | 0.00005 | 0.0002 + fee |

Costs above are the NQ precedent (NQ 1.0 pt = 4 ticks + fee) expressed in each market's points; they are REPLACED by the
measured round trip from the first live fills before any readiness verdict (R5).

## Load (minutes per symbol; nothing touches the live tables' NQ rows)
1. `build/dbn_tool bars --symbol GC /home/theone/Desktop/bot/data/raw/gc-ohlcv-1m/*.dbn.zst` (ON CONFLICT DO NOTHING; `buy_volume 0`,
   `ticks 0` = aggressor split unknown, exactly as ES). Repeat per symbol. Outside RTH (it writes `bars_1m`).
2. `scripts/edge_lab.sh export GC CL ZN RTY YM` → `data/edge_lab/cache/<SYM>.csv` (+ `.bin` on first load). `session_stats` /
   ATR / levels are computed by edge_lab from the bars themselves — no other table is needed. VIX gates stay VIX (an
   equity-fear gauge); on non-equity markets the weak-day spec keeps its `v = 0` variant so the walk-forward may choose
   the ungated form.
3. Check: `build-exp/edge_lab run research/ideas/so_cascade.json --data … --quick` with `"symbol": "GC"` must report the
   market's bars / days (≥ 3,500 days) — a short series means a roll or load problem, fix before any verdict.

## Specs (NO tuning — the rule is the test)
Each existing PASS spec is copied with ONLY `symbol`, `confirm` and `cost` changed; every threshold stays in ATR units
and clock times exactly as on NQ (the ES transfer was done the same way and passed). Per market: `so_cascade`,
`cascade_gap_agree`, `gap_or_retest`, `weak_day_long` (with its `v = 0` variant), `monday_long`, `nf_xm_rs_ctl`
(10:00 momentum). Confirm market: ES for RTY / YM; GC ↔ CL for each other (both are "another commodity", the check is
weak and counts as one of seven); ZN / 6E: `confirm` empty (the robustness gate then has six checks, needs five).
Files: `research/ideas/mk_<sym>_<idea>.json`, e.g. `mk_gc_so_cascade`. 6 ideas × 5 markets = 30 runs, minutes each.

## Pass rules (written now)
- In-sample 2010-07 → 2024-12, edge_lab's five gates at the market's cost; sealed holdout 2025-01 → today, one look per
  PASS (≥ 30 trades, net > 0).
- Multiple testing: 30 specs → expect 1–2 screen passes by luck; a market-family pair counts only with the holdout.
- **Diversification test, the point of the exercise:** a PASS pair's monthly P&L correlation with the NQ four-family book
  (2019 →) must be **≤ 0.3** to count as a new family; 0.3–0.5 = a return add, not a diversifier; > 0.5 = redundant.
- A pair that passes goes to the paper fleet's existing `markets` lane (`config/paper_fleet_<code>.json`, labels
  `gc cl rty ym`, live since 2026-10-16) with the SAME ATR-unit knobs, and then through the readiness scorecard
  (R1–R12) like everything else; ≥ 40 forward trades before any live decision.
- Expected (stated so the result can disappoint): ~half the pairs pass at smaller per-trade edge than NQ (the ES
  experience); if GC / CL / ZN together add ≥ 2 passing pairs with corr ≤ 0.3, the book goes from ~1.2 to ~2.5
  signals/day; the 3–5 target needs the clock versions (Asia / London) on top, pre-registered separately.

## Not part of this
Per-market parameter searches (forbidden — a pair either transfers or it does not), Binance crypto (fees decided it),
YM as a diversifier (it is NQ with a different multiplier).

## Results
(filled after the runs)
