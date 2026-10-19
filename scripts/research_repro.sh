#!/usr/bin/env bash
# research_repro.sh — re-run the archived research programs (research/programs/) and compare each output with the one saved in
# research/programs/out/ when the result was reported. Read-only on market data; builds into a temp dir.
#   scripts/research_repro.sh            every program listed below
#   scripts/research_repro.sh NAME …     only those
# Exit 0 when every output is identical. A difference means the data under data/edge_lab/cache changed (re-exported) or the
# program changed since the result was written — the report says which file differs and shows the first differing lines.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
P=research/programs; O=$P/out; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
# name | arguments (the run whose output is in out/<name>.txt)
LIST=(
  "regopt|data/edge_lab/cache 200" "regcal|" "reentry|" "reentry_pay|" "perm|$O/mtf_ldn.csv" "emastop|" "emaexit|" "openreg|" "emasweep|"
  "emamtf|" "emamom|" "mom2|" "momopt|" "momx|" "momadd|" "sprint|" "sprint3|" "sprint4|" "evalspeed|" "payfast|" "planchk|"
  "session_model|" "intraday_models|" "behavior_sessions|" "behavior3|research/programs/inputs_20261019/cascade_2r.trades.csv"
  "target_by_range|research/programs/out/intraday_range_pred.csv research/programs/inputs_20261019/cascade_2r.trades.csv research/programs/inputs_20261019/gap_retest_2r.trades.csv research/programs/inputs_20261019/gap_rule_1000_2r.trades.csv"
)
want=("$@"); same=0; diff_n=0; fail=0
for row in "${LIST[@]}"; do n="${row%%|*}"; a="${row#*|}"
  if [[ ${#want[@]} -gt 0 && ! " ${want[*]} " == *" $n "* ]]; then continue; fi
  [[ -f "$O/$n.txt" ]] || { echo "SKIP  $n (no saved output)"; continue; }
  if ! g++ -O2 -std=c++20 -Isrc -I"$P" "$P/$n.cpp" -o "$T/$n" 2>"$T/$n.err"; then echo "FAIL  $n — does not compile"; head -3 "$T/$n.err"; fail=$((fail + 1)); continue; fi
  # shellcheck disable=SC2086
  "$T/$n" $a > "$T/$n.txt" 2>/dev/null
  if cmp -s "$T/$n.txt" "$O/$n.txt"; then echo "SAME  $n"; same=$((same + 1))
  else echo "DIFF  $n"; diff "$O/$n.txt" "$T/$n.txt" | head -6 | sed 's/^/        /'; diff_n=$((diff_n + 1)); fi
done
echo "research_repro: $same identical, $diff_n different, $fail failed to build"
[[ $diff_n -eq 0 && $fail -eq 0 ]]
