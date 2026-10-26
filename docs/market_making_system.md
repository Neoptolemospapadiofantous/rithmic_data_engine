# Lighter market-making system

State as of 2026-10-25. Research and paper trading only: no component holds an API key or can place an
order. Source: `src/dex/`, tests `tests/dex/`, changelog entries of 2026-10-25 in `CHANGES.md`.

## 1. What it is

A passive market maker for small perpetual futures on decentralized exchanges, built from the
"algorithmic market making" idea: quote a bid and an ask around a fair price and earn part of the spread on
each fill. The strategy is frozen from history and is now paper-traded live on Lighter.

Why Lighter: its Standard account charges 0 maker / 0 taker fees. The edge is about +0.5 bps per dollar
traded, and a maker fee of 0.5 bps erases it (section 7).

## 2. Running services (systemd user units)

| Unit | Binary | Reads | Writes |
|---|---|---|---|
| `mm-paper-local` | `build/mm_paper` | Lighter websocket (own connection), 24 markets | Postgres `mm_paper_*`, `data/mm_paper/status.json`, log `data/logs/mm_paper.log` |
| `mm-paper-fast-local` | `build/mm_paper --label paper_220 --latency-ms 220` | same feed | same tables under label `paper_220`; `data/mm_paper/status_paper_220.json` — the shadow at a nearby server's delay |
| `lighter-recorder-local` | `build/lighter_recorder` | Lighter websocket, same 24 markets | `/media/theone/381C48C51C48803C/rithmic_dex/lighter/<SYM>/<UTC day>.jsonl.zst` |
| `hl-recorder-local` | `build/hl_recorder` | Hyperliquid websocket, 30 perps | `rithmic_dex/hyperliquid/<COIN>/<UTC day>.jsonl.zst` |

Markets (Lighter): AAVE BNB ONDO AVAX DOT XLM ASTER TRUMP FIL OP CC POL WIF EIGEN BERA AZTEC APEX UNI CRV PYTH
ETHFI SEI 2Z GMX — the Lighter perps that were also tested on Hyperliquid history.

All archives live on the second drive. The recorders refuse to start when the output directory is missing,
so an unmounted drive can never fill the root disk.

Recorder health (since 2026-10-26). Every recorder (lighter, hl, dex ×3) rewrites `data/recorder_status/<name>.json`
every ~4 s (`src/dex/recorder_status.hpp`): per market the last message time, last book/trade time, counts, gaps and
the longest silence; overall reconnects, sequence gaps, zstd write errors, lines it could not archive, frames
written, and the busiest market with its age. Names: `lighter`, `hyperliquid`, `extended`, `aster`, `paradex`
(`--name` overrides). `scripts/recorder_watch.sh` reads them; `deploy/recorder-watch.{service,timer}` runs it every
minute (NOT installed). It alerts once through `grid-notify` when a unit is enabled but not active, a status file is
missing or older than 60 s, the busiest market has had no book/trade message for more than 60 s, or lines were not
archived; a "recovered" line follows when it clears; at most one message per 10 minutes. Fail-open.

| Unit (not installed) | What |
|---|---|
| `lighter-recorder-backup` | second Lighter recorder, `--name lighter_backup`, own archive root (see "Backup recorder" below) |
| `recorder-watch.timer` | the every-minute health check above |

### Backup recorder (design, 2026-10-26 — not installed)
Why: live P&L and fill measurement need the tape for every minute we quote. One recorder has single points of failure:
the process (OOM, crash, a deploy restart — measured gap ≈ the restart time plus the first snapshot, a few seconds,
but `RestartSec=60` makes an unplanned crash cost ~60 s), its websocket (a sequence gap forces a reconnect: the book
is lost until the new snapshot), the machine and its network.
- Same binary, second instance: `lighter_recorder --name lighter_backup --out <other root> --symbols <same list>`
  (`deploy/lighter-recorder-backup.service`). `--name` gives it its own status file. `<out>/.lighter_recorder.lock`
  (flock) makes a second recorder on the SAME root refuse to start — two writers on one day file would interleave
  frames — so the backup must have its own root.
