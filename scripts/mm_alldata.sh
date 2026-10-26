#!/usr/bin/env bash
# mm_alldata.sh — "all data" view of the frozen market-making setting for the dashboard /mm page: one file,
# data/mm_gates/alldata.json, with every dataset we hold side by side.
#   history  — Tardis Hyperliquid sample days (rithmic_dex/tardis = 30 tuned coins, tardis_extra = 15 never-tuned coins),
#              build/mm_robust at Lighter fees: candidate + the one-at-a-time variants, per-day P&L, bootstrap stats.
#              History never changes, so it is recomputed only when missing or when build/mm_robust is newer.
#   hl_live  — our own Hyperliquid recording (rithmic_dex/hyperliquid), same tool, AT LIGHTER FEES (hypothetical:
#              Hyperliquid's real 1.5 bps maker fee loses) — recomputed every run.
#   lighter  — the G1 replay rows mm_gates.sh just wrote (g1_daily.json: Lighter's own books, Hyperliquid tick rule,
#              Lighter grid, 300 ms), grouped per UTC day; partial days are kept and flagged.
#   venues   — the per-day rows mm_venues.sh just wrote (venues.json .daily: Extended / Aster / Paradex at real fees).
#   regime   — build/mm_regime with the pre-registered filter (trades/min ≤ 20 AND spread ≥ 2 bps) + each feature's
#              tuning-chosen buckets: history (data/mm_gates/dash/regime_filters*.json, written 2026-10-25 by the regime
#              study) and, recomputed every run, Lighter (--params params_lighter.json: strategy tick unit) and our
#              Hyperliquid recording (every day holdout). Pre-registration: research/preregistered/2026-10-25_mm_skip_days.md.
#   weekdays — per-day P&L of the history split by UTC weekday / weekend (H1 of the same pre-registration).
# Read-only on the archives; writes only under data/mm_gates. mm_gates.sh calls it at the end (fail-open);
# by hand: scripts/mm_alldata.sh
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=data/mm_gates; A="$OUT/alldata"; mkdir -p "$A"
ROOT=/media/theone/381C48C51C48803C/rithmic_dex
STAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)
log() { printf '[%s] [mm_alldata] %s\n' "$(date -u +%H:%M:%S)" "$*"; }
[ -x build/mm_robust ] || { log "build/mm_robust missing"; exit 0; }
[ -d "$ROOT" ] || { log "archive drive not mounted ($ROOT)"; exit 0; }

robust() {   # robust <dataset> <always:0|1>
  local d=$1 always=$2
  [ -d "$ROOT/$d" ] || { log "$d: no archive"; return; }
  if [ "$always" = 1 ] || [ ! -s "$A/$d/robust_variants.json" ] || [ build/mm_robust -nt "$A/$d/robust_variants.json" ]; then
    if nice build/mm_robust --dir "$ROOT/$d" --threads 3 --dash "$A/$d.tmp" --report "$A/$d.tmp/report.md" > "$A/$d.log" 2>&1; then
      rm -rf "$A/$d"; mv "$A/$d.tmp" "$A/$d"; log "$d: recomputed"
    else log "$d: mm_robust failed (see $A/$d.log)"; rm -rf "$A/$d.tmp"; fi
  fi
}
robust tardis 0
robust tardis_extra 0
robust hyperliquid 1

PREREG=(--allow activity=0,1 --allow spread=1,2,3)
regime() {   # regime <dataset> [extra args] — every day is holdout (live recordings)
  local d=$1; shift
  [ -d "$ROOT/$d" ] && [ -x build/mm_regime ] || return
  if nice build/mm_regime --dir "$ROOT/$d" --threads 3 --split 2000-01-01 "${PREREG[@]}" "$@" --dash "$A/regime_$d.tmp" \
       --report "$A/regime_$d.tmp/report.md" > "$A/regime_$d.log" 2>&1; then rm -rf "$A/regime_$d"; mv "$A/regime_$d.tmp" "$A/regime_$d"
  else log "regime $d failed (see $A/regime_$d.log)"; rm -rf "$A/regime_$d.tmp"; fi
}
[ -s "$OUT/params_lighter.json" ] && regime lighter --params "$OUT/params_lighter.json"
regime hyperliquid

