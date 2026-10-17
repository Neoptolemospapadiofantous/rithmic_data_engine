#!/usr/bin/env bash
# desk_cycle.sh — the deterministic half of one StarNet desk cycle (every 4 h, rithmic-research-sync.timer):
# judge whatever the researchers proposed last cycle through edge_lab (desk_edge.sh, since 2026-10-16 — the
# paper-variant replay desk_replay.sh is kept but no longer called), then refresh the research clone and re-export.
# A failed judge run never blocks the export.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
"$DIR/desk_edge.sh" || echo "[desk-cycle] $(date -Is) WARN desk_edge failed (export continues)"
"$DIR/sync_research_clone.sh"; rc=$?
# the clone reset keeps untracked research_data/, but re-publish edge_lab/ in case the export rebuilt it
"$DIR/desk_edge.sh" >/dev/null 2>&1 || true
exit $rc
