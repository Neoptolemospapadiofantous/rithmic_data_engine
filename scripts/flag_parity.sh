#!/usr/bin/env bash
# Validate the dashboard /flags page against the research detector (2026-10-20).
#   1. research/programs/flag_parity.cpp exports N random days + the last 10 (bars, context, the research's flags)
#   2. bot/frontend/src/lib/flags.ts (the page's own detection) is compiled with tsc and run on the SAME bars
#   3. compare.js: every flag and field must match; repaint.js: minute-by-minute replay must never change a shown flag
# Usage: scripts/flag_parity.sh [N_DAYS=80] [REPLAY_DAYS=90]. Read-only; writes only to a temp dir. Exit 1 on any difference.
set -euo pipefail
cd "$(dirname "$0")/.."
N=${1:-80}; RD=${2:-90}; FE=/home/theone/Desktop/bot/frontend
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
g++ -O2 -std=c++20 -Isrc -Iresearch/programs research/programs/flag_parity.cpp -o "$T/flag_parity"
"$T/flag_parity" "$T/days.json" "$N"
(cd "$FE" && npx tsc --outDir "$T/js" --module commonjs --target es2020 --skipLibCheck --strict src/lib/flags.ts)
out=$(node "$FE/scripts/flag-parity/compare.js" "$T/js" "$T/days.json"); echo "$out"
rep=$(node "$FE/scripts/flag-parity/repaint.js" "$T/js" "$T/days.json" "$RD"); echo "$rep"
grep -q "only in research: 0 " <<<"$out" && grep -q "only on the page: 0 " <<<"$out" && grep -q "field differences on matched flags: none" <<<"$out" \
  && grep -q "0 failures" <<<"$(grep '^repaint' <<<"$rep")" && grep -q "invariants: .* 0 failures" <<<"$rep" && { echo "flag_parity: PASS"; exit 0; }
echo "flag_parity: FAIL"; exit 1
