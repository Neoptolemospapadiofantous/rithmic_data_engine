#!/usr/bin/env bash
# hl_history_download.sh — Tardis.dev free sample days (the 1st of each month) of Hyperliquid order-book
# snapshots (book_snapshot_25) and trades, for market-making research (src/dex/, founder 2026-10-25).
#
#   scripts/hl_history_download.sh OUT_DIR FROM_YYYY-MM TO_YYYY-MM COIN[,COIN...]
#
# Writes OUT_DIR/raw/<COIN>/<YYYY-MM-01>.{book,trades}.csv.gz. Only the 1st of a month is free without
# an API key (any other day answers 401). Existing files are kept; a coin not yet listed on a day
# (Tardis answers 404 or an empty file) is skipped and logged. Read-only on everything else.
set -euo pipefail
OUT=${1:?OUT_DIR}; FROM=${2:?FROM_YYYY-MM}; TO=${3:?TO_YYYY-MM}; COINS=${4:?COINS}
[ -d "$OUT" ] || { echo "$OUT does not exist — refusing (keep this off the root disk)" >&2; exit 1; }
IFS=, read -r -a coins <<< "$COINS"
d="$FROM-01"
while [[ "$d" < "$TO-02" ]]; do
  y=${d:0:4}; m=${d:5:2}
  for c in "${coins[@]}"; do
    dir="$OUT/raw/$c"; mkdir -p "$dir"
    for kind in book trades; do
      f="$dir/$d.$kind.csv.gz"
      [ -s "$f" ] && continue
      type=$([ "$kind" = book ] && echo book_snapshot_25 || echo trades)
      code=$(curl -s -m 300 -o "$f.part" -w '%{http_code}' \
        "https://datasets.tardis.dev/v1/hyperliquid/$type/$y/$m/01/$c.csv.gz" || echo 000)
      if [ "$code" = 200 ] && [ "$(stat -c %s "$f.part")" -gt 100 ]; then mv "$f.part" "$f"
      else rm -f "$f.part"; echo "skip $c $d $kind (HTTP $code)"; fi
    done
  done
  echo "done $d"
  d=$(date -d "$d +1 month" +%Y-%m-%d)
done
