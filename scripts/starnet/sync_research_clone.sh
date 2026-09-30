#!/usr/bin/env bash
# sync_research_clone.sh — refresh the StarNet crew's READ-ONLY research clone
# (~/starnet-work/rithmic_engine) to the founder's current branch HEAD.
#
# Only COMMITTED work reaches the clone (uncommitted edits in the real checkout are
# invisible to the crew by design — the clone is fetched, never copied). Tracked-file
# edits made in the clone are discarded; untracked research_data/ is kept.
# Push stays disabled (origin pushurl = DISABLED-read-only-research-copy).
set -euo pipefail
REAL="${RITHMIC_REPO:-$HOME/Desktop/rithmic_engine}"
CLONE="${RESEARCH_CLONE:-$HOME/starnet-work/rithmic_engine}"

[[ -d "$CLONE/.git" ]] || { echo "[sync] no research clone at $CLONE" >&2; exit 1; }
branch=$(git -C "$REAL" rev-parse --abbrev-ref HEAD)
git -C "$CLONE" fetch -q --prune origin
git -C "$CLONE" checkout -q -f -B research "origin/$branch"
git -C "$CLONE" reset -q --hard "origin/$branch"
# never let a sync re-enable pushing
git -C "$CLONE" remote set-url --push origin DISABLED-read-only-research-copy
echo "[sync] $(date -Is) research clone -> $branch @ $(git -C "$CLONE" rev-parse --short HEAD)"

# fresh read-only results for the crew (research_data/ is untracked, so the reset above keeps it)
"$REAL/scripts/starnet/export_research_data.sh"