- Level 1, same box: root on the main disk (`/home/theone/rithmic_dex_backup/lighter`, ~0.5 GB/day for 24 markets).
  Covers process crashes and restarts (restart the two units at different times, never together). Does not cover
  the box, its power or its network.
- Level 2, two machines (the real backup): the same binary and unit on a second host with its own local root, ideally
  on another network; copy its day files back nightly (rsync, read-only on the primary) into a separate directory,
  e.g. `rithmic_dex/lighter_backup/`. Clocks: `r` is each box's own receive time; exchange timestamps (`timestamp`,
  trade `timestamp`, the book nonce) are the same on both, and replay orders by exchange time.
- Using it: the two copies are each self-consistent but are NOT merged line by line (nonces continue across both,
  receive times differ). Pick per coin-day: `archive_check` both files (skipped_frames, seq_gaps, gaps_over,
  max_gap_s) and replay the better one (`mm_sim --dir <that root>`). An automatic per-day chooser / gap-filler is not
  built yet.
- Unverified: whether Lighter limits public websocket connections or subscriptions per IP; two recorders from one IP
  double our subscriptions (24 markets × 3 channels each).

## 3. Data sources

### Lighter (live) — `wss://mainnet.zklighter.elliot.ai/stream`
- Subscribe `{"type":"subscribe","channel":"order_book/<market_id>"}` and `"trade/<market_id>"`.
  Market ids come from `GET https://mainnet.zklighter.elliot.ai/api/v1/orderBooks`.
- `subscribed/order_book`: the full book. `update/order_book`: only changed levels; size `0` removes a level.
  `order_book.begin_nonce` must equal the previous `nonce`; otherwise there is a gap. The book is untrusted
  and the connection is reopened, which brings fresh snapshots.
- `subscribed/trade`: a replay of recent trades. It is ignored, because those trades happened before we listened.
  `update/trade`: new `trades` and `liquidation_trades`. `is_maker_ask: true` means the taker bought.
  Types `deleverage` and `market-settlement` are skipped.
- `market_stats/<market_id>` (recorded since 2026-10-26, verified on the live mainnet stream that day): channel
  `market_stats:<id>`, first `subscribed/market_stats` then `update/market_stats` about once per second, envelope
  `timestamp` (ms), object `market_stats` with `mark_price`, `index_price`, `mid_price`, `best_bid_price`,
  `best_ask_price`, `last_trade_price`, `funding_rate`, `current_funding_rate`, `funding_timestamp` (ms, on the hour —
  funding is hourly), `open_interest`, `open_interest_usd`, `premium`, daily volume/range (decimal strings).
  Archived in the same day file as books and trades; `hl_events.hpp` skips these lines for replay (mm_sim output
  byte-identical with and without them — checked on BNB and GMX 2026-10-25) and returns them through
  `load_archive(file, &bad, &stats)` / `hl::load_market_stats(file)` as `lighter::MarketStats`. `--no-stats` turns
  them off. Cost: ~37 bytes/line compressed ≈ 3 MB per market-day, ~+20 % on the whole Lighter archive.
- Market metadata: `GET /api/v1/orderBookDetails` (public) is saved at recorder start and every day at 00:05 UTC to
  `<archive>/_meta/<UTC date>.json` (raw reply, ~400 KB, all markets; `src/dex/lighter_meta.hpp`). Each snapshot is
  compared with the newest one already there; any change in a RECORDED market's `price_decimals`, `size_decimals`,
  `supported_*_decimals`, `min_base_amount`, `min_quote_amount`, `status`, `maker_fee` or `taker_fee`, or a market
  that appears/disappears, is logged as `meta change: BNB(25) price_decimals 4 → 3`. P&L and order sizing measured on
  a day's archive must use that day's terms. `--no-meta` turns it off. `hl::list_days` ignores `_meta/`.
- Keepalive: `{"type":"ping"}` every 60 s (the server closes after 2 minutes of client silence).
- Lighter only sends a book when it changes. A quiet market can be silent for tens of seconds and still be valid.

