#!/usr/bin/env bash
# working_fleet_report.sh — the forward scorecard on a timer (working-fleet-report.timer, weekdays 16:30 New York).
# Runs scripts/working_fleet.sh (forward results since config/working_fleet.json forward_from, the rule verdict per strategy)
# and writes data/working_fleet/<date>.txt + latest.txt. Telegram (grid-notify): once when any strategy's verdict CHANGES
# (collecting → PASS / RETIRE / KEEP / DROP / PROMOTE), and a short weekly summary on Fridays. Read-only on Postgres.
#   scripts/working_fleet_report.sh [--notify] [--weekly]     (the timer passes --notify; --weekly forces the summary)
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
notify=0; weekly=0
for a in "$@"; do case "$a" in --notify) notify=1 ;; --weekly) weekly=1 ;; *) sed -n 2,6p "$0"; exit 2 ;; esac; done
[[ "$(TZ=America/New_York date +%u)" == 5 ]] && weekly=1
OUT=data/working_fleet; mkdir -p "$OUT"; d=$(TZ=America/New_York date +%F)
scripts/working_fleet.sh > "$OUT/$d.txt" 2>&1 || { echo "working_fleet.sh failed"; cat "$OUT/$d.txt"; exit 1; }
cp "$OUT/$d.txt" "$OUT/latest.txt"
# verdict per id: the text after the 7 numeric columns
cur=$(awk 'NR > 1 && NF >= 8 && $3 ~ /^[0-9]+$/ { v = ""; for (i = 8; i <= NF; i++) v = v (i > 8 ? " " : "") $i; print $1 "\t" v }' "$OUT/$d.txt")
prev_f="$OUT/.verdicts"; changes=""
while IFS=$'\t' read -r id v; do
  [[ -n "$id" ]] || continue
  old=$(awk -F'\t' -v k="$id" '$1 == k { print $2 }' "$prev_f" 2>/dev/null)
  [[ -n "$old" && "$old" != "$v" ]] && changes+="$id: $old → $v"$'\n'
done <<<"$cur"
printf '%s\n' "$cur" > "$prev_f"
gn=$(command -v grid-notify || echo "$HOME/.local/bin/grid-notify")
if (( notify )) && [[ -x "$gn" ]]; then
  [[ -n "$changes" ]] && "$gn" "Working fleet — verdict changed ($d):"$'\n'"$changes" >/dev/null 2>&1 || true
  if (( weekly )); then
    top=$(awk 'NR > 1 && NF >= 8 && $3 ~ /^[0-9]+$/ && $3 > 0 { printf "%s %s trades, %s pts/trade after cost, PF %s — %s\n", $1, $3, $4, $5, ($8 == "collecting" ? "collecting" : $8) }' "$OUT/$d.txt" | head -30)
    "$gn" "Working fleet — forward week to $d (since $(jq -r .forward_from config/working_fleet.json)):"$'\n'"${top:-no forward trades yet}" >/dev/null 2>&1 || true
  fi
fi
cat "$OUT/$d.txt"
