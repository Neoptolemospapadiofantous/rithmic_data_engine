.PHONY: build test hermes hermes-fast hermes-note hermes-fleet hermes-fleet-fast hermes-lifecycle hermes-lifecycle-once hermes-lifecycle-local coordinator-start coordinator-once coordinator-status agent-run obsidian-daily obsidian-session push-eod deploy deploy-dry clean

BUILD_DIR := build
JOBS      := $(shell nproc)
ORACLE    := opc@170.9.233.177
SSH_KEY   := ~/.ssh/id_ed25519

# ── Build ──────────────────────────────────────────────────────────────────────
build:
	cmake --build $(BUILD_DIR) -j$(JOBS)

# Rebuild from scratch (run cmake first if build/ is missing)
configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# ── Tests (C++ only) ───────────────────────────────────────────────────────────
# Unit tests: no DB, no network — fast (<2s).
test-unit:
	$(BUILD_DIR)/test_orb_strategy
	$(BUILD_DIR)/test_risk_manager
	$(BUILD_DIR)/test_validator

# Full test suite including DB test.
test: test-unit
	$(BUILD_DIR)/test_db

# ── Hermes CI loop ─────────────────────────────────────────────────────────────
# Full check: build + all tests + audit_daemon (local/testing only).
hermes:
	@bash scripts/hermes.sh

# Fast check: unit tests only, skip DB test.
hermes-fast:
	@bash scripts/hermes.sh --fast

# Run hermes then write a note + update today's daily note in Obsidian.
hermes-note:
	@bash scripts/hermes.sh && bash scripts/obsidian_note.sh || \
	  (bash scripts/obsidian_note.sh && exit 1)
	@bash scripts/obsidian_daily.sh

# Full multi-agent fleet: 4 specialist agents analyse in parallel, coordinator fixes + writes enriched Obsidian note.
hermes-fleet:
	@bash scripts/hermes_fleet.sh

# Fast fleet (skips DB test in baseline hermes run).
hermes-fleet-fast:
	@bash scripts/hermes_fleet.sh --fast

# Time-aware lifecycle loop: auto-selects phase (PRE_MARKET/SESSION/POST_SESSION/OVERNIGHT).
hermes-lifecycle:
	@bash scripts/hermes_lifecycle.sh

# Single lifecycle cycle (for testing — does not loop).
hermes-lifecycle-once:
	@bash scripts/hermes_lifecycle.sh --once

# Lifecycle without git push or make deploy (safe for local dev).
hermes-lifecycle-local:
	@bash scripts/hermes_lifecycle.sh --no-deploy

# ── Coordinator (manages all specialist agents) ────────────────────────────────
# Persistent coordinator loop — orchestrates all 5 agents based on market phase.
coordinator-start:
	@bash scripts/coordinator.sh

# Run one full coordinator cycle then exit.
coordinator-once:
	@bash scripts/coordinator.sh --once

# Show current status of all agents.
coordinator-status:
	@bash scripts/agents/status.sh

# Run a single agent manually: make agent-run AGENT=ci-watchdog
agent-run:
	@bash scripts/agents/$(subst -,_,$(AGENT)).sh

# Regenerate today's daily digest note in Obsidian.
obsidian-daily:
	@bash scripts/obsidian_daily.sh

# Generate a trading session post-mortem from PostgreSQL (optional date: make obsidian-session DATE=2026-05-09).
obsidian-session:
	@bash scripts/obsidian_session.sh $(or $(DATE),)

# ── End-of-day push ────────────────────────────────────────────────────────────
push-eod:
	@bash scripts/hermes.sh --fast || (echo "Not clean — fix before pushing." && exit 1)
	@echo "All gates pass. Pushing..."
	git push origin main
	@echo "Done. Oracle will pull on next deployment."

# ── Oracle deployment ──────────────────────────────────────────────────────────
# Pipeline: validate locally → commit → push → Oracle pull+rebuild → restart.
# Rule: ALL changes must be committed before deploying. Uncommitted changes
# never reach Oracle because Oracle builds from git, not from local files.
deploy:
	@bash scripts/hermes.sh --fast || (echo "Not clean — fix before deploying." && exit 1)
	@if ! git diff --quiet || ! git diff --cached --quiet; then \
		echo "ERROR: Uncommitted changes detected. Commit them before deploying."; \
		echo "       Run: git add <files> && git commit -m 'your message'"; \
		git status --short; \
		exit 1; \
	fi
	@echo "Gates pass — working tree clean. Pushing to origin..."
	git push origin main
	@echo "Connecting to Oracle..."
	ssh -i $(SSH_KEY) -o StrictHostKeyChecking=no $(ORACLE) '\
		set -e; \
		cd /home/opc/rithmic_engine && git pull origin main; \
		cmake --build build -j$$(nproc) --target rithmic_engine nq_executor; \
		sudo install -m 755 build/nq_executor /usr/local/bin/nq_executor; \
		sudo chcon -t bin_t /usr/local/bin/nq_executor 2>/dev/null || true; \
		sudo chcon -t bin_t /home/opc/rithmic_engine/build/nq_executor 2>/dev/null || true; \
		sudo chcon -t etc_t /home/opc/rithmic_engine/.env 2>/dev/null || true; \
		sudo cp deploy/nq_executor@.service /etc/systemd/system/nq_executor@.service; \
		sudo cp deploy/nq_executor_24x7@.service /etc/systemd/system/nq_executor-24x7@.service; \
		cp -f scripts/run_continuous.sh /home/opc/rithmic_engine/scripts/run_continuous.sh 2>/dev/null || true; \
		sudo systemctl disable nq_executor.service 2>/dev/null || true; \
		sudo systemctl daemon-reload; \
		route=$$(python3 -c "import json; print(json.load(open(\"config/live_config.json\")).get(\"trade_route\",\"\"))" 2>/dev/null || echo ""); \
		if [[ "$$route" == "Rithmic Order Routing" ]]; then \
			echo "ERROR: live_config.json trade_route is \"Rithmic Order Routing\" — aborting deploy"; \
			exit 1; \
		fi; \
		PGPASSWORD=testpass123 psql -h 127.0.0.1 -U rithmic_user -d rithmic \
			-c "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE usename='"'"'rithmic_user'"'"' AND state='"'"'idle in transaction'"'"' AND pid != pg_backend_pid();" 2>/dev/null || true; \
		for svc in rithmic-engine "nq_executor-24x7@legends"; do \
			if systemctl is-active "$$svc" 2>/dev/null | grep -q "^active$$"; then \
				echo "Restarting $$svc..."; \
				sudo systemctl restart "$$svc"; \
			fi; \
		done; \
		echo "Deploy complete."'

# Dry-run: show what deploy would do without connecting.
deploy-dry:
	@echo "=== deploy-dry ==="
	@echo "1. bash scripts/hermes.sh --fast"
	@echo "2. git push origin main"
	@echo "3. SSH $(ORACLE)"
	@echo "   a. cd /home/opc/rithmic_engine && git pull origin main"
	@echo "   b. cmake --build build --target rithmic_engine nq_executor"
	@echo "   c. restart active services (nq_executor, rithmic-engine)"
	@echo "   NOTE: audit_daemon is local/testing only — not deployed"
	@echo "=== End dry run — no connection made ==="

# ── Misc ───────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