hist() {     # hist <dataset> → JSON object or null
  local d="$A/$1"
  [ -s "$d/robust_variants.json" ] || { echo null; return; }
  jq -n --slurpfile v "$d/robust_variants.json" --slurpfile s "$d/robust_stats.json" --slurpfile dd "$d/robust_days.json" --slurpfile c "$d/robust_coins.json" '
    {variants: $v[0].rows, stats: ($s[0].rows | map({(.metric): .value}) | add), days: $dd[0].rows,
     coins: ($c[0].rows | length), coins_best: ($c[0].rows | sort_by(-.pnl_usd) | .[:3]), coins_worst: ($c[0].rows | sort_by(.pnl_usd) | .[:3])}'
}
LIGHTER='[]'
if [ -s "$OUT/g1_daily.json" ]; then
  LIGHTER=$(jq -c '[.rows[] | select(.cfg=="chosen/pess")] | group_by(.day) | map({day: .[0].day, hours: (map(.hours)|max),
      pnl_usd: (map(.pnl_usd)|add), volume_usd: (map(.volume_usd)|add), fills: (map(.fills)|add), markets: length,
      mk10_w: (map(.mk10_bps * .volume_usd)|add)}) | map(. + {complete: (.hours >= 23.5),
      net_bps: (if .volume_usd > 0 then .pnl_usd/.volume_usd*1e4 else null end),
      mk10_bps: (if .volume_usd > 0 then .mk10_w/.volume_usd else null end)} | del(.mk10_w))' "$OUT/g1_daily.json")
fi
VENUES='[]'; [ -s "$OUT/venues.json" ] && VENUES=$(jq -c '.daily // []' "$OUT/venues.json")
rows() { [ -s "$1" ] && jq -c '.rows' "$1" || echo null; }
REGIME=$(jq -n --argjson ht "$(rows "$OUT/dash/regime_filters.json")" --argjson hu "$(rows "$OUT/dash/regime_filters_unseen.json")" \
  --argjson li "$(rows "$A/regime_lighter/regime_filters.json")" --argjson hl "$(rows "$A/regime_hyperliquid/regime_filters.json")" \
  '{history_tuned: $ht, history_unseen: $hu, lighter: $li, hl_live: $hl}')
weekdays() {  # per-day P&L with the UTC weekday
  [ -s "$A/$1/robust_days.json" ] || { echo null; return; }
  jq -r '.rows[] | [.day, .pnl_usd, .holdout] | @tsv' "$A/$1/robust_days.json" | while IFS=$'\t' read -r day p ho; do
    printf '{"day":"%s","dow":"%s","pnl_usd":%s,"holdout":%s}\n' "$day" "$(date -u -d "$day" +%a)" "$p" "$ho"; done | jq -s -c .
}
WEEK=$(jq -n --argjson t "$(weekdays tardis)" --argjson e "$(weekdays tardis_extra)" '{tardis: $t, tardis_extra: $e}')

jq -n --arg stamp "$STAMP" --argjson tardis "$(hist tardis)" --argjson extra "$(hist tardis_extra)" --argjson hl "$(hist hyperliquid)" \
      --argjson lighter "$LIGHTER" --argjson venues "$VENUES" --argjson regime "$REGIME" --argjson week "$WEEK" '
  {generated: $stamp, split: "2025-11-01",
   history: {tardis: $tardis, tardis_extra: $extra}, hl_live: $hl, lighter: $lighter, venues: $venues, regime: $regime, days: $week}' > "$OUT/alldata.json.tmp" \
  && mv "$OUT/alldata.json.tmp" "$OUT/alldata.json" && log "wrote $OUT/alldata.json"
