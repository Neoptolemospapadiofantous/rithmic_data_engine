#!/usr/bin/env bash
# Deploy Manager agent — Oracle deploy + health check.
# Called by coordinator with: bash scripts/agents/deploy_manager.sh [--health-only]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
ORACLE="opc@170.9.233.177"
SSH_KEY="$HOME/.ssh/id_ed25519"
SERVICE="${2:-nq_executor@RTH}"
HEALTH_ONLY="${1:-}"
OUT="$REPO/data/agents/deploy-manager"
TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

update_status() {
  python3 "$REPO/scripts/agents/agent_status.py" \
    "$REPO/data/agents" "deploy-manager" "$1" "$TS" "$2" 2>/dev/null || true
}

update_status "running" "started"
RESULT="ok"

# Oracle health check
ORACLE_STATUS=$(ssh -i "$SSH_KEY" -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
  "$ORACLE" "systemctl is-active $SERVICE; exit 0" 2>/dev/null || echo "unreachable")

if [[ "$HEALTH_ONLY" == "--health-only" ]]; then
  echo "DEPLOY_MANAGER: Oracle $SERVICE = $ORACLE_STATUS"
  cat > "$OUT/last_run.json" << EOF
{"ts":"$TS","action":"health-check","oracle_status":"$ORACLE_STATUS","service":"$SERVICE"}
EOF
  update_status "idle" "health:$ORACLE_STATUS"
  exit 0
fi

# Full deploy — only if hermes is green
OVERALL=$(python3 -c "import json; print(json.load(open('$REPO/data/hermes_findings.json'))['overall'])" 2>/dev/null || echo "UNKNOWN")

if [[ "$OVERALL" != "PASS" ]]; then
  echo "DEPLOY_MANAGER: skipping deploy — hermes=$OVERALL"
  update_status "idle" "skipped:hermes=$OVERALL"
  exit 0
fi

echo "DEPLOY_MANAGER: deploying (hermes=PASS)"
cd "$REPO"
if make deploy > "$OUT/deploy.log" 2>&1; then
  sleep 15
  ORACLE_STATUS=$(ssh -i "$SSH_KEY" -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
    "$ORACLE" "systemctl is-active $SERVICE; exit 0" 2>/dev/null || echo "unreachable")
  RESULT="deployed:oracle=$ORACLE_STATUS"
else
  RESULT="deploy_failed"
fi

cat > "$OUT/last_run.json" << EOF
{"ts":"$TS","action":"deploy","hermes":"$OVERALL","oracle_status":"$ORACLE_STATUS","result":"$RESULT"}
EOF

update_status "idle" "$RESULT"
echo "DEPLOY_MANAGER: $RESULT"
