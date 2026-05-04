#!/usr/bin/env bash
# Post-deploy validation on Oracle — run after every deploy.
# Usage: ssh opc@170.9.233.177 'bash ~/rithmic_engine/scripts/validate-remote.sh'
set -euo pipefail

RED='\033[31m'; GREEN='\033[32m'; YELLOW='\033[33m'; RESET='\033[0m'
pass=0; fail=0

ok()   { echo -e "${GREEN}✓${RESET} $*"; ((pass++)); }
fail() { echo -e "${RED}✗${RESET} $*"; ((fail++)); }
warn() { echo -e "${YELLOW}~${RESET} $*"; }

echo "=== validate-remote.sh — Oracle post-deploy check ==="
echo

# 1. Binary at /usr/local/bin/ with correct SELinux context
if [[ -x /usr/local/bin/nq_executor ]]; then
  ctx=$(stat -c "%C" /usr/local/bin/nq_executor 2>/dev/null || echo "unknown")
  if [[ "$ctx" == *"bin_t"* || "$ctx" == *"unconfined"* ]]; then
    ok "nq_executor at /usr/local/bin/ (context: $ctx)"
  else
    fail "nq_executor has wrong SELinux context: $ctx (expected bin_t)"
  fi
else
  fail "nq_executor not found at /usr/local/bin/"
fi

# 2. Service file points to /usr/local/bin/
if grep -q "ExecStart=/usr/local/bin/nq_executor" /etc/systemd/system/nq_executor.service 2>/dev/null; then
  ok "Service file ExecStart=/usr/local/bin/nq_executor"
else
  fail "Service file does NOT point to /usr/local/bin/nq_executor"
fi

# 3. trade_route in live_config.json must never be "Rithmic Order Routing"
LIVE_CFG="/home/opc/rithmic_engine/config/live_config.json"
if [[ -f "$LIVE_CFG" ]]; then
  route=$(python3 -c "import json; print(json.load(open('$LIVE_CFG')).get('trade_route','MISSING'))" 2>/dev/null || echo "MISSING")
  if [[ "$route" == "Rithmic Order Routing" ]]; then
    fail "live_config.json trade_route='Rithmic Order Routing' — silently cancels all Legends orders"
  elif [[ "$route" == "simulator" ]]; then
    ok "live_config.json trade_route='simulator'"
  else
    warn "live_config.json trade_route='$route' (verify this is correct)"
    ((pass++))
  fi
else
  fail "live_config.json not found"
fi

# 4. Account ID matches expected Legends account
acct=$(python3 -c "import json; print(json.load(open('$LIVE_CFG')).get('account_id',''))" 2>/dev/null || echo "")
if [[ "$acct" == "LTARAPAPA503900216181" ]]; then
  ok "account_id=$acct"
else
  fail "account_id='$acct' (expected LTARAPAPA503900216181)"
fi

# 5. dry_run must be false in production
dry=$(python3 -c "import json; print(json.load(open('$LIVE_CFG')).get('dry_run','?'))" 2>/dev/null || echo "?")
if [[ "$dry" == "False" || "$dry" == "false" ]]; then
  ok "dry_run=false"
else
  fail "dry_run=$dry (must be false in production)"
fi

# 6. No zombie PostgreSQL backends in idle-in-transaction
zombies=$(PGPASSWORD=testpass123 psql -h 127.0.0.1 -U rithmic_user -d rithmic -t \
  -c "SELECT count(*) FROM pg_stat_activity WHERE state='idle in transaction' AND usename='rithmic_user';" \
  2>/dev/null | tr -d ' ' || echo "?")
if [[ "$zombies" == "0" ]]; then
  ok "No idle-in-transaction PostgreSQL backends"
elif [[ "$zombies" == "?" ]]; then
  warn "Could not check PostgreSQL backends"
else
  fail "$zombies idle-in-transaction backend(s) — may block schema migration on next restart"
fi

# 7. NO_DEPLOY lockfile must not exist before start
if [[ -f /home/opc/rithmic_engine/NO_DEPLOY ]]; then
  fail "NO_DEPLOY lockfile present — service will refuse to start"
else
  ok "No NO_DEPLOY lockfile"
fi

# 8. Service status
svc_status=$(systemctl is-active nq_executor 2>/dev/null || echo "inactive")
if [[ "$svc_status" == "active" ]]; then
  ok "nq_executor service is active"
elif [[ "$svc_status" == "inactive" ]]; then
  warn "nq_executor is inactive (may be stopped intentionally)"
  ((pass++))
else
  fail "nq_executor service status: $svc_status"
fi

echo
echo "=== RESULT: $pass PASS / $fail FAIL ==="
[[ $fail -eq 0 ]] && exit 0 || exit 1
