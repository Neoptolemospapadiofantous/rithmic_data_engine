#!/usr/bin/env bash
# desk_mm.sh — the StarNet desk's judge for MARKET-MAKING ideas (2026-10-26). Deterministic, no LLM. Mirrors
# desk_edge.sh: researchers propose ideas, this script puts every one through mm_lab (src/dex/mm_lab.hpp) IN-SAMPLE,
# and gives the in-sample passes one look at the SEALED holdout, written where only the final reporter reads.
#
# Input : every $DESK/*-MMRESEARCH*.md not judged yet. Each carries ONE fenced ```json block {"ideas":[{…}, …]}
#         (format: `mm_lab` header / research_data/mm_lab/README.md).
# Output: $DESK/<stamp>-MMLAB.md                     accepted / refused + why, in-sample verdicts
#         $RDATA (research_data/mm_lab/)             README (rules + feature list), registry.md, cards/<id>.{md,json}
#         $HOLD/<stamp>-MM-HOLDOUT.md + $HOLD/mm_cards/   holdout verdicts (final reporter ONLY)
#
# Guards (each exists because the alternative silently corrupts something):
#   · ids are forced to mmd_[a-z0-9_]{3,60} and must be new — the registry is the multiple-testing count; a re-used
#     id would let a researcher re-roll an idea until it passes. Re-test = a new id, and it counts again.
#   · "why" is mandatory. ≤ $MAX_IDEAS ideas per run (the rest are refused, propose again).
#   · one --asof per run (the last full UTC hour, ≥ 5 min old — mm_lab refuses a later one).
#   · ONE registry: mm_lab's pinned <repo>/data/mm_lab/registry.jsonl (compiled into the binary; there is no override,
#     and --holdout refuses any). Ops step, needs root, not done here: `sudo chattr +a <repo>/data/mm_lab/registry.jsonl`.
#   · the holdout look happens ONLY through `mm_lab run --holdout`: an in-sample PASS of the same spec, one look per id,
#     the look logged and counted in the registry; its cards never go to $RDATA.
#   · one bad proposal file never aborts the cycle: it is marked rejected with the reason and the others go on.
#   · runs a COPY of the binary (a rebuild never breaks a run), under a 4 GB memory scope when systemd-run exists.
#   · reads archives and params only; touches no service, database, config or executor.
set -euo pipefail
REAL="${RITHMIC_REPO:-$HOME/Desktop/rithmic_engine}"
DESK="${DESK_DIR:-$HOME/starnet-work/desk}"
HOLD="${DESK_HOLDOUT_DIR:-$HOME/starnet-work/holdout}"
RDATA="${RESEARCH_DATA:-$HOME/starnet-work/rithmic_engine/research_data}/mm_lab"
STATE="${DESK_MM_STATE:-$HOME/.local/share/rithmic-desk/mm}"
SRC_BIN="${MM_LAB_SRC_BIN:-$REAL/build/mm_lab}"
REGISTRY="$REAL/data/mm_lab/registry.jsonl"     # read here for ids/registry.md; mm_lab writes its own pinned copy
ROOT="${MM_LAB_ROOT:-/media/theone/381C48C51C48803C/rithmic_dex}"
PARAMS_DIR="${MM_LAB_PARAMS_DIR:-$REAL/data/mm_gates}"
MAX_IDEAS="${DESK_MAX_IDEAS:-40}"
THREADS="${DESK_MM_THREADS:-3}"
mkdir -p "$STATE"/{bin,out_is,runs} "$DESK" "$HOLD/mm_cards" "$RDATA/cards" "$(dirname "$REGISTRY")"
log() { echo "[desk-mm] $(date -Is) $*"; }
unset MM_LAB_REGISTRY

exec 9>"$STATE/lock"
flock -n 9 || { log "another run is in progress — skipping"; exit 0; }

[[ -x "$SRC_BIN" ]] || SRC_BIN="$REAL/build-judge/mm_lab"
[[ -x "$SRC_BIN" ]] || { log "no mm_lab binary"; exit 1; }
cmp -s "$SRC_BIN" "$STATE/bin/mm_lab" || { cp "$SRC_BIN" "$STATE/bin/mm_lab.tmp"; mv -f "$STATE/bin/mm_lab.tmp" "$STATE/bin/mm_lab"; }
BIN="$STATE/bin/mm_lab"
touch "$REGISTRY" "$STATE/processed"
SCOPE=()
if [[ "${DESK_MM_SCOPE:-1}" == 1 ]] && command -v systemd-run >/dev/null 2>&1; then SCOPE=(systemd-run --user --scope -q -p MemoryMax=4G); fi
mmlab() { nice -n 10 "${SCOPE[@]}" "$BIN" "$@" --root "$ROOT" --params-dir "$PARAMS_DIR" --threads "$THREADS"; }

