#!/usr/bin/env bash
# lag_check.sh — exchange→box lag of a recorded venue archive: the median and p90 of (receive time − exchange
# timestamp) over today's book updates, per market. This is the number that picks a VPS region (142 ms from the home
# box to Lighter on 2026-10-25; a server in the venue's region should show tens of ms). Needs an NTP-synced clock to
# mean anything. Works on every archive layout: Lighter (lighter_recorder), Hyperliquid (hl_recorder) and the
# dex_recorder venues (Extended ts ms, Aster event time E ms, Paradex ts µs).
#
#   scripts/lag_check.sh /data/rithmic_dex/<venue> [YYYY-MM-DD]
set -euo pipefail
DIR=${1:?archive dir}; DAY=${2:-$(date -u +%F)}
printf "%-10s %8s %8s %8s\n" market n p50_ms p90_ms
for m in "$DIR"/*/; do
  f="$m/$DAY.jsonl.zst"; [ -s "$f" ] || continue
  zstdcat "$f" 2>/dev/null | jq -r '
      (.v // "") as $v
      | (if $v == "paradex" then .m.ts / 1000
         elif $v == "extended" then .m.ts
         elif $v == "aster" then .m.data.E
         else (.m.timestamp // (.m.data.time // null)) end) as $t
      | select($t != null and (
            ((.m.type // "") == "update/order_book") or ((.m.channel // "") == "l2Book")
            or ((.m.type // "") == "DELTA") or ((.m.data.e // "") == "depthUpdate") or ((.m.c // "") == "order_book")))
      | (.r / 1000 - $t)' 2>/dev/null \
  | sort -n | awk -v name="$(basename "$m")" '{a[NR]=$1} END{if(NR>0) printf "%-10s %8d %8.0f %8.0f\n", name, NR, a[int(NR*0.5)+1], a[int(NR*0.9)+1]}'
done
