#!/usr/bin/env python3
"""
test_cross_system_audit.py — Unit tests for scripts/cross_system_audit.py.

Each check function is tested with at least two scenarios:
  - one that produces a PASS result (correct MNQ values)
  - one that produces a FAIL result (NQ values or deliberate mismatch)

All tests use monkeypatch or tmp_path to control file paths so that the real
codebase files are not required.

All tests marked @pytest.mark.fast — no live DB, no subprocess, no network.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).parent.parent
if str(REPO_ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "scripts"))

import cross_system_audit as csa

pytestmark = pytest.mark.fast

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _redirect(monkeypatch, attr: str, path: Path) -> None:
    """Point a cross_system_audit module-level Path variable to *path*."""
    monkeypatch.setattr(csa, attr, path)


def _write(path: Path, content: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)
    return path


# ---------------------------------------------------------------------------
# check_cpp_tick_value
# ---------------------------------------------------------------------------

class TestCheckCppTickValue:
    """Check 1: MNQ_TICK_VALUE in orb_config.hpp."""

    def test_mnq_tick_value_correct_passes(self, tmp_path, monkeypatch):
        """orb_config.hpp with MNQ_TICK_VALUE=0.50 → PASS finding."""
        hpp = tmp_path / "orb_config.hpp"
        _write(hpp, "inline constexpr double MNQ_TICK_VALUE = 0.50;\n")
        _redirect(monkeypatch, "ORB_CONFIG_HPP", hpp)

        findings = csa.check_cpp_tick_value()
        tick_findings = [f for f in findings if f["check"] == "cpp_mnq_tick_value"]
        assert tick_findings, "No cpp_mnq_tick_value finding produced"
        assert tick_findings[0]["status"] == "PASS"

    def test_nq_tick_value_wrong_fails(self, tmp_path, monkeypatch):
        """orb_config.hpp with MNQ_TICK_VALUE=5.0 (NQ value) → FAIL finding."""
        hpp = tmp_path / "orb_config.hpp"
        _write(hpp, "inline constexpr double MNQ_TICK_VALUE = 5.00;\n")
        _redirect(monkeypatch, "ORB_CONFIG_HPP", hpp)

        findings = csa.check_cpp_tick_value()
        tick_findings = [f for f in findings if f["check"] == "cpp_mnq_tick_value"]
        assert tick_findings, "No cpp_mnq_tick_value finding produced"
        assert tick_findings[0]["status"] == "FAIL"

    def test_missing_hpp_file_fails(self, tmp_path, monkeypatch):
        """orb_config.hpp absent → FAIL finding."""
        _redirect(monkeypatch, "ORB_CONFIG_HPP", tmp_path / "nonexistent.hpp")
        findings = csa.check_cpp_tick_value()
        assert any(f["status"] == "FAIL" for f in findings)

    def test_mnq_tick_value_constant_missing_fails(self, tmp_path, monkeypatch):
        """orb_config.hpp exists but lacks MNQ_TICK_VALUE constant → FAIL."""
        hpp = tmp_path / "orb_config.hpp"
        _write(hpp, "// orb_config.hpp — no tick value defined\n")
        _redirect(monkeypatch, "ORB_CONFIG_HPP", hpp)

        findings = csa.check_cpp_tick_value()
        tick_findings = [f for f in findings if f["check"] == "cpp_mnq_tick_value"]
        assert tick_findings
        assert tick_findings[0]["status"] == "FAIL"


# ---------------------------------------------------------------------------
# check_cpp_point_value
# ---------------------------------------------------------------------------

class TestCheckCppPointValue:
    """Check 2: OrbConfig::point_value default in orb_config.hpp."""

    def test_mnq_point_value_correct_passes(self, tmp_path, monkeypatch):
        """point_value=2.0 (MNQ) → PASS."""
        hpp = tmp_path / "orb_config.hpp"
        _write(hpp, "    double point_value = 2.0;\n")
        _redirect(monkeypatch, "ORB_CONFIG_HPP", hpp)

        findings = csa.check_cpp_point_value()
        assert len(findings) == 1
        assert findings[0]["status"] == "PASS"

    def test_nq_point_value_wrong_fails(self, tmp_path, monkeypatch):
        """point_value=20.0 (NQ) → FAIL."""
        hpp = tmp_path / "orb_config.hpp"
        _write(hpp, "    double point_value = 20.0;\n")
        _redirect(monkeypatch, "ORB_CONFIG_HPP", hpp)

        findings = csa.check_cpp_point_value()
        assert len(findings) == 1
        assert findings[0]["status"] == "FAIL"
        assert "20.0" in findings[0]["message"]

    def test_missing_hpp_fails(self, tmp_path, monkeypatch):
        """Missing orb_config.hpp → FAIL."""
        _redirect(monkeypatch, "ORB_CONFIG_HPP", tmp_path / "missing.hpp")
        findings = csa.check_cpp_point_value()
        assert findings[0]["status"] == "FAIL"

    def test_no_point_value_field_warns(self, tmp_path, monkeypatch):
        """hpp exists but OrbConfig::point_value field absent → WARN."""
        hpp = tmp_path / "orb_config.hpp"
        _write(hpp, "struct OrbConfig { int ticks = 4; };\n")
        _redirect(monkeypatch, "ORB_CONFIG_HPP", hpp)

        findings = csa.check_cpp_point_value()
        assert findings[0]["status"] == "WARN"


# ---------------------------------------------------------------------------
# check_symbol_consistency
# ---------------------------------------------------------------------------

class TestCheckSymbolConsistency:
    """Check 3: live_config.json symbol vs C++ OrbConfig symbol default."""

    def _make_config(self, tmp_path: Path, symbol: str) -> Path:
        cfg_path = tmp_path / "config" / "live_config.json"
        _write(cfg_path, json.dumps({"symbol": symbol}))
        return cfg_path

    def _make_hpp(self, tmp_path: Path, symbol: str) -> Path:
        hpp = tmp_path / "orb_config.hpp"
        _write(hpp, f'    std::string symbol = "{symbol}";\n')
        return hpp

    def test_matching_symbols_passes(self, tmp_path, monkeypatch):
        """Python config and C++ default both MNQ → PASS."""
        cfg = self._make_config(tmp_path, "MNQ")
        hpp = self._make_hpp(tmp_path, "MNQ")
        _redirect(monkeypatch, "CONFIG_PATH", cfg)
        _redirect(monkeypatch, "ORB_CONFIG_HPP", hpp)

        findings = csa.check_symbol_consistency()
        assert any(f["status"] == "PASS" for f in findings)

    def test_mismatched_symbols_emits_info(self, tmp_path, monkeypatch):
        """Python=MNQ, C++ default=NQ → INFO (runtime override is expected)."""
        cfg = self._make_config(tmp_path, "MNQ")
        hpp = self._make_hpp(tmp_path, "NQ")
        _redirect(monkeypatch, "CONFIG_PATH", cfg)
        _redirect(monkeypatch, "ORB_CONFIG_HPP", hpp)

        findings = csa.check_symbol_consistency()
        assert any(f["status"] in ("INFO", "WARN") for f in findings)

    def test_missing_config_fails(self, tmp_path, monkeypatch):
        """Missing live_config.json → FAIL."""
        _redirect(monkeypatch, "CONFIG_PATH", tmp_path / "missing.json")
        _redirect(monkeypatch, "ORB_CONFIG_HPP", tmp_path / "orb_config.hpp")
        findings = csa.check_symbol_consistency()
        assert findings[0]["status"] == "FAIL"

    def test_missing_hpp_yields_info(self, tmp_path, monkeypatch):
        """Missing orb_config.hpp → INFO (C++ fallback not checkable)."""
        cfg = self._make_config(tmp_path, "MNQ")
        _redirect(monkeypatch, "CONFIG_PATH", cfg)
        _redirect(monkeypatch, "ORB_CONFIG_HPP", tmp_path / "no_hpp.hpp")

        findings = csa.check_symbol_consistency()
        assert any(f["status"] == "INFO" for f in findings)


# ---------------------------------------------------------------------------
# check_python_point_value_default
# ---------------------------------------------------------------------------

class TestCheckPythonPointValueDefault:
    """Check 4: live_trader.py point_value fallback default."""

    def test_mnq_default_passes(self, tmp_path, monkeypatch):
        """point_value fallback=2.0 (MNQ) → PASS."""
        trader = tmp_path / "live_trader.py"
        _write(trader, "pv = cfg.get('point_value', 2.0)\n")
        _redirect(monkeypatch, "LIVE_TRADER_PY", trader)

        findings = csa.check_python_point_value_default()
        assert any(f["status"] == "PASS" for f in findings)

    def test_nq_default_fails(self, tmp_path, monkeypatch):
        """point_value fallback=20.0 (NQ) → FAIL."""
        trader = tmp_path / "live_trader.py"
        _write(trader, "pv = cfg.get('point_value', 20.0)\n")
        _redirect(monkeypatch, "LIVE_TRADER_PY", trader)

        findings = csa.check_python_point_value_default()
        assert any(f["status"] == "FAIL" for f in findings)
        assert any("10x" in f["message"] for f in findings)

    def test_no_fallback_passes(self, tmp_path, monkeypatch):
        """live_trader.py with no point_value fallback → PASS (no risk)."""
        trader = tmp_path / "live_trader.py"
        _write(trader, "pv = cfg['point_value']\n")
        _redirect(monkeypatch, "LIVE_TRADER_PY", trader)

        findings = csa.check_python_point_value_default()
        assert any(f["status"] == "PASS" for f in findings)

    def test_missing_file_fails(self, tmp_path, monkeypatch):
        """live_trader.py absent → FAIL."""
        _redirect(monkeypatch, "LIVE_TRADER_PY", tmp_path / "missing.py")
        findings = csa.check_python_point_value_default()
        assert findings[0]["status"] == "FAIL"


# ---------------------------------------------------------------------------
# check_micro_orb_point_value
# ---------------------------------------------------------------------------

class TestCheckMicroOrbPointValue:
    """Check 5: strategy/micro_orb.py hardcoded point_value outside comments."""

    def test_no_hardcoded_value_passes(self, tmp_path, monkeypatch):
        """micro_orb.py with no hardcoded 20.0 for point_value → PASS."""
        micro = tmp_path / "micro_orb.py"
        _write(micro, "pv = cfg.get('point_value', 2.0)\n")
        _redirect(monkeypatch, "MICRO_ORB_PY", micro)

        findings = csa.check_micro_orb_point_value()
        assert any(f["status"] == "PASS" for f in findings)

    def test_nq_fallback_in_get_fails(self, tmp_path, monkeypatch):
        """micro_orb.py get('point_value', 20.0) outside comment → FAIL."""
        micro = tmp_path / "micro_orb.py"
        _write(micro, "    pv = cfg.get('point_value', 20.0)\n")
        _redirect(monkeypatch, "MICRO_ORB_PY", micro)

        findings = csa.check_micro_orb_point_value()
        assert any(f["status"] == "FAIL" for f in findings)

    def test_comment_lines_ignored(self, tmp_path, monkeypatch):
        """Lines starting with # are not checked for hardcoded values."""
        micro = tmp_path / "micro_orb.py"
        _write(micro, "# point_value fallback = 20.0  (NQ example in docs)\n"
                      "pv = cfg.get('point_value', 2.0)\n")
        _redirect(monkeypatch, "MICRO_ORB_PY", micro)

        findings = csa.check_micro_orb_point_value()
        assert not any(f["status"] == "FAIL" for f in findings)

    def test_missing_file_skips_with_info(self, tmp_path, monkeypatch):
        """Missing micro_orb.py → INFO (file optional at this path)."""
        _redirect(monkeypatch, "MICRO_ORB_PY", tmp_path / "no_such.py")
        findings = csa.check_micro_orb_point_value()
        assert any(f["status"] == "INFO" for f in findings)


