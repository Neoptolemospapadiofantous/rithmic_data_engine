#!/usr/bin/env bash
# refresh_bars.sh — roll the collector's ticks up into bars_1m (scripts/sql/bars_1m.sql).
# Run every minute by bars-1m.timer; idempotent and incremental (~35 ms once backfilled).
# Also applies the SQL file first so a schema/function change ships by itself.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
for f in bars_1m book_1m session_stats bars_views; do
  "${PSQL[@]}" -f "scripts/sql/$f.sql" >/dev/null 2>&1 || true   # NOTICEs on re-apply are fine
done
n=$("${PSQL[@]}" -c "SELECT refresh_bars_1m();") || { echo "$(date -Is) refresh_bars_1m FAILED" >&2; exit 1; }
# 2026-09-26: the same tick also feeds book_1m (quotes per minute, per symbol) and
# session_stats (one row per NY session, last 3 recomputed). Failures here are logged, never
# fatal — bars_1m is what the chart needs.
b=0; for s in NQ ES; do
  k=$("${PSQL[@]}" -c "SELECT refresh_book_1m('$s');" 2>/dev/null) || { echo "$(date -Is) refresh_book_1m($s) FAILED" >&2; k=0; }
  b=$(( b + k ))
done
d=$("${PSQL[@]}" -c "SELECT refresh_session_stats(3);" 2>/dev/null) || { echo "$(date -Is) refresh_session_stats FAILED" >&2; d=0; }
[[ "${1:-}" == "-v" ]] && echo "$(date -Is) bars_1m +$n · book_1m +$b · session_stats $d rows"
exit 0
