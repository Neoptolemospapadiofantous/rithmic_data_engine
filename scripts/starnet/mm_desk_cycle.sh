#!/usr/bin/env bash
# mm_desk_cycle.sh — the MARKET-MAKING desk cycle (2026-10-26, founder: "set up starnet … to optimize as much as
# possible"). Deterministic, no LLM: refresh the read-only research clone, then judge every new researcher proposal
# with desk_mm.sh (mm_lab, honest fills, sealed holdout). Timer: mm-desk-cycle.timer every 4 h at :05, before the
# STEER/SKEPTIC/researcher jobs of that cycle. The NQ desk_cycle.sh / rithmic-research-sync.timer stay off.
set -uo pipefail
REAL="${RITHMIC_REPO:-$HOME/Desktop/rithmic_engine}"
cd "$REAL" || exit 1
if [ -f "$HOME/.local/share/rithmic-desk/PAUSED" ]; then echo "[mm-desk] $(date -Is) PAUSED — skipping"; exit 0; fi
"$REAL/scripts/starnet/sync_research_clone.sh" || echo "[mm-desk] $(date -Is) clone sync failed — judging anyway"
"$REAL/scripts/starnet/desk_mm.sh"
