#!/usr/bin/env bash
# binance_history_download.sh — Binance USDⓈ-M futures aggTrades history from data.binance.vision.
#
#   usage: scripts/binance_history_download.sh [SYMBOLS] [FROM] [TO]
#          SYMBOLS comma list (default BTCUSDT,ETHUSDT); FROM/TO = YYYY-MM (default 2020-01 .. current month)
#
# Complete months come as one monthly zip (futures/um/monthly/aggTrades/<SYM>/<SYM>-aggTrades-YYYY-MM.zip);
# the current month (no monthly file yet) as daily zips up to yesterday (UTC). Every zip is checked
# against Binance's .CHECKSUM (sha256) and marked <zip>.ok; a rerun skips verified files, re-fetches
# a bad/partial one (resumable). Files: data/history/binance/<SYM>/ (gitignored, read by
# paper_engine --binance-agg and dbn_tool agg-bars).
#
# Disk guard: stops (exit 3) before any download that would leave < MIN_FREE_GB (default 40) free.
# Log: data/logs/binance_history_download.log
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
SYMS="${1:-BTCUSDT,ETHUSDT}"; FROM="${2:-2020-01}"; TO="${3:-$(date -u +%Y-%m)}"
MIN_FREE_GB="${MIN_FREE_GB:-40}"
BASE="https://data.binance.vision/data/futures/um"
DEST="data/history/binance"; LOG="data/logs/binance_history_download.log"
mkdir -p "$DEST" data/logs
say() { echo "$(date -Is) $*" | tee -a "$LOG"; }
free_bytes() { df -B1 --output=avail "$DEST" | tail -1 | tr -d ' '; }

fetch() {   # fetch URL into DIR, verify sha256; 0 ok, 1 missing upstream, 2 failed, 3 disk guard
  local url="$1" dir="$2" f; f="$dir/$(basename "$url")"
  [[ -f "$f.ok" && -f "$f" ]] && return 0
  local sum; sum=$(curl -sf --retry 3 "$url.CHECKSUM" | awk '{print $1}') || sum=""
  [[ -z "$sum" ]] && return 1
  local len; len=$(curl -sfI --retry 3 "$url" | awk 'tolower($1)=="content-length:"{print $2}' | tr -d '\r')
  local need=$(( ${len:-0} + MIN_FREE_GB * 1024 * 1024 * 1024 ))
  if (( $(free_bytes) < need )); then
    say "DISK GUARD: $(basename "$url") (${len:-?} bytes) would leave < ${MIN_FREE_GB} GB free — stopping"; return 3
  fi
  local try
  for try in 1 2 3; do
    curl -sf --retry 3 -C - -o "$f.part" "$url" || { rm -f "$f.part"; curl -sf --retry 3 -o "$f.part" "$url"; }
    if [[ -f "$f.part" && "$(sha256sum "$f.part" | cut -d' ' -f1)" == "$sum" ]]; then
      mv "$f.part" "$f"; touch "$f.ok"; return 0
    fi
    say "checksum mismatch/failed download $(basename "$url") (try $try)"; rm -f "$f.part"
  done
  return 2
}

IFS=',' read -ra SL <<< "$SYMS"
cur=$(date -u +%Y-%m); yday=$(date -u -d yesterday +%F)
total=0; fails=0
for s in "${SL[@]}"; do
  [[ "$s" =~ ^[A-Z0-9]+$ ]] || { say "bad symbol $s"; exit 2; }
  dir="$DEST/$s"; mkdir -p "$dir"
  m="$FROM"
  while [[ "$m" < "$TO" || "$m" == "$TO" ]]; do
    if [[ "$m" != "$cur" ]]; then
      fetch "$BASE/monthly/aggTrades/$s/$s-aggTrades-$m.zip" "$dir"; rc=$?
      if [[ $rc -eq 3 ]]; then exit 3; fi
      if [[ $rc -eq 1 ]]; then   # monthly file not published yet (first days of a month): daily files instead
        d="$m-01"
        while [[ "$d" < "$(date -u -d "$m-01 +1 month" +%F)" && ( "$d" < "$yday" || "$d" == "$yday" ) ]]; do
          fetch "$BASE/daily/aggTrades/$s/$s-aggTrades-$d.zip" "$dir"; r2=$?
          [[ $r2 -eq 3 ]] && exit 3; [[ $r2 -eq 2 ]] && ((fails++)); [[ $r2 -eq 0 ]] && ((total++))
          d=$(date -u -d "$d +1 day" +%F)
        done
      elif [[ $rc -eq 0 ]]; then ((total++)); say "$s $m ok ($(du -h "$dir/$s-aggTrades-$m.zip" | cut -f1))"
      else ((fails++)); say "$s $m FAILED"; fi
    else
      d="$m-01"
      while [[ "$d" < "$yday" || "$d" == "$yday" ]]; do
        fetch "$BASE/daily/aggTrades/$s/$s-aggTrades-$d.zip" "$dir"; r2=$?
        [[ $r2 -eq 3 ]] && exit 3; [[ $r2 -eq 2 ]] && ((fails++)); [[ $r2 -eq 0 ]] && ((total++))
        d=$(date -u -d "$d +1 day" +%F)
      done
      say "$s $m daily files through $yday"
    fi
    m=$(date -u -d "$m-01 +1 month" +%Y-%m)
  done
done
say "done: $total file(s) present/verified, $fails failure(s), $(du -sh "$DEST" | cut -f1) in $DEST, $(( $(free_bytes) / 1073741824 )) GB free"
[[ $fails -eq 0 ]]
