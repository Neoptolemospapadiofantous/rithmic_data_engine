#!/usr/bin/env bash
# funding_history.sh — funding-rate history (and Hyperliquid daily candles) for the funding-carry study
# (docs/funding_carry_plan.md, research/preregistered/2026-10-28_funding_carry.md). Public endpoints, no keys.
#
#   scripts/funding_history.sh coins                  # write <root>/coins.txt: active perps on Lighter ∩ Hyperliquid ∩ Binance
#   scripts/funding_history.sh {lighter|hl|bn|candles} [FROM_UNIX TO_UNIX]
#
# Output <root>/<venue>/<COIN>.json, one JSON array per coin (existing files are skipped, so a run resumes):
#   lighter  [{"t":unix,"rate":"%/h","dir":"long|short","value":…}]   hourly (duplicates at window edges are kept;
#                                                                       funding_lab drops them)
#   hl       [{"t":unix,"rate":"fraction/h"}]                          hourly
#   bn       [{"t":unix,"rate":"fraction per settlement"}]             8 h (some 4 h)
#   candles  [{"t":unix,"o":…,"h":…,"l":…,"c":…}]                      Hyperliquid 1d
# Root: $FUNDING_ROOT (default the second drive's rithmic_dex/funding). Default window 2025-10-28 → 2026-10-28 UTC.
# Lighter is paced at one request per 6 s: the live market maker uses Lighter's API from this same machine.
set -euo pipefail

ROOT=${FUNDING_ROOT:-/media/theone/381C48C51C48803C/rithmic_dex/funding}
LIGHTER=https://mainnet.zklighter.elliot.ai
HL=https://api.hyperliquid.xyz/info
BN=https://fapi.binance.com
mode=${1:?usage: funding_history.sh coins|lighter|hl|bn|candles [FROM TO]}
T0=${2:-1761609600}
T1=${3:-1793145600}
mkdir -p "$ROOT"

get() {   # url [post-body] — retries with back-off; prints the body
    local i
    for i in 1 2 3 4 5; do
        if [ $# -gt 1 ]; then
            curl -sf -m 30 -X POST -H 'content-type: application/json' -d "$2" "$1" && return 0
        else
            curl -sf -m 30 "$1" && return 0
        fi
        echo "retry $i: $1" >&2
        sleep $((10 * i))
    done
    echo "giving up: $1" >&2
    return 1
}

if [ "$mode" = coins ]; then
    get "$LIGHTER/api/v1/orderBooks" | jq -r '.order_books[] | select(.market_type=="perp" and .status=="active") | "\(.symbol) \(.market_id)"' | sort > "$ROOT/lighter_markets.txt"
    get "$HL" '{"type":"meta"}' | jq -r '.universe[] | select(.isDelisted != true) | .name' | sort > "$ROOT/.hl"
    get "$BN/fapi/v1/exchangeInfo" | jq -r '.symbols[] | select(.contractType=="PERPETUAL" and .status=="TRADING" and .quoteAsset=="USDT") | .baseAsset' | sort > "$ROOT/.bn"
    awk '{print $1}' "$ROOT/lighter_markets.txt" | comm -12 - "$ROOT/.hl" | comm -12 - "$ROOT/.bn" > "$ROOT/coins.txt"
    rm -f "$ROOT/.hl" "$ROOT/.bn"
    echo "$(wc -l < "$ROOT/coins.txt") coins on all three venues → $ROOT/coins.txt"
    exit 0
fi

[ -s "$ROOT/coins.txt" ] || { echo "run 'funding_history.sh coins' first" >&2; exit 1; }
out="$ROOT/$mode"
mkdir -p "$out"
while read -r c; do
    f="$out/$c.json"
    [ -s "$f" ] && continue
    parts=$(mktemp)
    case "$mode" in
        lighter)
            id=$(awk -v c="$c" '$1==c {print $2}' "$ROOT/lighter_markets.txt")
            s=$T0
            while [ "$s" -lt "$T1" ]; do
                e=$((s + 25 * 86400)); [ "$e" -gt "$T1" ] && e=$T1
                get "$LIGHTER/api/v1/fundings?market_id=$id&resolution=1h&start_timestamp=$s&end_timestamp=$e&count_back=1000" |
                    jq -c '[.fundings[]? | {t: .timestamp, rate, dir: .direction, value}]' >> "$parts"
                s=$e
                sleep 6
            done ;;
        hl)
            s=$((T0 * 1000))
            while [ "$s" -lt $((T1 * 1000)) ]; do
                page=$(get "$HL" "{\"type\":\"fundingHistory\",\"coin\":\"$c\",\"startTime\":$s,\"endTime\":$((T1 * 1000))}")
                n=$(jq 'length' <<<"$page"); [ "$n" -eq 0 ] && break
                jq -c '[.[] | {t: (.time / 1000 | floor), rate: .fundingRate}]' <<<"$page" >> "$parts"
                s=$(($(jq '.[-1].time' <<<"$page") + 1))
                sleep 1.5
            done ;;
        bn)
            s=$((T0 * 1000))
            while [ "$s" -lt $((T1 * 1000)) ]; do
                page=$(get "$BN/fapi/v1/fundingRate?symbol=${c}USDT&startTime=$s&endTime=$((T1 * 1000))&limit=1000")
                n=$(jq 'length' <<<"$page"); [ "$n" -eq 0 ] && break
                jq -c '[.[] | {t: (.fundingTime / 1000 | floor), rate: .fundingRate}]' <<<"$page" >> "$parts"
                s=$(($(jq '.[-1].fundingTime' <<<"$page") + 1))
                sleep 1
            done ;;
        candles)
            get "$HL" "{\"type\":\"candleSnapshot\",\"req\":{\"coin\":\"$c\",\"interval\":\"1d\",\"startTime\":$((T0 * 1000)),\"endTime\":$((T1 * 1000))}}" |
                jq -c '[.[] | {t: (.t / 1000 | floor), o, h, l, c}]' >> "$parts"
            sleep 1.5 ;;
        *) echo "unknown mode $mode" >&2; rm -f "$parts"; exit 2 ;;
    esac
    jq -s 'add // []' "$parts" > "$f.tmp" && mv "$f.tmp" "$f"
    rm -f "$parts"
    echo "$mode $c $(jq length "$f")"
done < "$ROOT/coins.txt"
echo "$mode done"