publish() {
  { sed -n '/^\/\/ ── Idea JSON/,/^#include/p' "$REAL/src/dex/mm_lab.hpp" | grep -v '^#include' | sed 's#^// \{0,1\}##'
    echo
    echo "## Feature library"
    echo
    "$BIN" features
    cat <<EOF

---
# DESK RULES (StarNet market-making desk, read before proposing)
- Your verdicts are IN-SAMPLE only. Each venue's holdout boundary is frozen in data/mm_lab/holdout_boundaries.json the
  first time the venue is judged; everything after it is a sealed holdout that only the final reporter sees.
  \`registry.md\` and \`cards/\` never contain holdout numbers.
- The judge is fixed: NoCross fills, pessimistic queue, the venue's real maker fee, Lighter Standard latency
  (place 145 / cancel 445 ms) AND its 60 requests/min limit, flat-to-flat UTC days, baseline = the frozen setting at
  YOUR size on the same coins and window. An idea changes what the strategy does, never how it is judged.
- Ids are renamed to \`mmd_<your id>\` and must be new. Every idea ever judged (and every holdout look) raises the bar:
  the k-th registry entry needs t ≥ z(0.025 / (k(k+1))).
- Each idea is also run at ±1 step of every number it sets; ONE neighbour with net ≤ 0, or losing to its own
  baseline, fails it. You only see how many failed. Fewer, sharper knobs.
- Output: ONE fenced \`\`\`json block \`{"ideas":[ … ]}\`, ≤ 10 ideas per file, each with a real "why".
EOF
  } > "$RDATA/README.md"
  { echo "# mm desk registry — every market-making idea judged (in-sample)"
    echo
    echo "Registry entries so far: **$(jq -s '[.[] | select(.kind == "in_sample" or .kind == "holdout_look")] | length' "$REGISTRY")** (ideas + holdout looks; each one raises the bar for the next)."
    echo
    echo "| tested | id | venue | verdict | net bps | Δ vs same-size baseline bps | fills | P&L \$ | t / t needed | k |"
    echo "|---|---|---|---|---|---|---|---|---|---|"
    jq -r 'select(.kind == "in_sample") | [(.ts // "" | .[0:16]), .id, .venue, .verdict, (.summary.net_bps|.*100|round/100), (.summary.delta_bps|.*100|round/100),
           .summary.fills, (.summary.pnl_usd|.*100|round/100), "\(.summary.t|.*100|round/100) / \(.t_needed|.*100|round/100)", .k] | @tsv' "$REGISTRY" \
      | sort -r | awk -F'\t' '{printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n",$1,$2,$3,$4,$5,$6,$7,$8,$9,$10}'
  } > "$RDATA/registry.md"
}

shopt -s nullglob
all=("$DESK"/*-MMRESEARCH*.md)
shopt -u nullglob
srcs=()
for f in "${all[@]}"; do grep -qxF "$f:$(stat -c %Y "$f")" "$STATE/processed" || srcs+=("$f"); done
if (( ${#srcs[@]} == 0 )); then publish; log "no new MMRESEARCH file"; exit 0; fi

stamp=$(date -u +%F-%H%M)
work="$STATE/runs/$stamp"; mkdir -p "$work"
report="$DESK/$stamp-MMLAB.md"
asof=$(date -u -d "@$(( ( $(date -u +%s) - 300 ) / 3600 * 3600 ))" +%Y-%m-%dT%H:%M)
: > "$work/rejected.md"; : > "$work/accepted.tsv"; echo '[]' > "$work/ideas.jsonarr"
jq -r 'select(.kind == "in_sample") | .id' "$REGISTRY" 2>/dev/null | sort -u > "$work/known_ids" || : > "$work/known_ids"
n_acc=0

# One proposal file. Any failure returns non-zero with the reason on stdout; the caller marks the file rejected and
# carries on (the ideas it accepted before failing are dropped with it: a half-read file is not judged).
take_file() {
  local f="$1" b idea raw id one
  b=$(basename "$f")
  one="$work/one.json"
  awk '/^```json[[:space:]]*$/{f=1;next} /^```[[:space:]]*$/{if(f){exit}} f' "$f" > "$one" || { echo "cannot read the file"; return 1; }
  jq -e '.ideas | type == "array"' "$one" >/dev/null 2>&1 || { echo "no fenced \`\`\`json block with an \`ideas\` array"; return 1; }
  n_acc=$(wc -l < "$work/accepted.tsv")   # runs in a subshell: state lives in the files
  while IFS= read -r idea; do
    raw=$(jq -r '(.id // "") | tostring' <<<"$idea") || { echo "unreadable idea"; return 1; }
    id="mmd_${raw#mmd_}"
    if ! [[ "$id" =~ ^mmd_[a-z0-9_]{3,60}$ ]]; then echo "- \`$b\` \`$raw\`: id must be [a-z0-9_]{3,60}" >> "$work/rejected.md"; continue; fi
    if grep -qxF "$id" "$work/known_ids" || cut -f1 "$work/accepted.tsv" | grep -qxF "$id"; then
      echo "- \`$b\` \`$id\`: id already tested — a re-test needs a new id" >> "$work/rejected.md"; continue; fi
    if [[ -z "$(jq -r '(.why // "") | tostring' <<<"$idea" | tr -d '[:space:]')" ]]; then
      echo "- \`$b\` \`$id\`: no \"why\" (who pays us, and why they keep paying)" >> "$work/rejected.md"; continue; fi
    if (( n_acc >= MAX_IDEAS )); then echo "- \`$b\` \`$id\`: over the $MAX_IDEAS-idea cap this run — propose again" >> "$work/rejected.md"; continue; fi
    jq --argjson i "$(jq -c --arg id "$id" '.id = $id' <<<"$idea")" '. + [$i]' "$work/ideas.jsonarr" > "$work/ideas.tmp" \
      && mv "$work/ideas.tmp" "$work/ideas.jsonarr" || { echo "could not add idea $id"; return 1; }
    printf '%s\t%s\n' "$id" "$b" >> "$work/accepted.tsv"; n_acc=$((n_acc + 1))
  done < <(jq -c '.ideas[] | select(type == "object")' "$one" 2>/dev/null) || true
  return 0
}
for f in "${srcs[@]}"; do
  b=$(basename "$f")
  acc_save=$(wc -l < "$work/accepted.tsv"); cp "$work/ideas.jsonarr" "$work/arr.save"
  if ! why=$(take_file "$f" 2>&1); then
    echo "- \`$b\`: rejected — ${why:-unknown error}" >> "$work/rejected.md"
    head -n "$acc_save" "$work/accepted.tsv" > "$work/acc.tmp" && mv "$work/acc.tmp" "$work/accepted.tsv"
    cp "$work/arr.save" "$work/ideas.jsonarr"
  fi
done
jq '{ideas: .}' "$work/ideas.jsonarr" > "$work/ideas.json"
n_acc=$(wc -l < "$work/accepted.tsv")

: > "$work/run.log"
if (( n_acc )); then
  # mm_lab validates every idea itself; the ones it refuses are listed in the summary with the reason
  ( exec 8>"$REGISTRY.lock"; flock 8
    mmlab run "$work/ideas.json" --out "$STATE/out_is" --asof "$asof" --summary "$work/summary.md" ) > "$work/run.log" 2>&1 \
    || log "mm_lab exited non-zero — see $work/run.log"
  while IFS=$'\t' read -r id _; do
    for ext in md json; do [[ -s "$STATE/out_is/cards/$id.$ext" ]] && cp -f "$STATE/out_is/cards/$id.$ext" "$RDATA/cards/"; done
  done < "$work/accepted.tsv"
fi

# ---- holdout: one look for every in-sample PASS of this run ----
: > "$work/holdout.md"
if [[ -s "$work/summary.json" ]]; then
  jq '[.ideas[] | select(.verdict == "PASS") | .id]' "$work/summary.json" > "$work/pass_ids.json"
  if (( $(jq length "$work/pass_ids.json") > 0 )); then
    jq --slurpfile p "$work/pass_ids.json" '{ideas: [.ideas[] | select(.id as $i | $p[0] | index($i))]}' "$work/ideas.json" > "$work/holdout_ideas.json"
    ( exec 8>"$REGISTRY.lock"; flock 8
      mmlab run "$work/holdout_ideas.json" --holdout --out "$HOLD/mm_cards" --asof "$asof" --summary "$work/holdout_summary.md" ) > "$work/holdout.log" 2>&1 \
      || log "holdout run exited non-zero — see $work/holdout.log"
    [[ -s "$work/holdout_summary.md" ]] && cp -f "$work/holdout_summary.md" "$work/holdout.md"
  fi
fi
if [[ -s "$work/holdout.md" ]]; then
  { echo "# MM desk holdout $stamp (frozen boundary → asof $asof)"
    echo
    echo "Final reporter only. PASS = net bps > 0, beats the same-size frozen baseline by ≥ 0.5 bps, ≥ 30 fills, ≥ 55 % of"
    echo "coins and of days/blocks positive — on data no researcher has seen. Cards: \`mm_cards/<id>.holdout.md\`."
    echo
    cat "$work/holdout.md"; } > "$HOLD/$stamp-MM-HOLDOUT.md"
fi

for f in "${srcs[@]}"; do echo "$f:$(stat -c %Y "$f")" >> "$STATE/processed"; done
publish
{ echo "# MM desk run $stamp"
  echo
  echo "Judged: $(for f in "${srcs[@]}"; do printf '`%s` ' "$(basename "$f")"; done)"
  echo "In-sample only (asof $asof; each venue's frozen holdout sealed). Registry entries so far: $(jq -s '[.[] | select(.kind == "in_sample" or .kind == "holdout_look")] | length' "$REGISTRY")."
  echo
  if [[ -s "$work/summary.md" ]]; then sed '1,2d' "$work/summary.md"; else echo "_no idea reached the judge_"; fi
  [[ -s "$work/rejected.md" ]] && { echo; echo "## Rejected before judging"; echo; cat "$work/rejected.md"; }
  echo
  echo "Cards: \`research_data/mm_lab/cards/<id>.md\`. Rules + feature list: \`research_data/mm_lab/README.md\`."
} > "$report"
log "judged $n_acc idea(s) from ${#srcs[@]} file(s) → $report"
