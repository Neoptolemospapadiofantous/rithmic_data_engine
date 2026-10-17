#!/usr/bin/env bash
# desk_edge.sh — the StarNet desk's judge (since 2026-10-16, founder: "launch starnet to find the best edge … a
# matrix of all the families as well as order flow inside the 5m candle"). Deterministic, no LLM.
#
# The researchers no longer propose paper-fleet variants (the desk only ever saw 3–9 sessions); they propose
# edge_lab IDEAS (research/README.md) and this script puts every one through the same five gates over 2010-07 →
# 2024-12, then gives the passes one look at the SEALED holdout (2025-01 → today), which no agent in the loop sees.
#
# Input : every ~/starnet-work/desk/*-RESEARCHER*.md not judged yet. Each carries ONE fenced ```json block
#         {"ideas":[{ edge_lab idea spec }, …]}.
# Output: ~/starnet-work/desk/<stamp>-EDGELAB.md                  accepted / rejected + why, verdicts (in-sample)
#         ~/starnet-work/rithmic_engine/research_data/edge_lab/    README, example ideas, every desk report, registry
#         ~/starnet-work/holdout/<stamp>-EDGE-HOLDOUT.md           holdout verdicts (BRIEF only) + meta/exits reports
#
# Guards (each exists because the alternative silently corrupts something):
#   · ids are forced to desk_[a-z0-9_]{3,60} and must be new — the registry is the multiple-testing count, and a
#     re-used id would let a researcher re-roll an idea until it passes. Re-test = a new id, and it counts again.
#   · "why" is mandatory (edge_lab refuses without it anyway; refused here so the reason reaches the researcher).
#   · the sealed data stops 2024-12-31 17:00 ET. Moving HOLDOUT_FROM re-exposes held-out days: don't.
#   · the edge_lab binary and the full-history cache belong to another session's work; this script runs a COPY of
#     the binary and hard-links the cache CSVs into its own dirs, so a rebuild or re-export there never breaks a run
#     here and nothing here writes into data/edge_lab/.
#   · nothing touches the paper fleet, Postgres, a config file or an executor.
set -euo pipefail
REAL="${RITHMIC_REPO:-$HOME/Desktop/rithmic_engine}"
DESK="${DESK_DIR:-$HOME/starnet-work/desk}"
HOLD="${DESK_HOLDOUT_DIR:-$HOME/starnet-work/holdout}"
RDATA="${RESEARCH_DATA:-$HOME/starnet-work/rithmic_engine/research_data}/edge_lab"
STATE="${DESK_EDGE_STATE:-$HOME/.local/share/rithmic-desk/edge}"
SRC_BIN="${EDGE_LAB_SRC_BIN:-$REAL/build-exp/edge_lab}"
SRC_CACHE="$REAL/data/edge_lab/cache"
HOLDOUT_FROM="2025-01-01"
CUT_T=1735664400            # 2024-12-31 17:00 as edge_lab's ET-wall-clock epoch (end of the last sealed trading day)
MAX_IDEAS="${DESK_MAX_IDEAS:-40}"
THREADS="${DESK_EDGE_THREADS:-6}"
mkdir -p "$STATE"/{bin,sealed,full,ideas,out_is,out_ho} "$DESK" "$HOLD" "$RDATA"/{reports,ideas}
log() { echo "[desk-edge] $(date -Is) $*"; }

exec 9>"$STATE/lock"
flock -n 9 || { log "another run is in progress — skipping"; exit 0; }

