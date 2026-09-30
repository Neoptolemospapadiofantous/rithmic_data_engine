#!/usr/bin/env bash
# desk_cycle.sh — the deterministic half of one StarNet desk cycle (every 2 h, rithmic-research-sync.timer):
# replay whatever RESEARCHER proposed last cycle, then refresh the research clone and re-export, so the
# cycle's QUANT (:05) ranks the new variants alongside the fleet. A failed replay never blocks the export.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
"$DIR/desk_replay.sh" || echo "[desk-cycle] $(date -Is) WARN desk_replay failed (export continues)"
exec "$DIR/sync_research_clone.sh"
