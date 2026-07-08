#!/usr/bin/env bash
# provision_oracle.sh — full bring-up of a FRESH Oracle Linux 9 VM for rithmic_engine.
#
# Run from the workstation (not the VM):
#   bash scripts/provision_oracle.sh <new-public-ip>
#
# What it does (idempotent — safe to re-run after a partial failure):
#   1. Waits for SSH on the new VM
#   2. dnf packages: toolchain, protobuf, OpenSSL, PostgreSQL 16 (PGDG), TimescaleDB
#   3. Initializes PG16, creates rithmic DB + rithmic_user, enables timescaledb
#   4. Builds Boost 1.83 from source (OL9 ships 1.75; CMake requires >= 1.82)
#   5. rsyncs the full local repo (incl. .git, .env, certs — untracked files too)
#   6. Creates per-instance configs (RTH_config.json / legends_config.json) from
#      live_config.json if absent — the systemd template units load these
#   7. Applies migrations/*.sql in order
#   8. cmake build: rithmic_engine, nq_executor, test_connection
#      (audit_daemon is local/testing only — deliberately NOT built here)
#   9. Installs systemd template units, SELinux labels, generates a GitHub
#      deploy key for `make deploy` (git pull on the VM)
#  10. Updates the hardcoded IP in local Makefile/scripts/docs
#
# It does NOT start the trading service — operator verifies first (see the
# printed next-steps and RUNBOOK.md go-live checklist).

set -euo pipefail

NEW_IP="${1:?Usage: bash scripts/provision_oracle.sh <new-public-ip>}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
ORACLE="opc@${NEW_IP}"
SSH_KEY="$HOME/.ssh/id_ed25519"
SSH_OPTS=(-i "$SSH_KEY" -o StrictHostKeyChecking=no -o ConnectTimeout=10)
OLD_IP="170.9.233.177"

log() { echo "[provision] $*"; }
rssh() { ssh "${SSH_OPTS[@]}" "$ORACLE" "$@"; }

# ── 1. Wait for SSH ───────────────────────────────────────────────────────────
log "Waiting for SSH on ${NEW_IP}..."
for i in $(seq 1 30); do
  if rssh 'echo ok' >/dev/null 2>&1; then log "SSH up."; break; fi
  [[ $i -eq 30 ]] && { echo "ERROR: SSH not reachable after 5 min"; exit 1; }
  sleep 10
done

rssh 'cat /etc/oracle-release || cat /etc/os-release | head -2'

# ── 2. Packages ───────────────────────────────────────────────────────────────
log "Installing packages (toolchain, protobuf, PG16, TimescaleDB)..."
rssh 'sudo dnf install -y gcc-c++ cmake git make rsync tar bzip2 which \
        protobuf-devel protobuf-compiler openssl-devel \
        policycoreutils-python-utils python3'

rssh 'sudo dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-$(arch)/pgdg-redhat-repo-latest.noarch.rpm 2>/dev/null || true
      sudo dnf -qy module disable postgresql || true
      sudo dnf install -y postgresql16-server postgresql16-devel postgresql16-contrib'

rssh 'sudo tee /etc/yum.repos.d/timescale.repo >/dev/null <<REPO
[timescale_timescaledb]
name=timescale_timescaledb
baseurl=https://packagecloud.io/timescale/timescaledb/el/9/\$basearch
repo_gpgcheck=1
gpgcheck=0
enabled=1
gpgkey=https://packagecloud.io/timescale/timescaledb/gpgkey
sslverify=1
sslcacert=/etc/pki/tls/certs/ca-bundle.crt
metadata_expire=300
REPO
      sudo dnf install -y timescaledb-2-postgresql-16 || echo "WARN: timescaledb install failed (ARM shape? collector hypertables degraded)"'

