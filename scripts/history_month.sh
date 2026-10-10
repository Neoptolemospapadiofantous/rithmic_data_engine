#!/usr/bin/env bash
# history_month.sh — replay ONE market × month of history into the paper tables (worker of
# scripts/history_queue.sh and scripts/nq_history_replay.sh).
#
#   usage: scripts/history_month.sh MARKET YYYY-MM [--redo] [--with-mbo-1m]
#     nq    config/paper_fleet.json → label nq_hist
#             source mbo : Databento MBO file of that month (paper_engine --dbn-mbo), 2023-03 … 2026-03
#             source pg  : the collector's own ticks/bbo in Postgres (existing replay path), months from
#                          2026-09 — window = max(month start, 2026-09-19) … min(month end, the last
#                          completed 17:00 ET session close)
#     nqx   the NQ sets outside the main fleet (night_fleet + staging winners/newmodes, merged into
#           data/history/configs/nq_extra.json) → label nq_hist, same sources as nq; nq and nqx each delete only
#           their own ids before a replay
#     btc   config/paper_fleet_crypto_btc.json → label crypto_hist (ids btc_*), Binance aggTrades files; the config
#           actually replayed is `crypto_fleet.sh month-config` (point knobs × month mean close / knob_ref_price,
#           relative costs commission_pct / slippage_bps) in data/history/configs/
#     eth   config/paper_fleet_crypto_eth.json → label crypto_hist (ids eth_*)
#
#   exit 0 done (marker written) · 4 no data for that month (marker 'nodata') · 1 failure
#
# Idempotent: deletes the label's paper_trades/paper_signals rows of that month (and, for crypto, of
# that coin's ids only) before replaying. Done markers: data/history/done/<market>/<YYYY-MM>.done
# (JSON). The engine's PID is written to data/history/pids/<market>_<YYYY-MM>.pid so the queue can
# pause it (SIGSTOP) through the guarded windows. Engine output is filtered to its summary lines
# (the full per-strategy log is ~350 MB a month): data/logs/history/<market>_<YYYY-MM>.log
# Binaries: build-exp/paper_engine, build-exp/dbn_tool (nice 15, ionice idle).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
MARKET="${1:-}"; M="${2:-}"; shift 2 2>/dev/null || true
REDO=0; MBO1M=0
while [[ $# -gt 0 ]]; do case "$1" in --redo) REDO=1;; --with-mbo-1m) MBO1M=1;; *) echo "unknown arg $1"; exit 2;; esac; shift; done
[[ "$M" =~ ^[0-9]{4}-[0-9]{2}$ ]] || { echo "usage: $0 {nq|nqx|nqbars|btc|eth} YYYY-MM [--redo] [--with-mbo-1m]"; exit 2; }
NQ_RAW="${NQ_HISTORY_RAW:-/home/theone/Desktop/bot/data/raw/mbo}"
BIN_DIR="data/history/binance"
PG_FIRST="2026-09-19"          # first day of the collector's own NQ ticks
ENGINE="$REPO/build-exp/paper_engine"; TOOL="$REPO/build-exp/dbn_tool"
DONE="data/history/done/$MARKET"; PIDS="data/history/pids"; LOGD="data/logs/history"
mkdir -p "$DONE" "$PIDS" "$LOGD"
LOG="$LOGD/${MARKET}_$M.log"; PIDF="$PIDS/${MARKET}_$M.pid"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
NEXT=$(date -u -d "$M-01 +1 month" +%Y-%m)

legacy="data/history_replay/nq_hist/$M.done"     # written by the first nq_history_replay.sh run
if [[ $REDO -eq 0 && ( -f "$DONE/$M.done" || ( "$MARKET" == nq && -f "$legacy" ) ) ]]; then echo "done already"; exit 0; fi

FROM_UTC="$M-01 00:00"; TO_UTC="$NEXT-01 00:00"; files=(); feedflag=(); src=""
IDLIST=""   # nq/nqx share label nq_hist: each deletes only its own ids, so a redo of one never wipes the other
case "$MARKET" in
  nq|nqx)
    CONFIG="config/paper_fleet.json"; LABEL="nq_hist"; IDS=""
    mkdir -p data/history/configs
    if [[ "$MARKET" == nqx ]]; then
      # 2026-10-11 founder: "trade on all the data we have" — the NQ strategy sets outside the main fleet:
      # overnight grid (nt_*), staged winners + new modes. Same header/costs as the main fleet.
      CONFIG="data/history/configs/nq_extra.json"
      jq -s '.[0] + {account_label: "staging", strategies: ([.[1:][].strategies[] | select(.enabled != false)])}' config/paper_fleet.json \
        config/night_fleet.json config/staging/paper_winners.json config/staging/paper_newmodes.json > "$CONFIG.tmp" \
        && mv "$CONFIG.tmp" "$CONFIG" || { echo "nqx config build failed"; exit 1; }
    fi
    IDLIST="$REPO/data/history/configs/${MARKET}_$M.ids"
    jq -r '.strategies[].id' "$CONFIG" > "$IDLIST" || { echo "id list failed"; exit 1; }
    if [[ -f "$NQ_RAW/$M.dbn.zst" ]]; then files=("$NQ_RAW/$M.dbn.zst")
    else mapfile -t files < <(ls "$NQ_RAW/$M"-partial*.dbn.zst 2>/dev/null | sort); fi
    if [[ ${#files[@]} -gt 0 ]]; then src="mbo"; feedflag=(--dbn-mbo "${files[@]}")
    elif [[ "$M-01" > "2026-08-31" ]]; then
      src="pg"
      [[ "$FROM_UTC" < "$PG_FIRST 00:00" ]] && FROM_UTC="$PG_FIRST 00:00"
      close=$(TZ=America/New_York date -d "today 17:00" +%s); (( $(date +%s) < close )) && close=$(TZ=America/New_York date -d "yesterday 17:00" +%s)
      close_utc=$(date -u -d "@$close" '+%F %H:%M')
      [[ "$close_utc" < "$TO_UTC" ]] && TO_UTC="$close_utc"
      [[ "$FROM_UTC" < "$TO_UTC" ]] || { echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true}" > "$DONE/$M.nodata"; exit 4; }
    else
      echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true,\"note\":\"no MBO file and before the collector (gap 2026-03-20..2026-09-18)\"}" > "$DONE/$M.nodata"; exit 4
    fi ;;
  nqbars)
    # NQ years with only 1-minute bars (2010-07 … 2023-02): 4 synthetic prints per bar (src/history/bar_feed.hpp),
    # an approximation — its own label so it never mixes with tick history (founder 2026-10-11: all the data we have)
    CONFIG="config/paper_fleet.json"; LABEL="nq_bars"; IDS=""; ENGINE="$REPO/build-hist/paper_engine"
    f="${NQ_BARS_RAW:-/home/theone/Desktop/bot/data/raw/ohlcv-1m}/$M.dbn.zst"
    [[ -f "$f" && "$M" < "2023-03" ]] || { echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true}" > "$DONE/$M.nodata"; exit 4; }
    files=("$f"); src="bars"; feedflag=(--dbn-bars "$f") ;;
  btc|eth)
    SYM=$([[ $MARKET == btc ]] && echo BTCUSDT || echo ETHUSDT)
    CONFIG="config/paper_fleet_crypto_$MARKET.json"; LABEL="crypto_hist"; IDS="${MARKET}\\_%"
    if [[ -f "$BIN_DIR/$SYM/$SYM-aggTrades-$M.zip.ok" ]]; then files=("$BIN_DIR/$SYM/$SYM-aggTrades-$M.zip")
    else for f in "$BIN_DIR/$SYM/$SYM-aggTrades-$M"-[0-9][0-9].zip; do [[ -f "$f.ok" ]] && files+=("$f"); done; fi
    [[ ${#files[@]} -gt 0 ]] || { echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true}" > "$DONE/$M.nodata"; exit 4; }
    src="binance"; feedflag=(--binance-agg "${files[@]}")
    # point knobs at the month's own price level (constant in % of price); costs are relative in the file
    CONFIG=$(scripts/crypto_fleet.sh month-config "$MARKET" "$M" | awk '{print $1}')
    [[ -f "$CONFIG" ]] || { echo "month-config failed for $MARKET $M (bars_1m for $SYM missing? dbn_tool agg-bars first)"; exit 1; } ;;
  *) echo "unknown market $MARKET"; exit 2 ;;
esac

idf=""; [[ -n "$IDS" ]] && idf="AND strategy_id LIKE '$IDS'"
[[ -n "$IDLIST" ]] && idf="AND strategy_id IN (SELECT id FROM ids)"
"${PSQL[@]}" <<SQL || { echo "cleanup failed"; exit 1; }
CREATE TEMP TABLE ids (id text PRIMARY KEY);
$( [[ -n "$IDLIST" ]] && echo "\\copy ids FROM '$IDLIST'" )
DELETE FROM paper_trades  WHERE account_label='$LABEL' $idf AND entry_time >= '$M-01 00:00+00' AND entry_time < '$NEXT-01 00:00+00';
DELETE FROM paper_signals WHERE account_label='$LABEL' $idf AND ts >= '$M-01 00:00+00' AND ts < '$NEXT-01 00:00+00';
SQL

if [[ $MBO1M -eq 1 && "$src" == mbo ]]; then
  nice -n 15 ionice -c3 "$TOOL" mbo-1m "${files[@]}" --symbol NQ > "$LOGD/${MARKET}_$M.mbo1m.log" 2>&1 || echo "mbo-1m failed (replay continues)"
fi

from_et=$(TZ=America/New_York date -d "$FROM_UTC UTC" '+%F %H:%M')
to_et=$(TZ=America/New_York date -d "$TO_UTC UTC - 1 minute" '+%F %H:%M')
t0=$(date +%s)
echo "$(date -Is) $MARKET $M source=$src window(ET) $from_et .. $to_et files=${#files[@]}" > "$LOG"
nice -n 15 ionice -c3 "$ENGINE" --config "$CONFIG" --account-label "$LABEL" --replay-from "$from_et" --replay-to "$to_et" \
    "${feedflag[@]}" > >(grep -a --line-buffered -E '\[PAPER\] (REPLAY|Day rollover|MBO|file feed|aggTrades|bar feed|Regime gate: date=.{10} prior|WARN feed gap|Fleet ready|FATAL|paper_engine|Shutdown)|\[MBO\]|\[AGG\]|FATAL|Segmentation|terminate' >> "$LOG") 2>&1 &
epid=$!
echo "$epid" > "$PIDF"
wait "$epid"; rc=$?
rm -f "$PIDF"; sleep 1
secs=$(( $(date +%s) - t0 ))
if [[ $rc -ne 0 ]] || ! grep -q "REPLAY complete" "$LOG"; then echo "$(date -Is) FAILED rc=$rc after ${secs}s" >> "$LOG"; exit 1; fi
n=$("${PSQL[@]}" <<SQL
CREATE TEMP TABLE ids (id text PRIMARY KEY);
$( [[ -n "$IDLIST" ]] && echo "\\copy ids FROM '$IDLIST'" )
SELECT count(*) FROM paper_trades WHERE account_label='$LABEL' $idf AND entry_time >= '$M-01 00:00+00' AND entry_time < '$NEXT-01 00:00+00';
SQL
)
feed=$(grep -aoE "(MBO|aggTrades|bar) feed: [0-9]+ (records|lines), [0-9]+ trades" "$LOG" | tail -1)
ticks=$(grep -aoE "[0-9]+ trades" <<< "$feed" | grep -oE "[0-9]+" | head -1)
echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"label\":\"$LABEL\",\"source\":\"$src\",\"window_utc\":\"$FROM_UTC..$TO_UTC\",\"trades\":$n,\"ticks\":${ticks:-null},\"secs\":$secs,\"finished\":\"$(date -Is)\"}" > "$DONE/$M.done"
echo "$(date -Is) done: $n paper trades in ${secs}s ($feed)" >> "$LOG"
echo "$MARKET $M: $n paper trades in ${secs}s"
