#!/usr/bin/env bash
# fleet_add_variants.sh — add filter / exit variants of every BASE strategy to config/paper_fleet.json.
#
# A base is a fleet id without "__". For each base and each overlay below, a variant
# "<base>__<overlay>" is added (if it does not exist yet) with the base's params + the overlay's keys
# + base_id / overlay (the fields strategy_leaderboard() and the dashboard group by). Existing ids are
# never touched, so the script is idempotent and re-running it only adds what is missing.
#
# Overlays (2026-10-01, founder: "add regime filters on each strategy … and make more variations"):
#   rg_trend   trade only a trending session, with its direction  (efficiency >= 0.5, from 30 min in)
#   rg_range   trade only a choppy session                        (efficiency <= 0.4, from 30 min in)
#   rg_expand  trade only once the range has expanded             (range >= 0.35 x prior-day ATR)
#   rg_fade    trade against an extended session                  (move >= 0.35 x ATR, opposite side)
#   rg_nochase skip extended sessions                             (move < 0.5 x ATR)
#   rvol15     trade only on heavy relative volume                (minute volume >= 1.5x its 20-bar avg)
#   tr25       wider trailing stop                                (trail_step 25; beat its base 7/8 in tr25)
# The rg_* gates measure the session from the 09:30 ET open, so they are applied only to bases that
# trade inside regular hours (an Asia / London base would never pass the gate). rvol15 applies to every
# base; tr25 only to bases that have a trail_step.
#
# Usage: scripts/fleet_add_variants.sh [--dry-run] [fleet.json]
set -euo pipefail
DRY=0; [[ "${1:-}" == "--dry-run" ]] && { DRY=1; shift; }
FLEET="${1:-$(cd "$(dirname "$0")/.." && pwd)/config/paper_fleet.json}"

OVERLAYS='{
  "rg_trend":   {"regime_min_eff": 0.5, "regime_min_minutes": 30, "regime_with_move": 1},
  "rg_range":   {"regime_max_eff": 0.4, "regime_min_minutes": 30},
  "rg_expand":  {"regime_min_range_atr": 0.35, "regime_min_minutes": 15},
  "rg_fade":    {"regime_min_move_atr": 0.35, "regime_min_minutes": 30, "regime_with_move": -1},
  "rg_nochase": {"regime_max_move_atr": 0.5, "regime_min_minutes": 15},
  "rvol15":     {"rvol_min": 1.5, "rvol_bars": 20},
  "tr25":       {"trail_step": 25}
}'

tmp=$(mktemp)
jq --argjson ov "$OVERLAYS" '
  # HHMM a base starts trading (ET); null when it cannot be told
  def start_hhmm:
    if .engine == "orb" then ((.params.session_open_hour // 9) * 100 + (.params.session_open_min // 30))
    elif .engine == "trend" then (.params.win_start // 930)
    elif .engine == "mtf_scalper" then ((.params.session_window // "0900-1200") | .[0:4] | tonumber)
    else null end;
  def rth: (start_hhmm) as $s | ($s != null and $s >= 900 and $s < 1600);
  (.strategies | map(.id)) as $have
  | .strategies as $all
  | [ $all[] | select(.id | contains("__") | not) ] as $bases
  | [ $bases[] as $b
      | $ov | to_entries[]
      | select(
          (.key | startswith("rg_") | not) or ($b | rth)
        )
      | select(.key != "tr25" or ($b.params | has("trail_step")))
      | {id: ($b.id + "__" + .key), engine: $b.engine, enabled: true,
         params: ($b.params + .value + {base_id: $b.id, overlay: .key})}
      | select(.id as $i | $have | index($i) | not)
    ] as $new
  | .strategies += $new
  | ._added = ($new | length)
' "$FLEET" > "$tmp"

added=$(jq '._added' "$tmp")
jq 'del(._added)' "$tmp" > "$tmp.2" && mv "$tmp.2" "$tmp"
echo "fleet: $(jq '.strategies | length' "$FLEET") -> $(jq '.strategies | length' "$tmp") strategies (+$added)"
jq -r --slurpfile old "$FLEET" '
  ($old[0].strategies | map(.id)) as $o
  | [.strategies[] | select(.id as $i | $o | index($i) | not) | .params.overlay]
  | group_by(.) | map("  \(.[0]): \(length)") | .[]' "$tmp"
if (( DRY )); then rm -f "$tmp"; echo "(dry run — nothing written)"; exit 0; fi
bk="$(dirname "$FLEET")/../data/backups/fleet"; mkdir -p "$bk"; cp "$FLEET" "$bk/$(basename "$FLEET").bak-$(date +%Y%m%d-%H%M%S)"
mv "$tmp" "$FLEET"
echo "written: $FLEET (backup in data/backups/fleet/)"
