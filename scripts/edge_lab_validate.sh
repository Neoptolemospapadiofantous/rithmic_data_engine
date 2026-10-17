#!/usr/bin/env bash
# edge_lab_validate.sh — prove the research pipeline does what research/README.md says, with ideas whose right answer
# is known in advance (research/validation/*.json, field "expect"). Nothing here is a real idea: no DB load, no registry.
#
#   scripts/edge_lab_validate.sh            all checks (≈ 3 min; the synthetic markets need ≈ 1.3 GB of scratch while they run)
#   scripts/edge_lab_validate.sh --fast     real-data checks only (≈ 15 s)
#
# Checks
#   null_*     coin-flip rules on real NQ          → must FAIL (a pass = a false positive)
#   leak_*     rules that read a future price     → must make no trades (a trade = look-ahead in the evaluator)
#   drift_long buy every day                       → must FAIL at CONTROLS (it is the market's drift, not an edge)
#   plant_*    synthetic market with a planted edge → must PASS at 8 pts, FAIL at 0 pts; nulls must FAIL there too;
#              3 / 5 pts show the smallest edge the gates can see
#   repro      every PASS idea in research_ideas re-run → same verdict and the same pts/trade (± 0.01)
# Output: data/edge_lab/validation/summary.json (read by the dashboard's research-lab page) + summary.md.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
BIN="${EDGE_LAB_BIN:-build/edge_lab}"; SYN="${SYNTH_BIN:-build/synth_market}"
OUT=data/edge_lab/validation; WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"; FAST=0; [[ "${1:-}" == "--fast" ]] && FAST=1
[[ -x "$BIN" ]] || { echo "no $BIN — cmake --build build --target edge_lab"; exit 1; }
rows="$WORK/rows.jsonl"; : >"$rows"

