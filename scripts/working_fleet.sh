#!/usr/bin/env bash
# working_fleet.sh — score the working fleet (config/working_fleet.json) against the rules written in it.
#
#   scripts/working_fleet.sh            forward results (entries on/after forward_from) + the rule verdict per strategy
#   scripts/working_fleet.sh backfill   the same table on the recorded-session replays (label backfill), for orientation
#
# Read-only. Forward label: 'tradeify' for the NQ fleet, 'es' for the ES fleet. Points are per contract (pnl_pts);
# the score subtracts each strategy's cost_pts (the real round trip edge_lab uses).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
export PGPASSWORD="$(grep -E '^PG_PASSWORD=' .env | head -1 | cut -d= -f2- | tr -d "'\"")"
PSQL=(psql -h 127.0.0.1 -U rithmic_user -d rithmic -Atq -F $'\t' -v ON_ERROR_STOP=1)
F=config/working_fleet.json
mode="${1:-forward}"
from=$(jq -r .forward_from "$F")
[[ "$mode" == forward || "$mode" == backfill ]] || { sed -n 2,6p "$0"; exit 2; }

printf '%-26s %-10s %6s %9s %6s %9s %9s  %s\n' id tier trades avg_pts PF net_pts max_dd verdict
declare -A AVG DD N
while IFS=$'\t' read -r id fleet tier core cost; do
    if [[ "$mode" == forward ]]; then label=$([[ "$fleet" == es ]] && echo es || echo tradeify); since="$from"
    else label=backfill; since="2000-01-01"; fi
    row=$("${PSQL[@]}" -c "
      WITH t AS (SELECT entry_time, pnl_pts - $cost AS n FROM paper_trades
                 WHERE strategy_id = '$id' AND account_label = '$label' AND exit_time IS NOT NULL
                   AND entry_time >= '$since'::date ORDER BY entry_time),
           e AS (SELECT entry_time, sum(n) OVER (ORDER BY entry_time) AS eq FROM t),
           d AS (SELECT eq - greatest(0, max(eq) OVER (ORDER BY entry_time)) AS dd FROM e)
      SELECT count(*), coalesce(round(avg(n)::numeric, 2), 0),
             coalesce(round((sum(n) FILTER (WHERE n > 0) / nullif(-sum(n) FILTER (WHERE n < 0), 0))::numeric, 2), 0),
             coalesce(round(sum(n)::numeric, 1), 0),
             coalesce((SELECT round(-min(dd)::numeric, 1) FROM d), 0)
      FROM t") || { echo "query failed for $id"; continue; }
    IFS=$'\t' read -r n avg pf net dd <<<"$row"
    N[$id]=$n; AVG[$id]=$avg; DD[$id]=$dd
    verdict="collecting"
    case "$tier" in
      core)      if (( n >= 60 )); then verdict=$(awk -v a="$avg" 'BEGIN{print (a < 0) ? "DROP (avg < 0 after 60)" : "KEEP"}'); fi ;;
      candidate) if (( n >= 40 )); then verdict=$(awk -v a="$avg" -v p="$pf" 'BEGIN{print (a > 0 && p >= 1.1) ? "PASS -> edge_lab re-test" : "RETIRE"}'); fi ;;
      variant)   if (( n >= 60 && ${N[$core]:-0} >= 60 )); then
                     verdict=$(awk -v a="$avg" -v b="${AVG[$core]}" -v d="$dd" -v e="${DD[$core]}" 'BEGIN{print (a >= b + 1 && d <= e) ? "PROMOTE over core" : "keep core"}')
                 fi ;;
    esac
    [[ "$mode" == backfill ]] && verdict="(replay — rules apply to forward only)"
    printf '%-26s %-10s %6s %9s %6s %9s %9s  %s\n' "$id" "$tier" "$n" "$avg" "$pf" "$net" "$dd" "$verdict"
done < <(jq -r '.strategies[] | [.id, .fleet, .tier, (.core // "-"), .cost_pts] | @tsv' "$F")
echo
echo "rules: $(jq -r '.rules | to_entries[] | "\(.key): \(.value)"' "$F" | tr '\n' ' ')" | fold -s -w 120
if [[ "$mode" == forward ]]; then echo "forward from $from (label tradeify / es)"; fi