# ---- inputs: binary copy, full cache (hard links), sealed cache (cut at the holdout) ----
[[ -x "$SRC_BIN" ]] || SRC_BIN="$REAL/build/edge_lab"
[[ -x "$SRC_BIN" ]] || { log "no edge_lab binary"; exit 1; }
cmp -s "$SRC_BIN" "$STATE/bin/edge_lab" || { cp "$SRC_BIN" "$STATE/bin/edge_lab.tmp"; mv -f "$STATE/bin/edge_lab.tmp" "$STATE/bin/edge_lab"; }
BIN="$STATE/bin/edge_lab"
for s in NQ ES; do [[ -s "$SRC_CACHE/$s.csv" ]] || { log "no $SRC_CACHE/$s.csv — run scripts/edge_lab.sh export"; exit 1; }; done
# every cache CSV (bars, and NQ.book.csv order-book minutes since 2026-10-16): hard-link the full copy, cut the sealed
# copy at the holdout — both redone whenever the source file is replaced (a re-export is a new inode)
for src in "$SRC_CACHE"/*.csv; do
  f=$(basename "$src"); ino=$(stat -c %i "$src")
  [[ "$ino" == "$(stat -c %i "$STATE/full/$f" 2>/dev/null)" ]] || { ln -f "$src" "$STATE/full/$f"; rm -f "$STATE/full/${f%.csv}.bin"; }
  if [[ "$ino" != "$(cat "$STATE/sealed/$f.ino" 2>/dev/null)" || ! -s "$STATE/sealed/$f" ]]; then
    awk -F, -v c="$CUT_T" '$1 < c' "$src" > "$STATE/sealed/$f.tmp"
    mv -f "$STATE/sealed/$f.tmp" "$STATE/sealed/$f"; rm -f "$STATE/sealed/${f%.csv}.bin"; echo "$ino" > "$STATE/sealed/$f.ino"
  fi
done
cp -f "$SRC_CACHE/fomc.txt" "$STATE/full/fomc.txt"; cp -f "$SRC_CACHE/fomc.txt" "$STATE/sealed/fomc.txt"
touch "$STATE/registry.tsv" "$STATE/processed"

# ---- research_data/edge_lab: what the researchers read (refreshed every run, even with no new idea) ----
publish() {
  { cat "$REAL/research/README.md"
    cat <<EOF

---
# DESK RULES (StarNet edge desk, read before proposing)
- Your data stops **2024-12-31**. Everything from $HOLDOUT_FROM on is a sealed holdout that only the BRIEF sees.
  The verdicts in \`registry.md\` and \`reports/\` are computed on 2010-07 → 2024-12 only.
- Order flow (\`cdelta\`, \`dsum(tf, n)\`, \`flow\`) exists only from **2023-03** — set \`"from": "2023-03-01"\` on an
  order-flow idea, and \`flow == 1\` in its \`when\`. That leaves ~22 months in-sample, ~10 of them walk-forward.
  \`dsum(5, 1)\` = aggressor delta of the last completed 5-minute bar, \`vsum(5, 1)\` its volume, \`dsum(1, k)\` the last
  k one-minute deltas (inside the forming 5-minute bar when k < 5), \`lag(k, cdelta)\` RTH delta k minutes ago.
- Ids are renamed to \`desk_<your id>\` and must be new. Re-testing an idea = a new id, and it counts against the
  luck bar like any other. Keep grids small (≤ 30 variants); \`t_needed\` in each report is the bar.
- Ideas listed under "already tested before the desk" (the founder's 29) are known: do not re-propose them.
- Output: ONE fenced \`\`\`json block \`{"ideas":[ … ]}\`, ≤ 10 ideas per file, each with a real "why".
EOF
  } > "$RDATA/README.md"
  cp -f "$REAL"/research/ideas/*.json "$RDATA/ideas/" 2>/dev/null || true
  { echo "# edge desk registry — every idea the desk has tested (in-sample 2010-07 → 2024-12)"
    echo
    echo "Ideas tested so far: **$(wc -l < "$STATE/registry.tsv")** (each one raises the luck bar for the next)."
    echo
    echo "| tested | id | verdict | stage | variants | screen best pts | t / t needed | OOS n | OOS pts/trade | OOS PF | from |"
    echo "|---|---|---|---|---|---|---|---|---|---|---|"
    sort -r "$STATE/registry.tsv" | awk -F'\t' '{printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n",$1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11}'
    echo
    echo "## Already tested before the desk (founder's 29, on ALL data incl. the holdout — known, do not re-propose)"
    echo
    for f in "$REAL"/data/edge_lab/reports/*.json; do
      jq -r '"- \(.id): \(.verdict) (stage \(.stage_reached), OOS \(.oos.avg_pts // 0 | .*100|round/100) pts/trade, n \(.oos.n // 0))"' "$f" 2>/dev/null
    done
  } > "$RDATA/registry.md"
}

mapfile -t srcs < <(for f in $(ls -1 "$DESK"/*-RESEARCHER*.md 2>/dev/null | sort); do
  grep -qxF "$f:$(stat -c %Y "$f")" "$STATE/processed" || echo "$f"; done)
if (( ${#srcs[@]} == 0 )); then publish; log "no new RESEARCHER file"; exit 0; fi

stamp=$(date +%F-%H%M)
work="$STATE/runs/$stamp"; mkdir -p "$work/ideas"
report="$DESK/$stamp-EDGELAB.md"
: > "$work/rejected.md"; : > "$work/accepted.tsv"
n_acc=0
for f in "${srcs[@]}"; do
  b=$(basename "$f")
  awk '/^```json[[:space:]]*$/{f=1;next} /^```[[:space:]]*$/{if(f){exit}} f' "$f" > "$work/one.json"
  if ! jq -e '.ideas | type == "array"' "$work/one.json" >/dev/null 2>&1; then
    echo "- \`$b\`: no fenced \`\`\`json block with an \`ideas\` array" >> "$work/rejected.md"; continue
  fi
  while IFS= read -r idea; do
    raw=$(jq -r '.id // ""' <<<"$idea"); id="desk_${raw#desk_}"
    if ! [[ "$id" =~ ^desk_[a-z0-9_]{3,60}$ ]]; then echo "- \`$b\` \`$raw\`: id must be [a-z0-9_]{3,60}" >> "$work/rejected.md"; continue; fi
    if cut -f2 "$STATE/registry.tsv" | grep -qxF "$id" || cut -f1 "$work/accepted.tsv" | grep -qxF "$id"; then
      echo "- \`$b\` \`$id\`: id already tested — a re-test needs a new id" >> "$work/rejected.md"; continue; fi
    if [[ -z "$(jq -r '.why // ""' <<<"$idea" | tr -d '[:space:]')" ]]; then
      echo "- \`$b\` \`$id\`: no \"why\" (who is forced to trade against us)" >> "$work/rejected.md"; continue; fi
    if (( n_acc >= MAX_IDEAS )); then echo "- \`$b\` \`$id\`: over the $MAX_IDEAS-idea cap this run — propose again" >> "$work/rejected.md"; continue; fi
    jq --arg id "$id" '.id = $id' <<<"$idea" > "$work/ideas/$id.json"
    printf '%s\t%s\n' "$id" "$b" >> "$work/accepted.tsv"; n_acc=$((n_acc + 1))
  done < <(jq -c '.ideas[]' "$work/one.json")
done

cd "$REAL"   # edge_lab reads data/regime (VIX) relative to the repo
: > "$work/verdicts.tsv"; : > "$work/errors.md"
if (( n_acc )); then
  # one idea per call: a spec edge_lab rejects (bad expression) then fails alone, with its own message
  while IFS=$'\t' read -r id src; do
    if nice -n 10 "$BIN" run "$work/ideas/$id.json" --data "$STATE/sealed" --out "$STATE/out_is" --threads "$THREADS" \
         > "$work/$id.log" 2>&1 && [[ -s "$STATE/out_is/reports/$id.json" ]]; then
      cp -f "$STATE/out_is/reports/$id.md" "$RDATA/reports/$id.md"
      jq -r --arg d "$(date +%F)" --arg src "$src" '[$d, .id, .verdict, .stage_reached, .screen.variants,
          (.screen.best_avg // 0 | .*100|round/100), "\(.screen.best_t // 0 | .*100|round/100) / \(.screen.t_needed // 0 | .*100|round/100)",
          (.oos.n // 0), (.oos.avg_pts // 0 | .*100|round/100), (.oos.pf // 0 | .*100|round/100), $src] | @tsv' \
          "$STATE/out_is/reports/$id.json" | tee -a "$STATE/registry.tsv" >> "$work/verdicts.tsv"
    else
      printf -- '- `%s` (%s): edge_lab refused it — %s\n' "$id" "$src" "$(grep -m1 -iE 'error|expr|unknown|needs|refus' "$work/$id.log" | cut -c1-240)" >> "$work/errors.md"
      printf '%s\t%s\t%s\t-\t-\t-\t-\t-\t-\t-\t%s\n' "$(date +%F)" "$id" "ERROR" "$src" >> "$STATE/registry.tsv"
    fi
  done < "$work/accepted.tsv"
fi

# ---- holdout: one look for every in-sample PASS, on the full history; then meta + exits on a holdout PASS ----
: > "$work/holdout.md"
while IFS=$'\t' read -r _ id verdict _; do
  [[ "$verdict" == "PASS" ]] || continue
  if nice -n 10 "$BIN" run "$work/ideas/$id.json" --data "$STATE/full" --out "$STATE/out_ho" --threads "$THREADS" > "$work/$id.ho.log" 2>&1; then
    cp -f "$STATE/out_ho/reports/$id.md" "$HOLD/$id.full.md"
    read -r hn hnet < <(awk -F'|' -v y="${HOLDOUT_FROM:0:4}" '/^\| year \|/{t=1;next} t&&/^\|[ ]*[0-9]{4} /{yr=$2+0; if(yr>=y){n+=$3; v=$4; gsub(/[+ ]/,"",v); s+=v}} t&&!/^\|/{t=0} END{printf "%d %s\n", n, s+0}' "$STATE/out_ho/reports/$id.md")
    hv=FAIL; (( hn >= 30 )) && awk -v s="$hnet" 'BEGIN{exit !(s > 0)}' && hv=PASS
    printf '| %s | %s | %s | %s |\n' "$id" "$hn" "$hnet" "$hv" >> "$work/holdout.md"
    if [[ "$hv" == PASS ]]; then
      nice -n 10 "$BIN" meta  "$work/ideas/$id.json" --data "$STATE/full" --out "$STATE/out_ho" --threads "$THREADS" >> "$work/$id.ho.log" 2>&1 \
        && cp -f "$STATE/out_ho/reports/${id}__meta.md" "$HOLD/" 2>/dev/null || true
      nice -n 10 "$BIN" exits "$work/ideas/$id.json" --data "$STATE/full" --out "$STATE/out_ho" --threads "$THREADS" >> "$work/$id.ho.log" 2>&1 \
        && cp -f "$STATE/out_ho/reports/${id}__exits.md" "$HOLD/" 2>/dev/null || true
      cp -f "$work/ideas/$id.json" "$HOLD/$id.json"
    fi
  else
    printf '| %s | - | - | ERROR (%s) |\n' "$id" "$(tail -1 "$work/$id.ho.log" | cut -c1-120)" >> "$work/holdout.md"
  fi
done < "$work/verdicts.tsv"
if [[ -s "$work/holdout.md" ]]; then
  { echo "# Edge desk holdout $stamp ($HOLDOUT_FROM → today; walk-forward trades of the full-history run)"
    echo
    echo "PASS = ≥ 30 holdout trades and net > 0 at real cost. Full reports: \`<id>.full.md\`; meta/exits on a PASS."
    echo
    echo "| idea | holdout trades | holdout net pts | verdict |"; echo "|---|---|---|---|"
    cat "$work/holdout.md"; } > "$HOLD/$stamp-EDGE-HOLDOUT.md"
fi

for f in "${srcs[@]}"; do echo "$f:$(stat -c %Y "$f")" >> "$STATE/processed"; done
publish
{ echo "# Edge desk run $stamp"
  echo
  echo "Judged: $(for f in "${srcs[@]}"; do printf '`%s` ' "$(basename "$f")"; done)"
  echo "Data: 2010-07 → 2024-12 only (holdout sealed). Ideas tested by the desk so far: $(wc -l < "$STATE/registry.tsv")."
  echo
  echo "## Verdicts (in-sample)"; echo
  echo "| id | verdict | stage | variants | screen best pts | t / t needed | OOS n | OOS pts/trade | OOS PF | from |"
  echo "|---|---|---|---|---|---|---|---|---|---|"
  awk -F'\t' '{printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n",$2,$3,$4,$5,$6,$7,$8,$9,$10,$11}' "$work/verdicts.tsv"
  [[ -s "$work/errors.md" ]] && { echo; echo "## Refused by edge_lab (fix the spec, re-propose under a new id)"; echo; cat "$work/errors.md"; }
  [[ -s "$work/rejected.md" ]] && { echo; echo "## Rejected before testing"; echo; cat "$work/rejected.md"; }
  echo
  echo "Stage reached: 0 why · 1 screen · 2 walk-forward · 3 controls · 4 robustness · 5 prop (5 = PASS)."
  echo "Full reports: \`research_data/edge_lab/reports/<id>.md\` in the clone."
} > "$report"
log "judged $n_acc idea(s) from ${#srcs[@]} file(s) → $report"
