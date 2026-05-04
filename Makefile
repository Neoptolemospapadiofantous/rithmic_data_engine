.PHONY: build test hermes hermes-fast push-eod deploy deploy-dry clean

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

# ── End-of-day push ────────────────────────────────────────────────────────────
push-eod:
	@bash scripts/hermes.sh --fast || (echo "Not clean — fix before pushing." && exit 1)
	@echo "All gates pass. Pushing..."
	git push origin main
	@echo "Done. Oracle will pull on next deployment."

# ── Oracle deployment ──────────────────────────────────────────────────────────
# Deploys: rithmic_engine (tick collector) + nq_executor (ORB trader).
# audit_daemon is LOCAL/TESTING ONLY — NOT deployed to Oracle.
deploy:
	@bash scripts/hermes.sh --fast || (echo "Not clean — fix before deploying." && exit 1)
	@echo "Gates pass. Pushing to origin..."
	git push origin main
	@echo "Connecting to Oracle..."
	ssh -i $(SSH_KEY) -o StrictHostKeyChecking=no $(ORACLE) '\
		set -e; \
		cd /home/opc/rithmic_engine && git pull origin main; \
		cmake --build build -j$$(nproc) --target rithmic_engine nq_executor; \
		for svc in nq_executor rithmic-engine; do \
			if systemctl is-active $$svc 2>/dev/null | grep -q "^active$$"; then \
				echo "Restarting $$svc..."; \
				sudo systemctl restart $$svc; \
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
