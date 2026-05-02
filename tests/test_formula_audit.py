"""
tests/test_formula_audit.py — Unit tests for scripts/formula_audit.py.

Coverage:
  - _calc_pnl_usd produces correct gross PnL for LONG and SHORT directions
  - check_constants validates YAML constants against a known-good config
  - check_pnl_formulas runs all YAML test vectors and returns only PASSes
  - check_prop_firm_limits returns PASS for a sane prop-firm config
  - check_config_invariants returns PASS for a valid config
  - main() runs end-to-end on real files without error
  - --json flag emits valid JSON output
  - YAML missing → exits 0 (SKIP, not FAIL)
  - Config missing → exits 1 (FAIL)
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from unittest.mock import patch

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

# Import the module under test
import scripts.formula_audit as fa  # type: ignore[import]

pytestmark = pytest.mark.fast

# ── Minimal known-good data fixtures ─────────────────────────────────────────

_GOOD_RULES: dict = {
    "constants": {
        "POINT_VALUE": 2.0,
        "TICK_SIZE": 0.25,
        "TICK_VALUE": 0.50,
        "COMMISSION_PER_SIDE": 2.0,
        "COMMISSION_RT": 4.0,
    },
    "formulas": [
        {
            "id": "FORMULA-001",
            "name": "gross_pnl_long",
            "test_cases": [
                {"entry": 21000.0, "exit": 21010.0, "direction": "long", "qty": 1, "expected_gross": 20.0},
                {"entry": 21000.0, "exit": 20990.0, "direction": "long", "qty": 1, "expected_gross": -20.0},
                {"entry": 21000.0, "exit": 21000.0, "direction": "long", "qty": 1, "expected_gross": 0.0},
            ],
        },
        {
            "id": "FORMULA-002",
            "name": "gross_pnl_short",
            "test_cases": [
                {"entry": 21010.0, "exit": 21000.0, "direction": "short", "qty": 1, "expected_gross": 20.0},
                {"entry": 21000.0, "exit": 21010.0, "direction": "short", "qty": 1, "expected_gross": -20.0},
            ],
        },
        {
            "id": "FORMULA-003",
            "name": "net_pnl",
            "test_cases": [
                {"gross": 20.0, "commission_rt": 4.0, "expected_net": 16.0},
                {"gross": 0.0, "commission_rt": 4.0, "expected_net": -4.0},
                {"gross": -20.0, "commission_rt": 4.0, "expected_net": -24.0},
            ],
        },
        {
            "id": "FORMULA-004",
            "name": "pnl_points",
            "test_cases": [
                {"entry": 21000.0, "exit": 21010.0, "direction": "long", "expected_points": 10.0},
                {"entry": 21010.0, "exit": 21000.0, "direction": "short", "expected_points": 10.0},
                {"entry": 21000.0, "exit": 20990.0, "direction": "long", "expected_points": -10.0},
            ],
        },
        {
            "id": "FORMULA-005",
            "name": "ticks_from_points",
            "test_cases": [
                {"points": 10.0, "expected_ticks": 40.0},
                {"points": 0.25, "expected_ticks": 1.0},
                {"points": 15.0, "expected_ticks": 60.0},
            ],
        },
        {
            "id": "FORMULA-008",
            "name": "sl_price_long",
            "test_cases": [
                {"entry": 21000.0, "sl_points": 15.0, "direction": "long", "expected_sl": 20985.0},
            ],
        },
        {
            "id": "FORMULA-009",
            "name": "sl_price_short",
            "test_cases": [
                {"entry": 21000.0, "sl_points": 15.0, "direction": "short", "expected_sl": 21015.0},
            ],
        },
        {
            "id": "FORMULA-010",
            "name": "target_price_long",
            "test_cases": [
                {"entry": 21000.0, "target_points": 20.0, "direction": "long", "expected_target": 21020.0},
            ],
        },
        {
            "id": "FORMULA-011",
            "name": "target_price_short",
            "test_cases": [
                {"entry": 21000.0, "target_points": 20.0, "direction": "short", "expected_target": 20980.0},
            ],
        },
    ],
    "test_vectors": {
        "vectors": [
            {
                "id": "TV-001",
                "label": "Long winner",
                "entry": 21000.0,
                "exit": 21010.0,
                "direction": "long",
                "qty": 1,
                "gross_pnl": 20.0,
                "net_pnl": 16.0,
                "pnl_points": 10.0,
            },
            {
                "id": "TV-002",
                "label": "Short winner",
                "entry": 21010.0,
                "exit": 21000.0,
                "direction": "short",
                "qty": 1,
                "gross_pnl": 20.0,
                "net_pnl": 16.0,
                "pnl_points": 10.0,
            },
            {
                "id": "TV-003",
                "label": "Long loser",
                "entry": 21000.0,
                "exit": 20990.0,
                "direction": "long",
                "qty": 1,
                "gross_pnl": -20.0,
                "net_pnl": -24.0,
                "pnl_points": -10.0,
            },
        ],
    },
}

_GOOD_CFG: dict = {
    "dry_run": False,
    "trade_route": "Rithmic Order Routing",
    "trailing_drawdown_cap": 2500.0,
    "consistency_cap_pct": 0.3,
    "max_daily_trades": 3,
    "commission_rt": 4.0,
    "orb": {
        "point_value": 2.0,
        "tick_size": 0.25,
    },
    "prop_firm": {
        "daily_loss_limit": 2000.0,
        "trailing_drawdown_limit": 2500.0,
        "max_position_size": 3,
        "max_daily_trades": 3,
        "consistency_rule_pct": 0.3,
    },
}


# ── _calc_pnl_usd unit tests ──────────────────────────────────────────────────

@pytest.mark.fast
class TestCalcPnlUsd:
    """Direct tests of the PnL formula function — tick math for MNQ."""

    def test_long_winner(self):
        # 10 points * $2/point = $20
        assert fa._calc_pnl_usd(21000.0, 21010.0, "LONG", 2.0) == pytest.approx(20.0)

    def test_long_loser(self):
        # -10 points * $2/point = -$20
        assert fa._calc_pnl_usd(21000.0, 20990.0, "LONG", 2.0) == pytest.approx(-20.0)

    def test_long_breakeven(self):
        assert fa._calc_pnl_usd(21000.0, 21000.0, "LONG", 2.0) == pytest.approx(0.0)

    def test_short_winner(self):
        # entry - exit = 10 points * $2 = $20
        assert fa._calc_pnl_usd(21010.0, 21000.0, "SHORT", 2.0) == pytest.approx(20.0)

    def test_short_loser(self):
        # entry - exit = -10 points * $2 = -$20
        assert fa._calc_pnl_usd(21000.0, 21010.0, "SHORT", 2.0) == pytest.approx(-20.0)

    def test_direction_case_insensitive(self):
        # lowercase direction must work the same as uppercase
        assert fa._calc_pnl_usd(21000.0, 21010.0, "long", 2.0) == pytest.approx(20.0)
        assert fa._calc_pnl_usd(21010.0, 21000.0, "short", 2.0) == pytest.approx(20.0)

    def test_mnq_tick_value(self):
        # 1 tick = 0.25 points * $2/point = $0.50
        assert fa._calc_pnl_usd(21000.0, 21000.25, "LONG", 2.0) == pytest.approx(0.50)

    def test_mnq_not_nq_point_value(self):
        # MNQ point_value is 2.0, NOT 20.0 (NQ). Validate the difference.
        mnq = fa._calc_pnl_usd(21000.0, 21010.0, "LONG", 2.0)
        nq = fa._calc_pnl_usd(21000.0, 21010.0, "LONG", 20.0)
        assert mnq == pytest.approx(20.0)
        assert nq == pytest.approx(200.0)

    def test_sl_loss_15pts(self):
        # Default sl_points=15 → gross loss = -15 * 2 = -$30
        assert fa._calc_pnl_usd(21000.0, 20985.0, "LONG", 2.0) == pytest.approx(-30.0)


# ── check_constants tests ─────────────────────────────────────────────────────

@pytest.mark.fast
class TestCheckConstants:
    """check_constants validates YAML vs live_config orb section."""

    def test_all_pass_on_good_config(self):
        findings = fa.check_constants(_GOOD_RULES, _GOOD_CFG)
        fails = [f for f in findings if f["status"] == "FAIL"]
        assert fails == [], f"Unexpected failures: {fails}"
        passes = [f for f in findings if f["status"] == "PASS"]
        assert len(passes) >= 4  # point_value, tick_size, tick_value, commission_rt

    def test_wrong_point_value_fails(self):
        bad_cfg = {**_GOOD_CFG, "orb": {"point_value": 20.0, "tick_size": 0.25}}
        findings = fa.check_constants(_GOOD_RULES, bad_cfg)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "constants_point_value" in fail_names

    def test_wrong_tick_size_fails(self):
        bad_cfg = {**_GOOD_CFG, "orb": {"point_value": 2.0, "tick_size": 0.50}}
        findings = fa.check_constants(_GOOD_RULES, bad_cfg)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "constants_tick_size" in fail_names

    def test_missing_point_value_fails(self):
        bad_cfg = {**_GOOD_CFG, "orb": {"tick_size": 0.25}}
        findings = fa.check_constants(_GOOD_RULES, bad_cfg)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "constants_point_value" in fail_names

    def test_tick_value_derivation(self):
        # tick_value = 2.0 * 0.25 = 0.50 — must pass
        findings = fa.check_constants(_GOOD_RULES, _GOOD_CFG)
        tick_val = next(f for f in findings if f["check"] == "constants_tick_value")
        assert tick_val["status"] == "PASS"

    def test_commission_rt_double_per_side(self):
        findings = fa.check_constants(_GOOD_RULES, _GOOD_CFG)
        crt = next(f for f in findings if f["check"] == "constants_commission_rt")
        assert crt["status"] == "PASS"

    def test_bad_commission_rt_fails(self):
        bad_rules = {
            **_GOOD_RULES,
            "constants": {
                **_GOOD_RULES["constants"],
                "COMMISSION_PER_SIDE": 2.0,
                "COMMISSION_RT": 3.0,  # wrong — should be 4.0
            },
        }
        findings = fa.check_constants(bad_rules, _GOOD_CFG)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "constants_commission_rt" in fail_names


# ── check_pnl_formulas tests ──────────────────────────────────────────────────

@pytest.mark.fast
class TestCheckPnlFormulas:
    """check_pnl_formulas runs all YAML formula test vectors."""

    def test_no_failures_on_good_rules(self):
        findings = fa.check_pnl_formulas(_GOOD_RULES, _GOOD_CFG)
        fails = [f for f in findings if f["status"] == "FAIL"]
        assert fails == [], f"Formula check failures: {fails}"

    def test_produces_pass_findings(self):
        findings = fa.check_pnl_formulas(_GOOD_RULES, _GOOD_CFG)
        passes = [f for f in findings if f["status"] == "PASS"]
        assert len(passes) > 0

    def test_bad_expected_gross_fails(self):
        bad_rules = {
            **_GOOD_RULES,
            "formulas": [
                {
                    "id": "FORMULA-BAD",
                    "name": "bad_test",
                    "test_cases": [
                        # entry=21000 exit=21010 long qty=1 → should be $20, but we expect $99
                        {"entry": 21000.0, "exit": 21010.0, "direction": "long", "qty": 1, "expected_gross": 99.0},
                    ],
                }
            ],
            "test_vectors": {"vectors": []},
        }
        findings = fa.check_pnl_formulas(bad_rules, _GOOD_CFG)
        fails = [f for f in findings if f["status"] == "FAIL"]
        assert len(fails) == 1

    def test_bad_net_pnl_fails(self):
        bad_rules = {
            **_GOOD_RULES,
            "formulas": [
                {
                    "id": "FORMULA-NET-BAD",
                    "name": "net_bad",
                    "test_cases": [
                        # gross=20, commission=4 → net should be 16, but expect 18
                        {"gross": 20.0, "commission_rt": 4.0, "expected_net": 18.0},
                    ],
                }
            ],
            "test_vectors": {"vectors": []},
        }
        findings = fa.check_pnl_formulas(bad_rules, _GOOD_CFG)
        fails = [f for f in findings if f["status"] == "FAIL"]
        assert len(fails) == 1

    def test_ticks_conversion_correct(self):
        # 10 points / 0.25 = 40 ticks
        rules = {
            **_GOOD_RULES,
            "formulas": [
                {
                    "id": "FORMULA-TICK",
                    "name": "tick_conversion",
                    "test_cases": [{"points": 10.0, "expected_ticks": 40.0}],
                }
            ],
            "test_vectors": {"vectors": []},
        }
        findings = fa.check_pnl_formulas(rules, _GOOD_CFG)
        fails = [f for f in findings if f["status"] == "FAIL"]
        assert fails == []

    def test_golden_test_vectors_pass(self):
        # Only run the test_vectors section with a clean formulas list
        rules = {**_GOOD_RULES, "formulas": []}
        findings = fa.check_pnl_formulas(rules, _GOOD_CFG)
        fails = [f for f in findings if f["status"] == "FAIL"]
        assert fails == [], f"Golden vector failures: {fails}"

    def test_sl_price_long_calculation(self):
        rules = {
            **_GOOD_RULES,
            "formulas": [
                {
                    "id": "FORMULA-SL",
                    "name": "sl_long",
                    "test_cases": [
                        {"entry": 21000.0, "sl_points": 15.0, "direction": "long", "expected_sl": 20985.0},
                    ],
                }
            ],
            "test_vectors": {"vectors": []},
        }
        findings = fa.check_pnl_formulas(rules, _GOOD_CFG)
        assert all(f["status"] == "PASS" for f in findings)

    def test_target_price_short_calculation(self):
        rules = {
            **_GOOD_RULES,
            "formulas": [
                {
                    "id": "FORMULA-TGT",
                    "name": "target_short",
                    "test_cases": [
                        {"entry": 21000.0, "target_points": 20.0, "direction": "short", "expected_target": 20980.0},
                    ],
                }
            ],
            "test_vectors": {"vectors": []},
        }
        findings = fa.check_pnl_formulas(rules, _GOOD_CFG)
        assert all(f["status"] == "PASS" for f in findings)


# ── check_prop_firm_limits tests ──────────────────────────────────────────────

@pytest.mark.fast
class TestCheckPropFirmLimits:
    """check_prop_firm_limits validates prop-firm configuration sanity."""

    def test_good_config_passes(self):
        findings = fa.check_prop_firm_limits(_GOOD_CFG)
        fails = [f for f in findings if f["status"] == "FAIL"]
        assert fails == [], f"Unexpected failures: {fails}"

    def test_zero_daily_loss_fails(self):
        bad = {**_GOOD_CFG, "prop_firm": {**_GOOD_CFG["prop_firm"], "daily_loss_limit": 0}}
        findings = fa.check_prop_firm_limits(bad)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "prop_firm_daily_loss" in fail_names

    def test_trailing_dd_less_than_daily_loss_fails(self):
        # trailing_dd must be > daily_loss
        bad = {
            **_GOOD_CFG,
            "prop_firm": {
                **_GOOD_CFG["prop_firm"],
                "daily_loss_limit": 2000.0,
                "trailing_drawdown_limit": 1500.0,  # < daily_loss → FAIL
            },
        }
        findings = fa.check_prop_firm_limits(bad)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "prop_firm_trailing_dd" in fail_names

    def test_zero_max_position_fails(self):
        bad = {**_GOOD_CFG, "prop_firm": {**_GOOD_CFG["prop_firm"], "max_position_size": 0}}
        findings = fa.check_prop_firm_limits(bad)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "prop_firm_max_pos" in fail_names

    def test_consistency_pct_out_of_range_warns(self):
        bad = {**_GOOD_CFG, "prop_firm": {**_GOOD_CFG["prop_firm"], "consistency_rule_pct": 1.5}}
        findings = fa.check_prop_firm_limits(bad)
        warn_names = {f["check"] for f in findings if f["status"] == "WARN"}
        assert "prop_firm_consistency" in warn_names


# ── check_config_invariants tests ─────────────────────────────────────────────

@pytest.mark.fast
class TestCheckConfigInvariants:
    """check_config_invariants covers INV-006, INV-007, INV-008."""

    def test_good_config_passes(self):
        findings = fa.check_config_invariants(_GOOD_CFG)
        fails = [f for f in findings if f["status"] == "FAIL"]
        assert fails == [], f"Unexpected failures: {fails}"

    def test_simulator_route_fails(self):
        bad = {**_GOOD_CFG, "trade_route": "simulator"}
        findings = fa.check_config_invariants(bad)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "trade_route_not_simulator" in fail_names

    def test_missing_trailing_drawdown_cap_fails(self):
        bad = {k: v for k, v in _GOOD_CFG.items() if k != "trailing_drawdown_cap"}
        findings = fa.check_config_invariants(bad)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "risk_flat_trailing_drawdown_cap" in fail_names

    def test_mismatched_max_daily_trades_fails(self):
        bad = {**_GOOD_CFG, "max_daily_trades": 99}
        findings = fa.check_config_invariants(bad)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "max_daily_trades_match" in fail_names

    def test_missing_flat_max_daily_trades_fails(self):
        bad = {k: v for k, v in _GOOD_CFG.items() if k != "max_daily_trades"}
        findings = fa.check_config_invariants(bad)
        fail_names = {f["check"] for f in findings if f["status"] == "FAIL"}
        assert "max_daily_trades_match" in fail_names


# ── main() / CLI end-to-end tests ─────────────────────────────────────────────

@pytest.mark.fast
class TestMainFunction:
    """End-to-end tests for the main() entry point and CLI flags."""

    def _run_script(self, *args: str) -> subprocess.CompletedProcess:
        """Run formula_audit.py as a subprocess using the real config files."""
        cmd = [sys.executable, str(REPO_ROOT / "scripts" / "formula_audit.py"), *args]
        return subprocess.run(cmd, capture_output=True, text=True, timeout=10)

    def test_main_runs_without_error_on_real_config(self):
        """Audit runs end-to-end on the real YAML + live_config.json."""
        result = self._run_script()
        # Exit code 0 = all PASSes, exit code 1 = some FAILs — both are acceptable;
        # what matters is it does not crash (exit 2+ from an exception).
        assert result.returncode in (0, 1), (
            f"formula_audit exited with unexpected code {result.returncode}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

    def test_json_flag_produces_valid_json(self):
        """--json flag must emit a parseable JSON array."""
        result = self._run_script("--json")
        assert result.returncode in (0, 1)
        try:
            data = json.loads(result.stdout)
        except json.JSONDecodeError as exc:
            pytest.fail(f"--json output is not valid JSON: {exc}\nOutput: {result.stdout!r}")
        assert isinstance(data, list), f"Expected JSON array, got: {type(data)}"
        assert len(data) > 0, "JSON output array is empty"

    def test_json_output_has_required_fields(self):
        """Each item in the JSON array has check/status/message/severity keys."""
        result = self._run_script("--json")
        data = json.loads(result.stdout)
        for item in data:
            assert "check" in item, f"Missing 'check' in: {item}"
            assert "status" in item, f"Missing 'status' in: {item}"
            assert "message" in item, f"Missing 'message' in: {item}"
            assert "severity" in item, f"Missing 'severity' in: {item}"

    def test_json_status_values_are_valid(self):
        """All status values must be one of PASS, FAIL, WARN, SKIP."""
        result = self._run_script("--json")
        data = json.loads(result.stdout)
        valid_statuses = {"PASS", "FAIL", "WARN", "SKIP"}
        for item in data:
            assert item["status"] in valid_statuses, (
                f"Unexpected status '{item['status']}' in: {item}"
            )

    def test_missing_yaml_exits_zero(self):
        """If YAML not found, script exits 0 (SKIP) — not a hard failure."""
        import io
        from contextlib import redirect_stdout

        with patch.object(fa, "RULES_PATH", Path("/nonexistent/formula_audit.yaml")), \
             patch.object(fa, "CONFIG_PATH", fa.CONFIG_PATH), \
             patch("sys.argv", ["formula_audit.py"]):
            buf = io.StringIO()
            with pytest.raises(SystemExit) as exc_info:
                with redirect_stdout(buf):
                    fa.main()
        assert exc_info.value.code == 0

    def test_missing_config_exits_one(self):
        """If live_config.json not found, script exits 1 (FAIL)."""
        import io
        from contextlib import redirect_stdout

        with patch.object(fa, "RULES_PATH", fa.RULES_PATH), \
             patch.object(fa, "CONFIG_PATH", Path("/nonexistent/live_config.json")), \
             patch("sys.argv", ["formula_audit.py"]):
            buf = io.StringIO()
            with pytest.raises(SystemExit) as exc_info:
                with redirect_stdout(buf):
                    fa.main()
        assert exc_info.value.code == 1

    def test_all_pass_config_exits_zero(self):
        """When all checks pass, main() exits 0."""
        import io
        from contextlib import redirect_stdout

        # Patch load_rules and load_live_config so no real I/O occurs
        with patch.object(fa, "load_rules", return_value=_GOOD_RULES), \
             patch.object(fa, "load_live_config", return_value=_GOOD_CFG), \
             patch("sys.argv", ["formula_audit.py"]):
            buf = io.StringIO()
            with pytest.raises(SystemExit) as exc_info:
                with redirect_stdout(buf):
                    fa.main()
        assert exc_info.value.code == 0

    def test_json_all_pass_exits_zero(self):
        """--json with all-pass config exits 0 and produces valid JSON."""
        import io
        from contextlib import redirect_stdout

        with patch.object(fa, "load_rules", return_value=_GOOD_RULES), \
             patch.object(fa, "load_live_config", return_value=_GOOD_CFG), \
             patch("sys.argv", ["formula_audit.py", "--json"]):
            buf = io.StringIO()
            with pytest.raises(SystemExit) as exc_info:
                with redirect_stdout(buf):
                    fa.main()
        assert exc_info.value.code == 0
        data = json.loads(buf.getvalue())
        assert isinstance(data, list)
        assert all(item["status"] == "PASS" for item in data), (
            f"Expected all PASS, got: {[i for i in data if i['status'] != 'PASS']}"
        )
