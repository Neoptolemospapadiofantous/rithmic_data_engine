#!/usr/bin/env bash
# readiness.sh — applies docs/readiness_standard.md to every setup we might trade (2026-10-28).
#
#   scripts/readiness.sh            → data/readiness/scorecard.md + scorecard.csv (+ reports/ with the edge_lab full-history runs)
#
# 1. every setup's idea spec is copied with the 2024-12 cut removed (id <id>__full) and, when it has none, a second-market check
#    (NQ ↔ ES), then run through edge_lab on the full history with --trades (walk-forward trades + screen / controls / robustness);
# 2. a TSV of the edge_lab gates, the paper-fleet build map and the forward scorecard (data/working_fleet/latest.txt) is written;
# 3. research/programs/readiness.cpp computes the remaining gates from the walk-forward trades and writes the scorecard.
# Read-only on market data and Postgres. Uses build-exp/edge_lab (never build/ during RTH); EDGE_LAB_BIN overrides.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
OUT=data/readiness; mkdir -p "$OUT/specs" "$OUT/reports"
BIN="${EDGE_LAB_BIN:-build-exp/edge_lab}"
[[ -x "$BIN" ]] || { echo "no $BIN"; exit 1; }
# setup → market, paper id it runs as (- = not built). Every edge_lab PASS setup the trading book uses.
SETUPS=(
  "weak_day_long NQ -"            "monday_long NQ monday_long"     "so_cascade NQ so_cascade_2r"
  "cascade_noon NQ -"             "strong_open NQ -"               "gap_or_retest NQ gap_or_retest"
  "gap_breakout NQ -"             "bdh_opt NQ -"                   "flag5_trend NQ -"
  "mom_scan_3_pb NQ -"            "bos_ema_gap NQ -"               "ema_cross_vwap NQ -"
  "scalp_m1_1000 NQ -"            "dir_market NQ -"                "pat_3bar NQ -"
  "es_cascade ES es_cascade_2r"   "es_weak_day ES -"               "es_gap_or_retest ES -"
)
specs=()
for row in "${SETUPS[@]}"; do read -r id mk _ <<<"$row"
  src="research/ideas/$id.json"; [[ -f "$src" ]] || { echo "missing $src"; exit 1; }
  other=$([[ "$mk" == ES ]] && echo NQ || echo ES)
  jq --arg id "${id}__full" --arg o "$other" '.id = $id | del(.to) | (if .confirm then . else .confirm = $o end)' "$src" > "$OUT/specs/$id.json"
  specs+=("$OUT/specs/$id.json")
done
echo "edge_lab: ${#specs[@]} setups on the full history …"
nice -n 10 "$BIN" run "${specs[@]}" --out "$OUT" --trades --threads 8 > "$OUT/edge_lab.log" 2>&1 || { tail -5 "$OUT/edge_lab.log"; exit 1; }
FWD=data/working_fleet/latest.txt
fwd() {   # paper id → "n avg pf" from the forward scorecard (0 0 0 if absent)
  [[ "$1" == - || ! -f "$FWD" ]] && { echo "0 0 0"; return; }
  awk -v id="$1" '$1 == id { print $3, $4, $5; f = 1 } END { if (!f) print "0 0 0" }' "$FWD"; }
tsv="$OUT/inputs.tsv"
printf 'id\tmarket\tsealed\tscreen\tdir_pct\ttime_pct\tday_pct\tneed\trobust_ok\tlate_ok\tplateau_ok\tconfirm_ok\tbuilt\tparity\tfwd_n\tfwd_avg\tfwd_pf\twf_file\n' > "$tsv"
for row in "${SETUPS[@]}"; do read -r id mk pid <<<"$row"
  j="$OUT/reports/${id}__full.json"; [[ -f "$j" ]] || { echo "no report for $id"; exit 1; }
  sealed=$(jq -r 'if .to then 1 else 0 end' "research/ideas/$id.json")
  g=$(jq -r '[ (.gates[] | select(.gate == "screen") | if .pass then 1 else 0 end),
               (.controls.dir_pct // -1), (.controls.time_pct // -1), (.controls.day_pct // -1), (.controls.need_pct // -1),
               (.gates[] | select(.gate == "robustness") | if .pass then 1 else 0 end),
               ((.robust.checks // [])[] | select(.name == "entry 1 bar late") | if .ok then 1 else 0 end) // -1,
               ((.robust.checks // [])[] | select(.name | startswith("plateau")) | if .ok then 1 else 0 end) // -1,
               ((.robust.checks // [])[] | select(.name | startswith("confirm on")) | if .ok then 1 else 0 end) // -1 ] | @tsv' "$j")
  built=$([[ "$pid" == - ]] && echo 0 || echo 1)
  read -r fn fa fp <<<"$(fwd "$pid")"
  printf '%s\t%s\t%s\t%s\t%s\t0\t%s\t%s\t%s\t%s\n' "$id" "$mk" "$sealed" "$g" "$built" "$fn" "$fa" "$fp" "$OUT/reports/${id}__full.wf_trades.csv" >> "$tsv"
done
# the London VWAP-SD wave is not an edge_lab spec: its fixed config's trades (ema721_wave_search dump); gates edge_lab would run
# (controls, robustness, second market) are NOT RUN until it is written as a spec — the scorecard shows that as FAIL
printf 'wave_ldn5\tNQ\t0\t1\t-1\t-1\t-1\t-1\t-1\t-1\t-1\t-1\t1\t0\t0\t0\t0\tresearch/programs/inputs_20261028/ldn5_NQ.csv\n' >> "$tsv"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
g++ -O2 -std=c++20 -Isrc -Iresearch/programs research/programs/readiness.cpp -o "$tmp/readiness" -lzstd || exit 1
"$tmp/readiness" "$tsv" "$OUT" || exit 1
echo "→ $OUT/scorecard.md"
