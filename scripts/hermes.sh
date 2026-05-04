#!/usr/bin/env bash
# Hermes CI runner — C++ only.
# Usage: ./scripts/hermes.sh [--fast]
#   --fast : skip full test_db (DB I/O); still runs unit tests + audit_daemon
#
# Exit codes: 0 = PASS, 1 = FAIL
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/build"
DATA="$REPO/data"
FINDINGS="$DATA/hermes_findings.json"
LOG_DIR="$DATA/logs"
LOG="$LOG_DIR/hermes_session.log"
FAST=0

for arg in "$@"; do
  [[ "$arg" == "--fast" ]] && FAST=1
done

mkdir -p "$LOG_DIR"
TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

pass=0; fail=0; warn=0
declare -a findings=()

log() { echo "[hermes] $*" | tee -a "$LOG"; }
record() {
  local status=$1 check=$2 detail=$3
  findings+=("{\"check\":\"$check\",\"status\":\"$status\",\"detail\":\"$detail\",\"ts\":\"$TS\"}")
  case "$status" in
    PASS) pass=$((pass + 1)); log "PASS  $check" ;;
    FAIL) fail=$((fail + 1)); log "FAIL  $check — $detail" ;;
    WARN) warn=$((warn + 1)); log "WARN  $check — $detail" ;;
  esac
}

# ── 1. Build ───────────────────────────────────────────────────────────────────
log "=== BUILD ==="
if cmake --build "$BUILD" -j"$(nproc)" --target \
    rithmic_engine nq_executor audit_daemon \
    test_orb_strategy test_risk_manager test_validator test_order_manager test_db \
    > "$LOG_DIR/build.log" 2>&1; then
  record PASS build "cmake --build succeeded"
else
  record FAIL build "cmake --build failed — see data/logs/build.log"
fi

# ── 2. Unit tests (no DB, no network) ─────────────────────────────────────────
log "=== UNIT TESTS ==="
for bin in test_orb_strategy test_risk_manager test_validator test_order_manager; do
  target="$BUILD/$bin"
  if [[ ! -x "$target" ]]; then
    record FAIL "$bin" "binary not found at $target"
    continue
  fi
  out=$("$target" 2>&1) && rc=0 || rc=$?
  if [[ $rc -eq 0 ]]; then
    record PASS "$bin" "exit 0"
  else
    record FAIL "$bin" "exit $rc"
    echo "$out" >> "$LOG"
  fi
done

# ── 3. DB test (skipped in --fast mode) ───────────────────────────────────────
if [[ $FAST -eq 0 ]]; then
  log "=== DB TEST ==="
  if [[ -x "$BUILD/test_db" ]]; then
    out=$("$BUILD/test_db" 2>&1) && rc=0 || rc=$?
    if [[ $rc -eq 0 ]]; then
      record PASS test_db "exit 0"
    else
      record FAIL test_db "exit $rc"
      echo "$out" >> "$LOG"
    fi
  else
    record FAIL test_db "binary not found"
  fi
fi

# ── 4. Audit daemon (local/testing only — not production) ─────────────────────
log "=== AUDIT DAEMON ==="
if [[ -x "$BUILD/audit_daemon" ]]; then
  out=$("$BUILD/audit_daemon" --once 2>&1) && rc=0 || rc=$?
  if [[ $rc -eq 0 ]]; then
    record PASS audit_daemon "all checks passed"
  else
    # audit_daemon exits non-zero if any check FAILs; extract count from JSON
    fail_count=$(python3 -c "
import json,sys
try:
  d=json.load(open('$DATA/audit_status.json'))
  print(sum(1 for c in d.get('checks',[]) if c.get('status')=='FAIL'))
except: print('?')
" 2>/dev/null || echo "?")
    record WARN audit_daemon "audit_daemon exit $rc — $fail_count check(s) FAIL (see data/audit_status.json)"
  fi
else
  record WARN audit_daemon "audit_daemon binary not found — skipping"
fi

# ── 5. Write findings JSON ─────────────────────────────────────────────────────
overall="PASS"
[[ $fail -gt 0 ]] && overall="FAIL"
[[ $fail -eq 0 && $warn -gt 0 ]] && overall="WARN"

joined=$(IFS=,; echo "${findings[*]:-}")
cat > "$FINDINGS" <<EOF
{
  "ts": "$TS",
  "overall": "$overall",
  "pass": $pass,
  "fail": $fail,
  "warn": $warn,
  "fast": $FAST,
  "findings": [$joined]
}
EOF

log "=== SUMMARY: $overall ($pass PASS / $fail FAIL / $warn WARN) ==="
echo "$TS  $overall  $pass/$fail/$warn  fast=$FAST" >> "$LOG"

[[ "$overall" == "FAIL" ]] && exit 1 || exit 0
