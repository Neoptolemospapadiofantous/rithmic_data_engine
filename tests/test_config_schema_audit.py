"""
tests/test_config_schema_audit.py — Unit tests for scripts/config_schema_audit.py.

Coverage:
  - run_audit() returns FAIL/CRITICAL when validate_config cannot be imported
  - run_audit() returns FAIL/CRITICAL when live_config.json does not exist
  - run_audit() returns PASS when validate_config returns (True, [])
  - run_audit() returns FAIL when validate_config returns (False, [errors])
  - _result() helper returns correct dict structure
"""
from __future__ import annotations

import sys
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

REPO_ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(REPO_ROOT))

import scripts.config_schema_audit as csa

pytestmark = pytest.mark.fast


# ── _result() helper ─────────────────────────────────────────────────────────

def test_result_helper_returns_correct_structure():
    r = csa._result("my_check", "PASS", "INFO", "all good")
    assert r == {
        "check": "my_check",
        "status": "PASS",
        "severity": "INFO",
        "message": "all good",
    }


def test_result_helper_fail():
    r = csa._result("schema_file", "FAIL", "CRITICAL", "not found")
    assert r["status"] == "FAIL"
    assert r["severity"] == "CRITICAL"


# ── run_audit() — import failure ──────────────────────────────────────────────

def test_run_audit_returns_fail_on_import_error(tmp_path):
    """run_audit() → FAIL/CRITICAL when validate_config cannot be imported."""
    with patch.dict("sys.modules", {"config.live_config_schema": None}), \
         patch("builtins.__import__", side_effect=ImportError("pydantic not found")):
        findings = csa.run_audit()
    assert len(findings) >= 1
    assert any(f["status"] == "FAIL" for f in findings)
    assert any(f["severity"] == "CRITICAL" for f in findings)


# ── run_audit() — config file missing ────────────────────────────────────────

def test_run_audit_returns_fail_when_config_missing(tmp_path):
    """run_audit() → FAIL/CRITICAL when live_config.json does not exist."""
    fake_validate = MagicMock(return_value=(True, []))
    mock_module = MagicMock()
    mock_module.validate_config = fake_validate

    missing_path = tmp_path / "nonexistent.json"

    with patch.dict("sys.modules", {"config.live_config_schema": mock_module}), \
         patch.object(csa, "CONFIG_PATH", missing_path):
        findings = csa.run_audit()

    assert any(f["status"] == "FAIL" and f["severity"] == "CRITICAL" for f in findings)


# ── run_audit() — valid config ────────────────────────────────────────────────

def test_run_audit_returns_pass_on_valid_config(tmp_path):
    """run_audit() → PASS when validate_config returns (True, [])."""
    cfg_path = tmp_path / "live_config.json"
    cfg_path.write_text("{}")  # content doesn't matter — validate_config is mocked

    mock_module = MagicMock()
    mock_module.validate_config = MagicMock(return_value=(True, []))

    with patch.dict("sys.modules", {"config.live_config_schema": mock_module}), \
         patch.object(csa, "CONFIG_PATH", cfg_path):
        findings = csa.run_audit()

    assert any(f["status"] == "PASS" for f in findings)
    assert not any(f["status"] == "FAIL" for f in findings)


# ── run_audit() — schema violations ──────────────────────────────────────────

def test_run_audit_returns_fail_on_schema_errors(tmp_path):
    """run_audit() → FAIL when validate_config returns (False, [errors])."""
    cfg_path = tmp_path / "live_config.json"
    cfg_path.write_text("{}")

    mock_module = MagicMock()
    mock_module.validate_config = MagicMock(return_value=(False, [
        "point_value",
        "Value error, must be 2.0 for MNQ",
    ]))

    with patch.dict("sys.modules", {"config.live_config_schema": mock_module}), \
         patch.object(csa, "CONFIG_PATH", cfg_path):
        findings = csa.run_audit()

    assert any(f["status"] == "FAIL" for f in findings)


def test_run_audit_schema_error_fallback(tmp_path):
    """run_audit() uses a single-finding fallback when pydantic errors don't parse."""
    cfg_path = tmp_path / "live_config.json"
    cfg_path.write_text("{}")

    mock_module = MagicMock()
    # Provide only unparseable error lines
    mock_module.validate_config = MagicMock(return_value=(False, [
        "1 validation error for LiveConfig",
        "some unexpected format",
    ]))

    with patch.dict("sys.modules", {"config.live_config_schema": mock_module}), \
         patch.object(csa, "CONFIG_PATH", cfg_path):
        findings = csa.run_audit()

    assert any(f["status"] == "FAIL" for f in findings)