# ── 3. PostgreSQL init + DB ───────────────────────────────────────────────────
log "Initializing PostgreSQL 16..."
rssh '[ -f /var/lib/pgsql/16/data/PG_VERSION ] || sudo /usr/pgsql-16/bin/postgresql-16-setup initdb
      if command -v timescaledb-tune >/dev/null 2>&1; then
        sudo timescaledb-tune --quiet --yes --pg-config=/usr/pgsql-16/bin/pg_config || true
      fi
      # Password auth for local TCP (engine connects via 127.0.0.1)
      sudo sed -i -E "s/^(host +all +all +127.0.0.1\/32 +)(ident|peer)/\1scram-sha-256/; s/^(host +all +all +::1\/128 +)(ident|peer)/\1scram-sha-256/" /var/lib/pgsql/16/data/pg_hba.conf
      sudo systemctl enable --now postgresql-16
      sudo systemctl restart postgresql-16'

log "Creating rithmic DB + user..."
rssh 'sudo -u postgres /usr/pgsql-16/bin/psql -tAc "SELECT 1 FROM pg_roles WHERE rolname='"'"'rithmic_user'"'"'" | grep -q 1 || \
        sudo -u postgres /usr/pgsql-16/bin/psql -c "CREATE ROLE rithmic_user LOGIN PASSWORD '"'"'testpass123'"'"'"
      sudo -u postgres /usr/pgsql-16/bin/psql -tAc "SELECT 1 FROM pg_database WHERE datname='"'"'rithmic'"'"'" | grep -q 1 || \
        sudo -u postgres /usr/pgsql-16/bin/createdb -O rithmic_user rithmic
      sudo -u postgres /usr/pgsql-16/bin/psql -d rithmic -c "CREATE EXTENSION IF NOT EXISTS timescaledb;" || \
        echo "WARN: timescaledb extension unavailable"'

# ── 4. Boost 1.83 ─────────────────────────────────────────────────────────────
log "Building Boost 1.83 (skipped if already installed — takes ~10 min)..."
rssh 'if [ -f /usr/local/lib/libboost_system.so ]; then echo "Boost already installed"; exit 0; fi
      # Small shapes: add swap so the build does not OOM
      mem_kb=$(grep MemTotal /proc/meminfo | awk "{print \$2}")
      if [ "$mem_kb" -lt 4000000 ] && [ ! -f /swapfile ]; then
        sudo fallocate -l 4G /swapfile && sudo chmod 600 /swapfile && sudo mkswap /swapfile && sudo swapon /swapfile
      fi
      cd ~
      [ -f boost_1_83_0.tar.gz ] || curl -fL -o boost_1_83_0.tar.gz https://archives.boost.io/release/1.83.0/source/boost_1_83_0.tar.gz
      rm -rf boost_1_83_0 && tar xf boost_1_83_0.tar.gz && cd boost_1_83_0
      ./bootstrap.sh --prefix=/usr/local --with-libraries=system,thread,filesystem,regex,random,chrono,date_time,atomic 2>&1 | tail -2
      sudo ./b2 install -j$(nproc) variant=release link=shared 2>&1 | tail -3
      sudo ldconfig
      echo /usr/local/lib | sudo tee /etc/ld.so.conf.d/usr-local.conf >/dev/null && sudo ldconfig'

# ── 5. Upload repo (full working tree: includes .env, certs, untracked configs) ─
log "Uploading repository to ${ORACLE}:~/rithmic_engine ..."
rssh 'mkdir -p ~/rithmic_engine'
rsync -az --delete \
  --exclude='build/' --exclude='CMakeFiles/' --exclude='data/logs/*' \
  --exclude='boost_*' \
  -e "ssh ${SSH_OPTS[*]}" \
  "$REPO/" "$ORACLE:/home/opc/rithmic_engine/"