# ---------------------------------------------------------------------------
# check_trade_route
# ---------------------------------------------------------------------------

class TestCheckTradeRoute:
    """Check 6: trade_route in live_config.json must not be 'simulator'."""

    def _cfg(self, tmp_path: Path, route: str) -> Path:
        cfg_path = tmp_path / "live_config.json"
        _write(cfg_path, json.dumps({"trade_route": route}))
        return cfg_path

    def test_simulator_route_fails(self, tmp_path, monkeypatch):
        """trade_route='simulator' → FAIL (BUG-14 guard)."""
        cfg = self._cfg(tmp_path, "simulator")
        _redirect(monkeypatch, "CONFIG_PATH", cfg)

        findings = csa.check_trade_route()
        assert any(f["status"] == "FAIL" for f in findings)
        assert any("simulator" in f["message"] for f in findings)

    def test_live_route_passes(self, tmp_path, monkeypatch):
        """trade_route='Rithmic Order Routing' (live) → PASS."""
        cfg = self._cfg(tmp_path, "Rithmic Order Routing")
        _redirect(monkeypatch, "CONFIG_PATH", cfg)

        findings = csa.check_trade_route()
        assert any(f["status"] == "PASS" for f in findings)

    def test_empty_route_warns(self, tmp_path, monkeypatch):
        """trade_route='' (missing) → WARN."""
        cfg_path = tmp_path / "live_config.json"
        _write(cfg_path, json.dumps({}))
        _redirect(monkeypatch, "CONFIG_PATH", cfg_path)

        findings = csa.check_trade_route()
        assert any(f["status"] == "WARN" for f in findings)

    def test_missing_config_fails(self, tmp_path, monkeypatch):
        """Missing live_config.json → FAIL."""
        _redirect(monkeypatch, "CONFIG_PATH", tmp_path / "no_config.json")
        findings = csa.check_trade_route()
        assert findings[0]["status"] == "FAIL"


