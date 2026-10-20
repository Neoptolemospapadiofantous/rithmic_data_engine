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
  "ema_cross_study|" "mtf_entry_study|" "volume_shock_study|" "orderflow_hf|" "repeat_entries|NQ" "flag_pullback|NQ" "flag_optimize|NQ" "account_5aday|" "winrate_ops|" "flag_trades_export|" "topone_sim|research/programs/inputs_20261019 2" "topone_stress|research/programs/inputs_20261019" "topone_monthly|research/programs/inputs_20261019" "flag_filters|" "flag_loosen|" "flag_lock|" "vwap_flags_sd|NQ" "flag_wr|" "flag_trail|" "topone_plan_math|" "flagx_export|" "dir1000_export|" "flag_features|" "flag_refined|NQ" "flag_daily|" "flag_regime_fail|" "flag_explain|20260212 1770892740 5" "flag_swing|" "flag_pullback_entry|" "flag_trail5|" "topone_evaltime|research/programs/inputs_20261019" "flag_tradelist|20231016" "flag_ema|" "flag_mid_wick|" "flag_proper|" "flag_wickentry|" "topone_flags|research/programs/inputs_20261019" "flag_optimize_wick|" "flag_trail_wide|" "flag_target_pts|" "flag_structure_matrix|" "choch_ny_detail|" "ny_setups_export|" "flag_sequence|" "flag_opt_all|" "flag_micro|" "flag_realtime|" "flag_mtf_entry|" "flag_review|" "flag_review2|" "flag_inner|" "flag_turn|" "flag_take_opt|" "box_fade|" "flag_quick_target|" "flag_grade|" "flag_exits|" "flag_all_kpi|" "flag_book_vest|" "flag_vest_opt|" "flag_flow_check|" "flag_es_dollar|" "flag_vest_mc|" "flag_1m_select|" "flag_shape|" "flag_wr_pf|" "flag_ema_feat|" "flag_pb_then_flag|"
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