# ── 6. Per-instance configs (systemd template: config/%I_config.json) ─────────
log "Creating instance configs from live_config.json (if absent)..."
rssh 'cd ~/rithmic_engine/config
      for inst in RTH legends; do
        [ -f ${inst}_config.json ] || { cp live_config.json ${inst}_config.json; echo "created ${inst}_config.json"; }
      done
      grep -H "\"trade_contract\"\|\"trade_route\"" RTH_config.json legends_config.json'

# ── 7. Migrations ─────────────────────────────────────────────────────────────
log "Applying migrations..."
rssh 'cd ~/rithmic_engine
      for f in $(ls migrations/*.sql | sort); do
        echo "-- $f";
        PGPASSWORD=testpass123 /usr/pgsql-16/bin/psql -h 127.0.0.1 -U rithmic_user -d rithmic -v ON_ERROR_STOP=0 -q -f "$f" || true
      done'

# ── 8. Build ──────────────────────────────────────────────────────────────────
log "Building rithmic_engine + nq_executor (audit_daemon intentionally excluded)..."
rssh 'cd ~/rithmic_engine
      cmake -B build -DCMAKE_BUILD_TYPE=Release \
            -DPostgreSQL_ROOT=/usr/pgsql-16 \
            -DBOOST_ROOT=/usr/local -DBoost_NO_SYSTEM_PATHS=ON
      cmake --build build -j$(nproc) --target rithmic_engine nq_executor test_connection'

# ── 9. systemd units + SELinux + deploy key ──────────────────────────────────
log "Installing systemd units..."
rssh 'cd ~/rithmic_engine
      sudo cp deploy/nq_executor@.service /etc/systemd/system/nq_executor@.service
      sudo cp deploy/nq_executor_24x7@.service /etc/systemd/system/nq_executor-24x7@.service
      sudo systemctl daemon-reload
      sudo chcon -t bin_t  ~/rithmic_engine/build/nq_executor 2>/dev/null || true
      sudo chcon -t bin_t  ~/rithmic_engine/build/rithmic_engine 2>/dev/null || true
      sudo chcon -t etc_t  ~/rithmic_engine/.env 2>/dev/null || true'

log "Generating GitHub deploy key (for git pull in make deploy)..."
rssh '[ -f ~/.ssh/id_ed25519 ] || ssh-keygen -t ed25519 -N "" -f ~/.ssh/id_ed25519 -C "oracle-rithmic-deploy" >/dev/null
      ssh-keyscan -t ed25519 github.com >> ~/.ssh/known_hosts 2>/dev/null
      echo "── Add this as a READ-ONLY deploy key on the GitHub repo ──"
      cat ~/.ssh/id_ed25519.pub'

# ── 10. Update local hardcoded IP ─────────────────────────────────────────────
log "Updating local files ${OLD_IP} → ${NEW_IP} ..."
for f in Makefile RUNBOOK.md CLAUDE.md engine_architecture.html \
         scripts/hermes_lifecycle.sh scripts/agents/deploy_manager.sh; do
  if grep -q "$OLD_IP" "$REPO/$f" 2>/dev/null; then
    sed -i "s/${OLD_IP}/${NEW_IP}/g" "$REPO/$f"
    echo "  updated $f"
  fi
done

log "=== PROVISION COMPLETE ==="
echo ""
echo "Next steps (manual, per RUNBOOK go-live checklist):"
echo "  1. Add the deploy key printed above to GitHub (repo → Settings → Deploy keys)"
echo "  2. Smoke-test credentials:  ssh ${ORACLE} 'cd rithmic_engine/build && ./test_connection ../.env'"
echo "  3. Verify config:           ssh ${ORACLE} 'grep -E \"trade_route|trade_contract|dry_run\" rithmic_engine/config/RTH_config.json'"
echo "  4. Start when ready:        ssh ${ORACLE} 'sudo systemctl start nq_executor@RTH'"
echo "  5. Watch logs:              ssh ${ORACLE} 'journalctl -u nq_executor_RTH -f'   (SyslogIdentifier=nq_executor_RTH)"