# ---------------------------------------------------------------------------
# check_risk_params_consistency
# ---------------------------------------------------------------------------

class TestCheckRiskParamsConsistency:
    """Check 7: flat risk keys match prop_firm nested values (BUG-15 / BUG-16)."""

    def _cfg(self, tmp_path: Path, data: dict) -> Path:
        cfg_path = tmp_path / "live_config.json"
        _write(cfg_path, json.dumps(data))
        return cfg_path

    def _consistent_cfg(self) -> dict:
        return {
            "trailing_drawdown_cap": 2500,
            "consistency_cap_pct": 30,
            "max_daily_trades": 10,
            "prop_firm": {
                "trailing_drawdown_limit": 2500,
                "consistency_rule_pct": 30,
                "max_daily_trades": 10,
            },
        }

    def test_all_matching_passes(self, tmp_path, monkeypatch):
        """Flat keys match prop_firm nested values → all PASS."""
        cfg = self._cfg(tmp_path, self._consistent_cfg())
        _redirect(monkeypatch, "CONFIG_PATH", cfg)

        findings = csa.check_risk_params_consistency()
        assert all(f["status"] == "PASS" for f in findings), findings

    def test_trailing_drawdown_mismatch_fails(self, tmp_path, monkeypatch):
        """Flat trailing_drawdown_cap != prop_firm.trailing_drawdown_limit → FAIL."""
        data = self._consistent_cfg()
        data["trailing_drawdown_cap"] = 9999  # mismatch
        cfg = self._cfg(tmp_path, data)
        _redirect(monkeypatch, "CONFIG_PATH", cfg)

        findings = csa.check_risk_params_consistency()
        fail_findings = [f for f in findings if f["status"] == "FAIL"]
        assert fail_findings, "Expected at least one FAIL finding"
        assert any("trailing_drawdown_cap" in f["message"] for f in fail_findings)

    def test_consistency_cap_mismatch_fails(self, tmp_path, monkeypatch):
        """Flat consistency_cap_pct != prop_firm.consistency_rule_pct → FAIL."""
        data = self._consistent_cfg()
        data["consistency_cap_pct"] = 99  # mismatch
        cfg = self._cfg(tmp_path, data)
        _redirect(monkeypatch, "CONFIG_PATH", cfg)

        findings = csa.check_risk_params_consistency()
        fail_findings = [f for f in findings if f["status"] == "FAIL"]
        assert fail_findings
        assert any("consistency_cap_pct" in f["message"] for f in fail_findings)

    def test_missing_flat_key_fails(self, tmp_path, monkeypatch):
        """Flat key missing entirely → FAIL (C++ will use hardcoded default)."""
        data = self._consistent_cfg()
        del data["trailing_drawdown_cap"]
        cfg = self._cfg(tmp_path, data)
        _redirect(monkeypatch, "CONFIG_PATH", cfg)

        findings = csa.check_risk_params_consistency()
        assert any(f["status"] == "FAIL" and "trailing_drawdown_cap" in f["message"]
                   for f in findings)

    def test_missing_nested_key_warns(self, tmp_path, monkeypatch):
        """prop_firm nested key missing → WARN (cannot verify flat vs nested)."""
        data = self._consistent_cfg()
        del data["prop_firm"]["trailing_drawdown_limit"]
        cfg = self._cfg(tmp_path, data)
        _redirect(monkeypatch, "CONFIG_PATH", cfg)

        findings = csa.check_risk_params_consistency()
        assert any(f["status"] == "WARN" and "trailing_drawdown_cap" in f["message"]
                   for f in findings)

    def test_missing_config_fails(self, tmp_path, monkeypatch):
        """Missing live_config.json → FAIL."""
        _redirect(monkeypatch, "CONFIG_PATH", tmp_path / "no_config.json")
        findings = csa.check_risk_params_consistency()
        assert findings[0]["status"] == "FAIL"
