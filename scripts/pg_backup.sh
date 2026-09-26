#!/usr/bin/env bash
# pg_backup.sh — nightly dump of the rithmic Postgres database (TODO.md §1, 2026-09-26).
#
# Until this existed the 03:00 bot/scripts/backup.sh covered the legacy SQLite bot.db, models,
# configs and logs — NOT Postgres. Every live and paper trade and all recorded ticks lived on
# the one root disk (93 % full). This writes a compressed custom-format dump
# (pg_restore-able, table-selectable) to data/backups/pg/, keeps the newest KEEP, and — once a
# week (Sunday) or with --offbox — copies the newest dump to the Oracle VM. The copy is
# best-effort: an unreachable Oracle never fails the local dump.
#
#   scripts/pg_backup.sh            dump + prune (+ off-box copy on Sundays)
#   scripts/pg_backup.sh --offbox   dump + prune + off-box copy now
#   scripts/pg_backup.sh --verify   also run pg_restore --list on the new dump (catalog check)
#
# Restore (whole DB into a scratch db):  createdb rithmic_restore && pg_restore -d rithmic_restore FILE
# Restore one table:                     pg_restore -d rithmic -t live_trades --data-only FILE
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
DEST="${PG_BACKUP_DIR:-$REPO/data/backups/pg}"; KEEP="${PG_BACKUP_KEEP:-14}"
ORACLE="${PG_BACKUP_ORACLE:-opc@170.9.233.177}"; ORACLE_DIR="${PG_BACKUP_ORACLE_DIR:-~/backups/rithmic}"
KEY="${PG_BACKUP_SSH_KEY:-$HOME/.ssh/id_ed25519}"
OFFBOX=0; VERIFY=0
for a in "$@"; do case "$a" in --offbox) OFFBOX=1;; --verify) VERIFY=1;; esac; done
[[ "$(date +%u)" == 7 ]] && OFFBOX=1   # Sunday: weekly off-box copy

mkdir -p "$DEST"
stamp=$(date +%Y%m%d_%H%M%S); out="$DEST/rithmic_${stamp}.dump"; t0=$(date +%s)
if ! pg_dump -h "${PG_HOST:-localhost}" -p "${PG_PORT:-5432}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" \
        --format=custom --compress=6 --no-owner --no-privileges --file="$out.partial"; then
  echo "$(date -Is) pg_dump FAILED" >&2; rm -f "$out.partial"
  command -v grid-notify >/dev/null && grid-notify "🔴 rithmic pg_backup FAILED on $(hostname) — see data/logs/pg_backup.log" >/dev/null 2>&1
  exit 1
fi
mv "$out.partial" "$out"
size=$(du -h "$out" | cut -f1); secs=$(( $(date +%s) - t0 ))
echo "$(date -Is) dumped $out ($size in ${secs}s)"

if [[ $VERIFY -eq 1 ]]; then
  if pg_restore --list "$out" >/dev/null 2>&1; then echo "$(date -Is) verify: catalog readable"
  else echo "$(date -Is) verify FAILED: pg_restore --list cannot read $out" >&2; exit 1; fi
fi

# prune: keep the newest KEEP dumps
ls -1t "$DEST"/rithmic_*.dump 2>/dev/null | tail -n +$((KEEP + 1)) | while read -r old; do rm -f "$old" && echo "$(date -Is) pruned $(basename "$old")"; done

if [[ $OFFBOX -eq 1 ]]; then
  if ssh -o BatchMode=yes -o ConnectTimeout=15 -i "$KEY" "$ORACLE" "mkdir -p $ORACLE_DIR" >/dev/null 2>&1 \
     && scp -o BatchMode=yes -o ConnectTimeout=15 -i "$KEY" -q "$out" "$ORACLE:$ORACLE_DIR/" ; then
    ssh -o BatchMode=yes -i "$KEY" "$ORACLE" "ls -1t $ORACLE_DIR/rithmic_*.dump | tail -n +5 | xargs -r rm -f" >/dev/null 2>&1
    echo "$(date -Is) off-box copy → $ORACLE:$ORACLE_DIR (keeps 4)"
  else
    echo "$(date -Is) off-box copy SKIPPED — $ORACLE unreachable (local dump is fine)" >&2
    command -v grid-notify >/dev/null && grid-notify "🟡 rithmic pg_backup: Oracle unreachable, weekly off-box copy skipped (local dump ok, $size)" >/dev/null 2>&1
  fi
fi
exit 0
