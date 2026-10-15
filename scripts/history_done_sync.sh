#!/usr/bin/env bash
# history_done_sync.sh — mirror the history queue's done markers into Postgres history_done (see
# scripts/sql/history_done.sql), so reports read only finished months through the hist_trades view.
# Run by history_report.sh and payout_sim.sh before they query; cheap, idempotent.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(env PGOPTIONS="-c client_min_messages=warning" psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)
"${PSQL[@]}" -f scripts/sql/history_done.sql >/dev/null || exit 1
"${PSQL[@]}" -c "BEGIN; TRUNCATE session_trend; INSERT INTO session_trend SELECT symbol, session_date, rth_close > avg(rth_close) OVER (PARTITION BY symbol ORDER BY session_date ROWS BETWEEN 49 PRECEDING AND CURRENT ROW) FROM session_stats WHERE rth_close IS NOT NULL; COMMIT;" >/dev/null || exit 1   # 50-session trend regime
rows=""
for f in data/history/done/*/*.done; do
  [[ -e "$f" ]] || continue
  market=$(basename "$(dirname "$f")"); month=$(basename "$f" .done)
  [[ "$market" =~ ^[a-z]+$ && "$month" =~ ^[0-9]{4}-[0-9]{2}$ ]] || continue
  read -r trades finished < <(jq -r '[(.trades // "null"), (.finished // "null")] | @tsv' "$f" 2>/dev/null) || continue
  [[ "$trades" =~ ^[0-9]+$ ]] || trades=NULL
  [[ "$finished" == null || -z "$finished" ]] && finished=NULL || finished="'$finished'"
  rows+="('$market','$month',$trades,$finished),"
done
[[ -n "$rows" ]] || { echo "[history_done_sync] no done markers"; exit 0; }
"${PSQL[@]}" -c "BEGIN; TRUNCATE history_done; INSERT INTO history_done VALUES ${rows%,}; COMMIT;" >/dev/null || exit 1
"${PSQL[@]}" -c "SELECT market || ' ' || count(*) FROM history_done GROUP BY market ORDER BY market" | tr '\n' ' ' | sed 's/^/[history_done_sync] /; s/$/\n/'