# one JSON line per check: group id name expect verdict stage oos_n oos_avg ok note
judge() {   # judge GROUP OUTDIR SPEC [EXPECT-OVERRIDE] [NOTE]
    local group="$1" dir="$2" spec="$3" id expect r
    id=$(jq -r .id "$spec"); expect="${4:-$(jq -r .expect "$spec")}"
    r="$dir/reports/$id.json"
    [[ -s "$r" ]] || { jq -nc --arg g "$group" --arg id "$id" --arg e "$expect" '{group:$g,id:$id,expect:$e,verdict:"NO RUN",ok:false}' >>"$rows"; return; }
    jq -c --arg g "$group" --arg e "$expect" --arg note "${5:-}" --arg name "$(jq -r .name "$spec")" '
      (.verdict) as $v | (.oos.n // 0) as $n |
      {group:$g, id:.id, name:$name, expect:$e, verdict:$v, stage:.stage_reached, oos_n:$n, oos_avg:(.oos.avg_pts // 0), note:$note,
       ok:( if $e == "fail" then ($v | startswith("PASS") | not)
            elif $e == "pass" then ($v | startswith("PASS"))
            elif $e == "noedge" then ($n == 0)
            elif $e == "fail_controls" then ($v == "FAIL at CONTROLS")
            else false end)}' "$r" >>"$rows"
}

echo "== real data: noise, look-ahead traps, drift"
"$BIN" run research/validation/null_*.json research/validation/leak_*.json research/validation/drift_long.json --out "$WORK/real" >/dev/null 2>&1
for f in research/validation/null_*.json; do judge "noise on real NQ" "$WORK/real" "$f"; done
for f in research/validation/leak_*.json; do judge "look-ahead traps" "$WORK/real" "$f"; done
judge "market drift" "$WORK/real" research/validation/drift_long.json

if (( ! FAST )); then
    [[ -x "$SYN" ]] || { echo "no $SYN — cmake --build build --target synth_market"; exit 1; }
    for k in 8 5 3 0; do
        echo "== synthetic market, planted edge $k pts"
        mkdir -p "$WORK/d$k" "$WORK/o$k"; "$SYN" "$WORK/d$k" "$k" 7 >/dev/null
        if [[ $k == 8 || $k == 0 ]]; then "$BIN" run research/validation/plant_even_day.json research/validation/null_*.json --data "$WORK/d$k" --out "$WORK/o$k" >/dev/null 2>&1
        else "$BIN" run research/validation/plant_even_day.json --data "$WORK/d$k" --out "$WORK/o$k" >/dev/null 2>&1; fi
        case $k in
          8) judge "planted edge (synthetic)" "$WORK/o8" research/validation/plant_even_day.json pass "8 pts planted: must be found"
             for f in research/validation/null_*.json; do judge "noise on synthetic data" "$WORK/o8" "$f"; done ;;
          0) judge "planted edge (synthetic)" "$WORK/o0" research/validation/plant_even_day.json fail "0 pts planted: nothing to find" ;;
          *) judge "detection limit (synthetic)" "$WORK/o$k" research/validation/plant_even_day.json info "$k pts planted: shows the smallest edge the gates can see" ;;
        esac
        rm -rf "$WORK/d$k"
    done
    echo "== determinism: the same idea twice on the same data"
    for i in 1 2; do "$BIN" run research/ideas/so_cascade.json --out "$WORK/det$i" >/dev/null 2>&1; done
    jq -nc --slurpfile a "$WORK/det1/reports/so_cascade.json" --slurpfile b "$WORK/det2/reports/so_cascade.json" '
      {group:"determinism", id:"so_cascade", name:$a[0].name, expect:"identical twice", verdict:$b[0].verdict, stage:$b[0].stage_reached,
       oos_n:$b[0].oos.n, oos_avg:$b[0].oos.avg_pts,
       ok:(($a[0] | del(.seconds)) == ($b[0] | del(.seconds))),
       note:"every number in the report card compared, run time excluded"}' >>"$rows"
    echo "== reproducibility: every PASS idea re-run"
    # newest input file: a re-run may legitimately differ when the data moved after the stored run
    newest=$(stat -c %Y data/edge_lab/cache/{NQ,ES}.csv data/edge_lab/cache/{macro,releases,events}.csv data/edge_lab/cache/fomc.txt data/regime/VIX*_History.csv 2>/dev/null | sort -n | tail -1)
    while IFS=$'\t' read -r id path avg verdict ran; do
        [[ -f "$path" ]] || continue
        "$BIN" run "$path" --out "$WORK/repro" >/dev/null 2>&1
        r="$WORK/repro/reports/$id.json"
        [[ -s "$r" ]] || { jq -nc --arg id "$id" '{group:"reproducibility",id:$id,expect:"same",verdict:"NO RUN",ok:false}' >>"$rows"; continue; }
        moved=$(( newest > ${ran%.*} ? 1 : 0 ))
        jq -c --arg id "$id" --arg was "$verdict" --argjson avg "$avg" --argjson moved "$moved" '
          {group:"reproducibility", id:$id, name:.name, expect:("same as stored: " + $was), verdict:.verdict, stage:.stage_reached,
           oos_n:(.oos.n // 0), oos_avg:(.oos.avg_pts // 0), stored_avg:$avg,
           same:(.verdict == $was and (((.oos.avg_pts // 0) - $avg) | fabs) < 0.01)} |
          .ok = (.same or ($moved == 1 and (.verdict == $was))) |
          .note = (if .same then "identical" elif $moved == 1 and .verdict == $was then "same verdict; numbers moved because the input data was updated after the stored run"
                   elif $moved == 1 then "VERDICT CHANGED after a data update — re-run and review" else "CHANGED with the same data: code changed or the run is not deterministic" end)' "$r" >>"$rows"
    done < <("${PSQL[@]}" -F $'\t' -c "SELECT id, metrics->>'spec_path', (metrics->'oos'->>'avg_pts')::float, verdict, extract(epoch FROM last_run) FROM research_ideas
                                       WHERE verdict = 'PASS' AND id NOT LIKE '%\_\_%' AND NOT coalesce((metrics->>'quick')::bool, false) ORDER BY id")
fi

jq -s --arg ts "$(date -u +%FT%TZ)" --argjson fast "$FAST" '
  {ts:$ts, fast:($fast == 1), checks:., total:length, passed:(map(select(.expect != "info" and .ok)) | length),
   failed:(map(select(.expect != "info" and (.ok | not))) | length),
   groups:(group_by(.group) | map({group:.[0].group, n:length, ok:(map(select(.expect == "info" or .ok)) | length)}))}' "$rows" >"$OUT/summary.json.tmp" \
  && mv "$OUT/summary.json.tmp" "$OUT/summary.json"
jq -r '"# edge_lab validation — \(.ts)\n\n\(.passed) checks as expected, \(.failed) not.\n",
       (.groups[] | "- \(.group): \(.ok)/\(.n)"),
       "\n| group | check | expected | got | ok |\n|---|---|---|---|---|",
       (.checks[] | "| \(.group) | \(.id) | \(.expect) | \(.verdict) (\(.oos_n // 0) trades, \((.oos_avg // 0) * 100 | round / 100) pts) | \(if .expect == "info" then "info" elif .ok then "yes" else "NO" end) |")' "$OUT/summary.json" >"$OUT/summary.md"
jq -r '"validation: \(.passed) as expected, \(.failed) not"' "$OUT/summary.json"
jq -r '.checks[] | select(.expect != "info" and (.ok | not)) | "  NOT AS EXPECTED: \(.group) / \(.id): expected \(.expect), got \(.verdict) \(.note // "")"' "$OUT/summary.json"
