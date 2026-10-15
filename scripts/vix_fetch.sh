#!/usr/bin/env bash
# vix_fetch.sh — refresh the VIX regime inputs the hold-mode VIX gates read (src/paper/vix_regime.hpp).
# Downloads CBOE's daily VIX and VIX3M histories (public CSVs, MM/DD/YYYY,OPEN,HIGH,LOW,CLOSE) into data/regime/.
# The engine derives the 5-day change and the 252-day percentile itself. Fail-open: a failed download keeps the
# previous file (the gates then run on yesterday's regime); no file at all = gated variants never enter.
# Cron (not version-controlled — this line is its record; box clock is Asia/Nicosia = ET+7): 30 0,2 * * 2-6 cd $HOME/Desktop/rithmic_engine && scripts/vix_fetch.sh >> data/logs/vix_fetch.log 2>&1
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; OUT="$REPO/data/regime"; mkdir -p "$OUT"
UA='Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/129.0 Safari/537.36'
rc=0
for idx in VIX VIX3M; do
  tmp="$OUT/${idx}_History.csv.tmp"
  if curl -sfL -A "$UA" --max-time 60 -o "$tmp" "https://cdn.cboe.com/api/global/us_indices/daily_prices/${idx}_History.csv" \
     && [[ $(wc -l < "$tmp") -gt 1000 ]] && head -1 "$tmp" | grep -q CLOSE; then
    mv "$tmp" "$OUT/${idx}_History.csv"; echo "$(date -Is) $idx ok $(tail -1 "$OUT/${idx}_History.csv")"
  else
    rm -f "$tmp"; echo "$(date -Is) $idx FAILED — keeping the previous file"; rc=1
  fi
done
exit $rc