### Hyperliquid (live) — `wss://api.hyperliquid.xyz/ws`
- `l2Book` with `"fast": true` (book every ~0.55 s; without it about every 5 s) and `trades`, 20 levels per side.
- The first trades message after subscribing replays history; the recorder marks it `"snap": true`.

### Tardis.dev (history) — `scripts/hl_history_download.sh`
- Free sample days only: the 1st of each month, 2024-11 → 2026-10 (24 days); any other day returns HTTP 401.
- `book_snapshot_25` (Hyperliquid keeps 5 levels per side there) + `trades`, gzip CSV.
- `rithmic_dex/tardis` = the 30 tuned coins (636 coin-days); `rithmic_dex/tardis_extra` = 15 coins never used
  in tuning, holdout year only (180 coin-days).

## 4. Archive format

One JSON object per line, zstd-compressed (`src/zstd_daily.hpp`, level 3), a new file per UTC day:
`{"r": <receive time µs>, "m": <raw venue message>}`. Hyperliquid lines also carry `"snap"`. The first line of
each Lighter day file is a full synthetic snapshot (`"synthetic": true`) rebuilt from the live book, so every
file replays alone. A restart appends a new zstd frame; concatenated frames read as one stream. A file still
being written ends in an unfinished frame; `ZstdLines` treats that as the end of data.

Crash tolerance (since 2026-10-26): the recorders end the zstd frame every `--frame-seconds` (default 60; 0 = the old
one frame per run) by the line's receive time and by the 2 s flush clock, with the frame checksum and an
`fdatasync` at each frame end. A kill (even SIGKILL) loses at most the unflushed tail, ≤ 2 s (checked live: SIGKILL
after 25 s, archive read back clean to the last second); a damaged byte costs at most its own ≤ 60 s frame instead of
the rest of the run, and the checksum detects it. Size at level 3, re-writing a real day with a 2 s flush: Lighter
BNB 2026-10-25 (busy) one frame 19.3 MB → 60 s frames 18.3 MB (−5 %; 10 s 17.6 MB); GMX (quiet) 577 KB → 618 KB
(+7 %; 10 s +59 %, 300 s −2 %). 60 s is the chosen trade-off. Lines are whole within a frame; the reader resumes at
the next frame header after any damage. A writer that cannot open its file or hits a short write counts
`dropped_lines` / `write_errors` (status file) and starts a fresh frame.

`src/dex/hl_events.hpp` loads any of the three layouts (Hyperliquid archive, Lighter archive, Tardis CSV) into
one event list ordered by exchange time. At equal times trades come **before** the book, because a block's book
already shows the size its trades removed.

## 5. The strategy (`src/dex/hl_mm.hpp`, `hl::Simulator`)

On every book update, for each side:

1. **Fair price** = mid (microprice was tested and lost).
2. **Centre** = fair − skew × inventory (skew = 0 in the frozen setting).
3. **Target** bid = floor to the venue grid of (centre − width × tick); ask = ceil of (centre + width × tick).
   **The strategy's tick is a Hyperliquid tick**, `max(10^(floor(log10 price) − 4), 10^−(6 − szDecimals))`: the unit
   every backtest measured width, spread filter, pause and requote in (it reproduces HL's tick on 12/12 live coins).
   Rounding and the post-only clip use the **venue grid**: Lighter's `supported_price_decimals` per market. The two
   differ on 8 of the 24 markets (BNB's grid is 100× finer than HL's, AAVE's and DOT's 10×; 2Z, APEX, PYTH, SEI are
   3–10× coarser), which is why the engine cannot simply count Lighter levels. Replaying Lighter's books with the
   venue grid as the unit instead would have quoted AAVE 10× closer to the mid than validated (found 2026-10-25).
4. **Post-only clip:** the bid is never at or above the best ask, and the ask never at or below the best bid.
5. **Allowed?** Not halted, AND spread ≥ `min_spread_ticks`, AND not inside a jump pause, AND the side is not
   "toxic" (top-of-book imbalance (bid−ask)/(bid+ask) ≥ 0.6 pulls the ask, ≤ −0.6 pulls the bid), AND
   inventory + one order stays within ±`max_inv_units` × order size in $.
6. **Requote** only when the live order is more than `requote_ticks` from the target; otherwise keep it and its
   queue place. A cancel takes effect `latency_ms` later, and the order can still fill until then.
7. A new order reaches the book `latency_ms` after the decision. On arrival, an order that would cross is
   rejected (post-only).

### Fill model (queue position)
- A new order joins the **back** of its price level: queue ahead = the visible size there. If the price is
  deeper than the visible book, the queue is assumed equal to the deepest visible level's size.
- A trade at our price uses up the queue ahead first; only the remainder fills us. A trade **through** our
  price fills us completely.
- Size that leaves the level without a trade (cancels) cannot be attributed on an aggregated book.
  **Pessimistic**: cancels were behind us (queue only shrinks through trades). **Optimistic**: cancels came
  pro rata from the whole level. All selection used pessimistic.
- Markouts: the mid 1, 5, 10, 30 and 60 s after each fill. Flatten (taker) exits are excluded.

### Frozen setting (`mmpaper::frozen_params`)
| Parameter | Value |
|---|---|
| order size | $100 |
| inventory cap | 3 orders each way ($300) |
| half width | 5 ticks |
| spread filter | quote only when spread ≥ 4 ticks |
| imbalance pull | 0.6 |
| jump pause | 2 s after a mid move ≥ 2 ticks between two books |
| keep queue | requote only for target changes > 1 tick |
| fair price / skew | mid / 0 |
| fees (Lighter Standard) | 0 maker, 0 taker |
| delay | 300 ms (200 ms imposed by the venue + network; measured exchange→box lag 142–145 ms; the backtests used 300) |
| tick unit | Hyperliquid tick (see above); orders rounded to Lighter's grid |
| queue model | pessimistic |

## 6. Paper engine (`src/dex/mm_paper.hpp`, `src/dex/mm_paper_main.cpp`)

`mmpaper::Portfolio` holds one simulator per market and applies the risk controls:

| Control | Trigger | Action | Release |
|---|---|---|---|
| Day roll | 00:00 UTC | flatten every market (taker), write `mm_paper_daily`, fresh simulators | immediate |
| Feed down | disconnected, or no message for 15 s | pull every quote | feed back |
| Stale market | no book change for 120 s | pull that market's quotes | book flows again |
| Daily loss stop | day equity ≤ −$150 | flatten and halt everything | next UTC day |
| Kill switch | file `data/mm_paper/KILL` exists | flatten and halt everything | file deleted |

A restart starts flat (paper positions are not carried). Equity resets each UTC day.

Postgres (`migrations/20261025_mm_paper.sql`; also created on connect):
- `mm_paper_fills` — every simulated fill (`taker` = a flatten), with `run_id`
- `mm_paper_equity` — once a minute per market and `_total`
- `mm_paper_daily` — one row per UTC day × market
- `mm_paper_events` — start, feed_down/up, stale/fresh, loss_stop, kill/resume, day_roll

Writes go through a bounded queue on their own thread, so the feed never waits for the database. The status
file (every 10 s) holds the day, equity, gross inventory, halted flag and per-market state.

## 7. Evidence (all at the frozen setting unless stated)

History: Tardis sample days, $100 quotes, flat-to-flat days, pessimistic queue. "Holdout" = the setting
chosen on 2024-11 → 2025-10 and judged on 2025-11 → 2026-10.

| Test | Result |
|---|---|
| Rounds 1–2: width × skew × mid/microprice | nothing profitable at any fee |
| Round 3 holdout, Lighter fees, 30 coins | +$446, +0.52 bps, 8 of 12 days positive |
| Lighter Standard (200 ms) vs Premium (0.4 bps, ~0 ms) | +$637 vs +$516–528 |
| 15 coins never used in tuning | +$341, +0.60 bps, 10 of 12 days positive |
| Neighbouring settings | all positive (+$280 to +$750): a plateau |
| Jump pause off | −$321 |
| Delay 100 ms / 500 ms / 1 s | +$750 / +$194 / −$78 |
| Maker fee 0.25 / 0.5 / 1.0 bps | +$233 / ≈ $0 / −$408 |
| Without the best 3 coins | +$131 (18 of 30 coins positive) |
| Mean holdout day, 95% interval (day bootstrap) | +$37 (+$9 … +$68), only 12 days |
| Hyperliquid fees (1.5 bps maker), any method | negative (best −$132) |
| Validation: 20 → 5 visible levels | ≤ 0.01 bps change |

## 8. Research tools

| Tool | Purpose |
|---|---|
| `build/mm_sim --dir DIR [--sweep] [--params FILE] [--max-levels N] [--dash DIR]` | replay an archive; fixed configs or a frozen per-coin file |
| `build/mm_opt --dir DIR [--train 6] [--best-dir DIR]` | walk-forward grid (121 settings × 2 venues): per-coin, one-setting, holdout, join, hindsight |
| `build/mm_robust --dir DIR [--split YYYY-MM-DD]` | the candidate + one-at-a-time variants; per-coin, bootstrap, worst coin-days |
| `scripts/hl_history_download.sh OUT FROM TO COINS` | Tardis free sample days |

Dashboard: https://claude.ai/artifact/WtGeoKFdUaPKcxkv9cCtzs

## 8b. Validation of 2026-10-25 (on the recorded ticks)

| Check | Result |
|---|---|
| Lighter archive, 24 markets × 1.46 h | 0 sequence gaps, 0 crossed books, 0 out-of-order timestamps, one snapshot per market |
| Live engine vs replay of the same archive | 120 vs 122 fills, −$7.10 vs −$7.49, per-market fills within ±3 |
| Determinism | 3 replays of a frozen copy: identical |
| Database | minute equity rows continuous except the restart window; `_total` matches `status.json` |
| Throughput | one Tardis coin-day (146k books + 76k trades) × 4 configs in 0.47 s; live engine 1% CPU, 22 MB |
| Clock | NTP-synced; exchange→box lag 142–145 ms (p10–p99) |
| Tick unit | mismatch found on 8/24 Lighter markets → Hyperliquid tick rule added (above) |
| Tests | 28 + 10 + 18 pass |

## 9. Runbook

```
systemctl --user status mm-paper-local lighter-recorder-local hl-recorder-local
cat data/mm_paper/status.json
touch data/mm_paper/KILL          # flatten + halt every market (paper)
rm data/mm_paper/KILL             # resume
tail -f data/logs/mm_paper.log
build/mm_robust --dir /media/theone/381C48C51C48803C/rithmic_dex/lighter   # re-test on Lighter's own books
build/test_hl_mm && build/test_lighter && build/test_recorder
jq '{busiest,busiest_age_s,reconnects,gaps,dropped_lines}' data/recorder_status/lighter.json   # recorder health
scripts/recorder_watch.sh --dry-run                                     # what the watch would alert on
grep 'meta change' data/logs/lighter_recorder.log                       # a market's decimals/minimums changed
```

## 10. Not modelled, and open risks

- **Lighter's own books are only now being recorded.** All evidence so far used Hyperliquid's books. First 1.5 h
  on Lighter (Sunday): fills come, but the 60 s markout is −1.8 to −2.3 bps against ~0 on Hyperliquid's live books.
  Too short to judge; the point of Phase 1.
- Exchange→box lag is 142–145 ms (NTP-synced clock). From a server next to the exchange it would be near zero;
  the 200 ms Standard-account delay stays.
- Our orders do not change other traders' behaviour in the simulation (no market impact).
- Funding payments are not modelled.
- History is one day per month; coins were chosen by today's volume rank (survivorship).
- A flatten assumes the best level absorbs the position ($100–$300 positions).

## 11. Roadmap

1. **Phase 1 (live):** recorder + paper engine.
2. **Phase 2:** daily report after 00:00 UTC (Telegram + dashboard) and a weekly re-check.
3. **Phase 3:** BTC-lead pause (Binance via crypto_collector fan-out :8197), BTC/ETH hedge, 60–100 coins.
4. **Phase 4, founder's decision:** Lighter account + key, order signing (Lighter ships only Python/Go SDKs:
   a Go sidecar or a C++ port), a server near Lighter, a $250 pilot ($10 quotes, 15 coins).
