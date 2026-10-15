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
#     nqx   the NQ sets outside the main fleet (night_fleet + staging winners/newmodes + the main fleet's lv_* level
#           variants, merged into
#           data/history/configs/nq_extra_<M>.json) → label nq_hist, same sources as nq; nq and nqx each delete only
#           their own ids before a replay
#     nqcombo  config/staging/paper_fleet_combo.json (12,031 combos) on the NQ ticks → label nq_combo_hist
#     nqbars   every NQ set on 1m bars 2010-07 … 2023-02 (4 synthetic prints/bar) → label nq_bars
#     nqbcheck every NQ set on 1m bars 2023-03 … 2026-03 — bars vs ticks per strategy → label nq_barcheck
#     es       staged ES mirror fleet on the collector's ES ticks (2026-09-19 …) → label es_hist
#     esbars   staged ES mirror fleet on ES 1m bars 2010-07 … 2026-03 → label es_bars
#     btclv / ethlv  only the lv_* level variants of the crypto fleet, for months replayed before they existed
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
[[ "$M" =~ ^[0-9]{4}-[0-9]{2}$ ]] || { echo "usage: $0 {nq|nqx|nqcombo|nqbars|nqbcheck|es|esbars|btc|eth|btclv|ethlv} YYYY-MM [--redo] [--with-mbo-1m]"; exit 2; }
NQ_RAW="${NQ_HISTORY_RAW:-/home/theone/Desktop/bot/data/raw/mbo}"
BIN_DIR="data/history/binance"
PG_FIRST="2026-09-19"          # first day of the collector's own NQ ticks
ENGINE="$REPO/build-hist/paper_engine"; TOOL="$REPO/build-hist/dbn_tool"   # build-hist: has the bar feed + price levels (2026-10-11)
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
mkdir -p data/history/configs
# A month of NQ ticks: the Databento MBO file, else (from 2026-09) the collector's ticks in Postgres.
nq_tick_source() {
  if [[ -f "$NQ_RAW/$M.dbn.zst" ]]; then files=("$NQ_RAW/$M.dbn.zst")
  else mapfile -t files < <(ls "$NQ_RAW/$M"-partial*.dbn.zst 2>/dev/null | sort); fi
  if [[ ${#files[@]} -gt 0 ]]; then src="mbo"; feedflag=(--dbn-mbo "${files[@]}"); return 0; fi
  [[ "$M-01" > "2026-08-31" ]] && { pg_source; return $?; }
  echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true,\"note\":\"no MBO file and before the collector (gap 2026-03-20..2026-09-18)\"}" > "$DONE/$M.nodata"; exit 4
}
# The collector's own ticks (feed symbol from the config): max(month start, PG_FIRST) … last completed 17:00 ET close.
pg_source() {
  src="pg"
  [[ "$FROM_UTC" < "$PG_FIRST 00:00" ]] && FROM_UTC="$PG_FIRST 00:00"
  close=$(TZ=America/New_York date -d "today 17:00" +%s); (( $(date +%s) < close )) && close=$(TZ=America/New_York date -d "yesterday 17:00" +%s)
  close_utc=$(date -u -d "@$close" '+%F %H:%M')
  [[ "$close_utc" < "$TO_UTC" ]] && TO_UTC="$close_utc"
  [[ "$FROM_UTC" < "$TO_UTC" ]] || { echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true}" > "$DONE/$M.nodata"; exit 4; }
}
bars_file() {   # $1 = raw dir of one OHLCV-1m month, $2 = last month allowed
  local f="$1/$M.dbn.zst"
  [[ -f "$f" && ! "$M" > "$2" ]] || { echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true}" > "$DONE/$M.nodata"; exit 4; }
  files=("$f"); src="bars"; feedflag=(--dbn-bars "$f")
}
# Every NQ set in one config: main fleet (incl. lv_* variants) + overnight grid + staged winners / new modes.
nq_all_config() {
  CONFIG="data/history/configs/nq_all_$M.json"
  jq -s '.[0] + {account_label: "staging", strategies: (.[0].strategies + [.[1:][].strategies[] | select(.enabled != false)])}' \
    config/paper_fleet.json config/night_fleet.json config/staging/paper_winners.json config/staging/paper_newmodes.json \
    > "$CONFIG.tmp" && mv "$CONFIG.tmp" "$CONFIG" || { echo "nq_all config build failed"; exit 1; }
}
case "$MARKET" in
  nq|nqx|nqcombo)
    LABEL="nq_hist"; IDS=""
    case "$MARKET" in
      nqx)
        # 2026-10-11 founder: "trade on all the data we have" — the NQ sets outside the main fleet: overnight
        # grid (nt_*), staged winners + new modes, + the main fleet's lv_* level variants (added after most nq
        # months were replayed, so they ride with nqx and the nq job leaves them out).
        CONFIG="data/history/configs/nq_extra_$M.json"         # per month: concurrent jobs never share a file
        jq -s '.[0] + {account_label: "staging", strategies: ([.[1:][].strategies[] | select(.enabled != false)]
                 + [.[0].strategies[] | select((.params.overlay // "") | startswith("lv_"))])}' config/paper_fleet.json \
          config/night_fleet.json config/staging/paper_winners.json config/staging/paper_newmodes.json > "$CONFIG.tmp" \
          && mv "$CONFIG.tmp" "$CONFIG" || { echo "nqx config build failed"; exit 1; } ;;
      nqcombo)
        # the 12,031 cross-category filter pairs (fleet_expand.sh combos), own label — lowest priority
        LABEL="nq_combo_hist"; CONFIG="config/staging/paper_fleet_combo.json" ;;
      *)
        CONFIG="data/history/configs/nq_main_$M.json"        # the main fleet minus the lv_* variants (they run in nqx)
        jq '.strategies |= map(select((.params.overlay // "") | startswith("lv_") | not))' config/paper_fleet.json > "$CONFIG.tmp" \
          && mv "$CONFIG.tmp" "$CONFIG" || { echo "nq config build failed"; exit 1; } ;;
    esac
    IDLIST="$REPO/data/history/configs/${MARKET}_$M.ids"
    jq -r '.strategies[].id' "$CONFIG" > "$IDLIST" || { echo "id list failed"; exit 1; }
    nq_tick_source ;;
  nqbars|nqbcheck)
    # NQ from 1-minute bars, 4 synthetic prints per bar (src/history/bar_feed.hpp) — an approximation, own labels:
    #   nqbars   2010-07 … 2023-02 (the years with no tick data)                 → nq_bars
    #   nqbcheck 2023-03 … 2026-03 (the MBO months again, from bars): per strategy, bars vs ticks → nq_barcheck
    nq_all_config; IDS=""
    if [[ "$MARKET" == nqbars ]]; then LABEL="nq_bars"; [[ "$M" < "2023-03" ]] || { echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true}" > "$DONE/$M.nodata"; exit 4; }
    else LABEL="nq_barcheck"; [[ "$M" < "2023-03" ]] && { echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true}" > "$DONE/$M.nodata"; exit 4; }; fi
    bars_file "${NQ_BARS_RAW:-/home/theone/Desktop/bot/data/raw/ohlcv-1m}" "2026-03" ;;
  es|esbars)
    # ES mirror fleet (staged 1,614 incl. the 24 forward es_* ids; MES $5/pt): the collector's ES ticks from
    # 2026-09-19 (es → es_hist) and ES 1-minute bars 2010-07 … 2026-03 (esbars → es_bars).
    CONFIG="config/staging/paper_fleet_es.json"; IDS=""
    if [[ "$MARKET" == es ]]; then LABEL="es_hist"; [[ "$M-01" > "2026-08-31" ]] || { echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true}" > "$DONE/$M.nodata"; exit 4; }; pg_source
    else LABEL="es_bars"; bars_file "${ES_BARS_RAW:-/home/theone/Desktop/bot/data/raw/es-ohlcv-1m}" "2026-03"; fi ;;
  btc|eth|btclv|ethlv)
    COIN="${MARKET:0:3}"
    SYM=$([[ $COIN == btc ]] && echo BTCUSDT || echo ETHUSDT)
    LABEL="crypto_hist"; IDS="${COIN}\\_%"
    if [[ -f "$BIN_DIR/$SYM/$SYM-aggTrades-$M.zip.ok" ]]; then files=("$BIN_DIR/$SYM/$SYM-aggTrades-$M.zip")
    else for f in "$BIN_DIR/$SYM/$SYM-aggTrades-$M"-[0-9][0-9].zip; do [[ -f "$f.ok" ]] && files+=("$f"); done; fi
    [[ ${#files[@]} -gt 0 ]] || { echo "{\"month\":\"$M\",\"market\":\"$MARKET\",\"nodata\":true}" > "$DONE/$M.nodata"; exit 4; }
    src="binance"; feedflag=(--binance-agg "${files[@]}")
    # point knobs at the month's own price level (constant in % of price); costs are relative in the file
    CONFIG=$(scripts/crypto_fleet.sh month-config "$COIN" "$M" | awk '{print $1}')
    [[ -f "$CONFIG" ]] || { echo "month-config failed for $COIN $M (bars_1m for $SYM missing? dbn_tool agg-bars first)"; exit 1; }
    if [[ "$MARKET" == *lv ]]; then
      # btclv / ethlv: only the lv_* level variants, for months replayed before they existed (2026-10-11)
      jq '.strategies |= map(select(.id | contains("__lv_")))' "$CONFIG" > "${CONFIG%.json}_lv.json" || exit 1
      CONFIG="${CONFIG%.json}_lv.json"; IDS=""
      IDLIST="$REPO/data/history/configs/${MARKET}_$M.ids"; jq -r '.strategies[].id' "$CONFIG" > "$IDLIST" || exit 1
    fi ;;
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
