#!/usr/bin/env bash
# local_backup.sh — nightly copy to the machine's SECOND drive (the Windows NVMe, a separate physical disk), founder 2026-10-17.
# Protects against the main drive failing (not against losing the machine). Fail-open: a missing drive alerts and exits 1.
#   pg/    the nightly Postgres dumps (scripts/pg_backup.sh), newest 14 kept there; on the MAIN disk only the newest
#          LOCAL_KEEP (3) stay once their copy is verified by size — the main disk is ~98 % full and each dump is ~2.8 GB
#   git/   a bundle of every branch of the engine and dashboard repos (git clone <bundle> restores them)
#   raw/   the owned market data (Databento MBO / OHLCV / trades, ~180 GB) — rsync, so only new files copy after the first run
# Logs to data/logs/local_backup.log. Runs at idle IO priority.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
DEST="${LOCAL_BACKUP_DIR:-/media/theone/381C48C51C48803C/rithmic_backup}"
BOT="${BOT_REPO:-/home/theone/Desktop/bot}"; LOCAL_KEEP="${LOCAL_KEEP:-3}"
notify() { command -v grid-notify >/dev/null && grid-notify "$1" >/dev/null 2>&1 || true; }
log() { echo "$(date -Is) $*"; }
mnt="$(dirname "$DEST")"
if ! mountpoint -q "$mnt" || ! mkdir -p "$DEST/pg" "$DEST/git" "$DEST/raw" 2>/dev/null; then
  log "SKIPPED — second drive not mounted at $mnt"; notify "🟡 rithmic local_backup: second drive not mounted ($mnt) — nightly copy skipped"; exit 1
fi
fail=0
# 1. Postgres dumps
for f in $(ls -1t data/backups/pg/rithmic_*.dump 2>/dev/null); do
  b=$(basename "$f"); [[ -f "$DEST/pg/$b" && $(stat -c %s "$DEST/pg/$b") -eq $(stat -c %s "$f") ]] && continue
  if ionice -c3 nice -n 15 cp "$f" "$DEST/pg/$b.part" && mv "$DEST/pg/$b.part" "$DEST/pg/$b"; then log "copied $b"; else log "FAILED copy $b"; fail=1; fi
done
ls -1t "$DEST"/pg/rithmic_*.dump 2>/dev/null | tail -n +15 | xargs -r rm -f
ls -1t data/backups/pg/rithmic_*.dump 2>/dev/null | tail -n +$((LOCAL_KEEP + 1)) | while read -r f; do
  b=$(basename "$f"); if [[ -f "$DEST/pg/$b" && $(stat -c %s "$DEST/pg/$b") -eq $(stat -c %s "$f") ]]; then rm -f "$f" && log "main disk: removed $b (copy verified on the second drive)"; fi
done
# 2. git bundles (every branch, full history)
for r in "$REPO" "$BOT"; do n=$(basename "$r")
  if git -C "$r" bundle create "$DEST/git/$n.bundle.part" --all >/dev/null 2>&1 && mv "$DEST/git/$n.bundle.part" "$DEST/git/$n.bundle"; then log "git bundle $n ($(du -h "$DEST/git/$n.bundle" | cut -f1))"; else log "FAILED git bundle $n"; fail=1; fi
done
# 3. owned market data
if ionice -c3 nice -n 15 rsync -a --size-only "$BOT/data/raw/" "$DEST/raw/"; then log "raw data synced ($(du -sh "$DEST/raw" | cut -f1))"; else log "FAILED raw rsync"; fail=1; fi
[[ $fail -eq 0 ]] || notify "🔴 rithmic local_backup: a step failed — see data/logs/local_backup.log"
log "done (second drive: $(df -h --output=avail "$mnt" | tail -1 | tr -d ' ') free; main disk: $(df -h --output=avail / | tail -1 | tr -d ' ') free)"
exit $fail
