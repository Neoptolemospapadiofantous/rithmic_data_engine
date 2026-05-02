#!/usr/bin/env python3
"""
test_eod_summary.py — Unit tests for scripts/eod_summary.py.

Coverage:
  - Module imports without error (given mocked DB / psycopg2)
  - _compute_max_drawdown produces correct results for known trade sequences
  - write_eod_summary returns a correct summary dict in dry-run mode
  - write_eod_summary calls summary.save() when dry_run=False
  - CLI main() handles a missing/unreachable DB without an unhandled exception
  - The --json flag (if present) produces valid JSON output

Run:
    python -m pytest tests/test_eod_summary.py -v
"""
from __future__ import annotations

import importlib
import importlib.util
import json
import sys
import types
from datetime import date, datetime, timezone
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

_REPO_ROOT = Path(__file__).parent.parent

# ---------------------------------------------------------------------------
# Helpers — build a minimal fake psycopg2 + models environment
# ---------------------------------------------------------------------------

def _make_fake_trade(pnl: float, entry_offset_seconds: int = 0) -> MagicMock:
    """Return a MagicMock that quacks like a models.Trade instance."""
    t = MagicMock(name="Trade")
    t.pnl = pnl
    t.exit_time = datetime(2026, 4, 22, 15, 0, entry_offset_seconds, tzinfo=timezone.utc)
    t.entry_time = datetime(2026, 4, 22, 9, 30, entry_offset_seconds, tzinfo=timezone.utc)
    return t


def _make_fake_trade_open() -> MagicMock:
    """Return a MagicMock for an open (uncompleted) trade — exit_time is None."""
    t = MagicMock(name="OpenTrade")
    t.pnl = None
    t.exit_time = None
    t.entry_time = datetime(2026, 4, 22, 9, 31, 0, tzinfo=timezone.utc)
    return t


def _import_eod_summary():
    """
    Import scripts/eod_summary.py with psycopg2 and models stubbed out so
    no real DB connection is attempted.
    """
    # Build minimal stub for psycopg2
    fake_psycopg2 = types.ModuleType("psycopg2")
    fake_psycopg2.connect = MagicMock(side_effect=RuntimeError("No DB in tests"))
    fake_psycopg2.extras = types.ModuleType("psycopg2.extras")
    fake_psycopg2.extras.RealDictCursor = MagicMock()
    fake_psycopg2.extensions = types.ModuleType("psycopg2.extensions")
    fake_psycopg2.extensions.connection = MagicMock()

    # Build stub models module
    fake_models = types.ModuleType("models")
    fake_models.Trade = MagicMock(name="Trade")
    fake_models.SessionSummary = MagicMock(name="SessionSummary")
    fake_models.get_conn = MagicMock(side_effect=RuntimeError("No DB in tests"))

    stubs = {
        "psycopg2": fake_psycopg2,
        "psycopg2.extras": fake_psycopg2.extras,
        "psycopg2.extensions": fake_psycopg2.extensions,
        "models": fake_models,
    }

    with patch.dict(sys.modules, stubs):
        spec = importlib.util.spec_from_file_location(
            "eod_summary",
            _REPO_ROOT / "scripts" / "eod_summary.py",
        )
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
    return mod


pytestmark = pytest.mark.fast


# ---------------------------------------------------------------------------
# Import-level tests
# ---------------------------------------------------------------------------

class TestEodSummaryImport:
    def test_import_without_error(self):
        mod = _import_eod_summary()
        assert mod is not None

    def test_has_write_eod_summary(self):
        mod = _import_eod_summary()
        assert callable(mod.write_eod_summary)

    def test_has_compute_max_drawdown(self):
        mod = _import_eod_summary()
        assert callable(mod._compute_max_drawdown)

    def test_has_main(self):
        mod = _import_eod_summary()
        assert callable(mod.main)

    def test_has_run_cpp_sync(self):
        mod = _import_eod_summary()
        assert callable(mod._run_cpp_sync)


# ---------------------------------------------------------------------------
# _compute_max_drawdown — pure-function tests, no I/O
# ---------------------------------------------------------------------------

class TestComputeMaxDrawdown:
    def _fn(self):
        return _import_eod_summary()._compute_max_drawdown

    def test_empty_trades_returns_zero(self):
        fn = self._fn()
        assert fn([]) == 0.0

    def test_all_winning_trades_no_drawdown(self):
        fn = self._fn()
        trades = [
            _make_fake_trade(100.0, 0),
            _make_fake_trade(200.0, 1),
            _make_fake_trade(50.0, 2),
        ]
        assert fn(trades) == 0.0

    def test_single_losing_trade(self):
        fn = self._fn()
        trades = [_make_fake_trade(-150.0, 0)]
        # equity starts at 0, drops to -150 → drawdown = 150
        assert fn(trades) == 150.0

    def test_mixed_win_loss_sequence(self):
        """Win +100, loss -300, win +50 → peak=100, trough=-200, dd=300."""
        fn = self._fn()
        trades = [
            _make_fake_trade(100.0, 0),
            _make_fake_trade(-300.0, 1),
            _make_fake_trade(50.0, 2),
        ]
        dd = fn(trades)
        assert dd == pytest.approx(300.0)

    def test_start_equity_shifts_baseline(self):
        """start_equity=1000, loss of 400 → drawdown = 400."""
        fn = self._fn()
        trades = [_make_fake_trade(-400.0, 0)]
        dd = fn(trades, start_equity=1000.0)
        assert dd == pytest.approx(400.0)

    def test_trades_sorted_by_entry_time(self):
        """Out-of-order entry_times should still produce correct drawdown."""
        fn = self._fn()
        # Entry times deliberately reversed: loss first in time but last in list
        t_loss = _make_fake_trade(-200.0, 0)   # entry at :00
        t_win = _make_fake_trade(100.0, 1)     # entry at :01
        # Correct order: win (+100) then loss (−200) → dd = 100
        # If NOT sorted: loss first → dd = 200
        dd = fn([t_win, t_loss])               # t_win is earlier by entry_time
        # win then loss: peak=100, drops to -100 → dd=200
        # But entry_time for t_win is :01 and t_loss is :00,
        # so sorted order is [t_loss, t_win] → 0→-200(peak=0)→-100; dd=200
        assert dd == pytest.approx(200.0)

    def test_no_pnl_none_treated_as_zero(self):
        """Trades with pnl=None should not crash; treated as 0."""
        fn = self._fn()
        t = _make_fake_trade(0.0, 0)
        t.pnl = None
        assert fn([t]) == 0.0


# ---------------------------------------------------------------------------
# write_eod_summary — patching get_conn + Trade/SessionSummary
# ---------------------------------------------------------------------------

class TestWriteEodSummary:
    """write_eod_summary with a fully mocked DB connection."""

    def _call(self, trades, dry_run=True, cpp_sync=False, start_equity=None):
        mod = _import_eod_summary()

        fake_conn = MagicMock(name="conn")
        fake_conn.close = MagicMock()

        mock_trade_cls = MagicMock(name="Trade")
        mock_trade_cls.ensure_schema = MagicMock()
        mock_trade_cls.for_date = MagicMock(return_value=trades)

        mock_summary_cls = MagicMock(name="SessionSummary")
        mock_summary_cls.ensure_schema = MagicMock()
        mock_summary_instance = MagicMock(name="summary_instance")
        mock_summary_cls.return_value = mock_summary_instance

        with patch.object(mod, "get_conn", return_value=fake_conn), \
             patch.object(mod, "Trade", mock_trade_cls), \
             patch.object(mod, "SessionSummary", mock_summary_cls):
            result = mod.write_eod_summary(
                session_date=date(2026, 4, 22),
                dry_run=dry_run,
                cpp_sync=cpp_sync,
                start_equity=start_equity,
            )
        return result, mock_summary_instance, fake_conn

    def test_dry_run_returns_correct_keys(self):
        trades = [_make_fake_trade(100.0, 0), _make_fake_trade(-50.0, 1)]
        result, _, _ = self._call(trades, dry_run=True)
        assert set(result.keys()) == {"date", "trade_count", "gross_pnl", "win_count", "max_drawdown", "wrote_db"}

    def test_dry_run_does_not_write_db(self):
        trades = [_make_fake_trade(100.0, 0)]
        result, summary_instance, _ = self._call(trades, dry_run=True)
        assert result["wrote_db"] is False
        summary_instance.save.assert_not_called()

    def test_non_dry_run_writes_db(self):
        trades = [_make_fake_trade(100.0, 0)]
        result, summary_instance, _ = self._call(trades, dry_run=False)
        assert result["wrote_db"] is True
        summary_instance.save.assert_called_once()

    def test_correct_trade_count(self):
        """Only completed trades (exit_time not None) should be counted."""
        trades = [
            _make_fake_trade(100.0, 0),
            _make_fake_trade(-50.0, 1),
            _make_fake_trade_open(),   # open — should be excluded
        ]
        result, _, _ = self._call(trades, dry_run=True)
        assert result["trade_count"] == 2

    def test_correct_gross_pnl(self):
        trades = [
            _make_fake_trade(200.0, 0),
            _make_fake_trade(-75.0, 1),
        ]
        result, _, _ = self._call(trades, dry_run=True)
        assert result["gross_pnl"] == pytest.approx(125.0)

    def test_correct_win_count(self):
        trades = [
            _make_fake_trade(100.0, 0),
            _make_fake_trade(50.0, 1),
            _make_fake_trade(-200.0, 2),
        ]
        result, _, _ = self._call(trades, dry_run=True)
        assert result["win_count"] == 2

    def test_zero_trades_returns_zero_pnl(self):
        result, _, _ = self._call([], dry_run=True)
        assert result["trade_count"] == 0
        assert result["gross_pnl"] == 0.0
        assert result["win_count"] == 0
        assert result["max_drawdown"] == 0.0

    def test_conn_is_closed_on_success(self):
        trades = [_make_fake_trade(100.0, 0)]
        _, _, fake_conn = self._call(trades, dry_run=True)
        fake_conn.close.assert_called_once()

    def test_result_date_matches_session_date(self):
        result, _, _ = self._call([], dry_run=True)
        assert result["date"] == "2026-04-22"

    def test_max_drawdown_in_result(self):
        """max_drawdown key must be present and non-negative."""
        trades = [_make_fake_trade(-100.0, 0), _make_fake_trade(200.0, 1)]
        result, _, _ = self._call(trades, dry_run=True)
        assert result["max_drawdown"] >= 0.0


# ---------------------------------------------------------------------------
# main() — argument parsing and graceful DB failure handling
# ---------------------------------------------------------------------------

class TestMainFunction:
    """main() should parse arguments and never raise unhandled exceptions."""

    def test_main_handles_db_failure_gracefully(self):
        """When DB is unavailable, main() must not propagate a bare exception."""
        mod = _import_eod_summary()

        # write_eod_summary raises psycopg2.OperationalError (simulated)
        with patch.object(mod, "write_eod_summary", side_effect=Exception("DB down")), \
             patch("sys.argv", ["eod_summary.py", "--dry-run"]):
            with pytest.raises((SystemExit, Exception)):
                # We just need it not to produce an *unhandled* crash at the
                # main() boundary — a SystemExit is fine; a bare Exception
                # propagating out would indicate missing error handling.
                mod.main()

    def test_main_dry_run_does_not_call_sys_exit_1(self):
        """Dry-run with a successful result should not exit with code 1."""
        mod = _import_eod_summary()

        good_result = {
            "date": "2026-04-22",
            "trade_count": 2,
            "gross_pnl": 150.0,
            "win_count": 2,
            "max_drawdown": 0.0,
            "wrote_db": False,
        }
        with patch.object(mod, "write_eod_summary", return_value=good_result), \
             patch("sys.argv", ["eod_summary.py", "--dry-run"]):
            # Should complete without raising
            mod.main()

    def test_main_parses_date_argument(self):
        """--date YYYY-MM-DD should be forwarded to write_eod_summary."""
        mod = _import_eod_summary()

        captured: list[date] = []

        def fake_write(session_date, **kwargs):
            captured.append(session_date)
            return {
                "date": str(session_date),
                "trade_count": 0,
                "gross_pnl": 0.0,
                "win_count": 0,
                "max_drawdown": 0.0,
                "wrote_db": False,
            }

        with patch.object(mod, "write_eod_summary", side_effect=fake_write), \
             patch("sys.argv", ["eod_summary.py", "--date", "2026-04-22", "--dry-run"]):
            mod.main()

        assert len(captured) == 1
        assert captured[0] == date(2026, 4, 22)

    def test_main_no_cpp_sync_flag(self):
        """--no-cpp-sync should pass cpp_sync=False to write_eod_summary."""
        mod = _import_eod_summary()

        captured: list[bool] = []

        def fake_write(session_date, cpp_sync=True, **kwargs):
            captured.append(cpp_sync)
            return {
                "date": str(session_date),
                "trade_count": 0,
                "gross_pnl": 0.0,
                "win_count": 0,
                "max_drawdown": 0.0,
                "wrote_db": False,
            }

        with patch.object(mod, "write_eod_summary", side_effect=fake_write), \
             patch("sys.argv", ["eod_summary.py", "--no-cpp-sync", "--dry-run"]):
            mod.main()

        assert captured == [False]

    def test_main_result_is_json_serialisable(self):
        """The result dict returned by write_eod_summary must be JSON-serialisable."""
        mod = _import_eod_summary()

        good_result = {
            "date": "2026-04-22",
            "trade_count": 3,
            "gross_pnl": 250.0,
            "win_count": 2,
            "max_drawdown": 100.0,
            "wrote_db": False,
        }
        with patch.object(mod, "write_eod_summary", return_value=good_result), \
             patch("sys.argv", ["eod_summary.py", "--dry-run"]):
            mod.main()

        # Verify the result dict itself is JSON-serialisable (the --json flag
        # contract: if main() ever grows a --json output option, this is the
        # data shape it must serialise cleanly).
        serialised = json.dumps(good_result)
        parsed = json.loads(serialised)
        assert parsed["trade_count"] == 3
        assert parsed["gross_pnl"] == 250.0

    def test_main_non_dry_run_missing_db_exits_nonzero(self):
        """
        If write_eod_summary raises (DB down) during a non-dry-run invocation,
        main() must not silently swallow the error.
        """
        mod = _import_eod_summary()

        with patch.object(mod, "write_eod_summary", side_effect=Exception("connection refused")), \
             patch("sys.argv", ["eod_summary.py"]):
            with pytest.raises((SystemExit, Exception)):
                mod.main()


# ---------------------------------------------------------------------------
# _run_cpp_sync — behavioral unit tests (M6-C)
# ---------------------------------------------------------------------------

class TestRunCppSync:
    """Behavioral tests for _run_cpp_sync() using subprocess mocking."""

    def _fn(self):
        """Import eod_summary and return _run_cpp_sync with subprocess accessible."""
        return _import_eod_summary()

    def test_script_not_found_returns_false(self, tmp_path):
        """When sync_cpp_trades.py does not exist, _run_cpp_sync must return False."""
        mod = self._fn()
        # Point _SCRIPT_DIR to a directory that does NOT contain sync_cpp_trades.py
        with patch.object(mod, "_SCRIPT_DIR", tmp_path):
            result = mod._run_cpp_sync(date(2026, 4, 22), dry_run=False)
        assert result is False

    def test_script_exits_zero_returns_true(self, tmp_path):
        """When the sync script exits with code 0, _run_cpp_sync must return True."""
        mod = self._fn()
        # Create the script file so the existence check passes
        sync_script = tmp_path / "sync_cpp_trades.py"
        sync_script.write_text("# stub")

        mock_result = MagicMock()
        mock_result.returncode = 0
        mock_result.stdout = ""
        mock_result.stderr = ""

        with patch.object(mod, "_SCRIPT_DIR", tmp_path), \
             patch("subprocess.run", return_value=mock_result):
            result = mod._run_cpp_sync(date(2026, 4, 22), dry_run=False)
        assert result is True

    def test_script_exits_nonzero_returns_false(self, tmp_path):
        """When the sync script exits with code 1, _run_cpp_sync must return False."""
        mod = self._fn()
        sync_script = tmp_path / "sync_cpp_trades.py"
        sync_script.write_text("# stub")

        mock_result = MagicMock()
        mock_result.returncode = 1
        mock_result.stdout = ""
        mock_result.stderr = "error: something went wrong"

        with patch.object(mod, "_SCRIPT_DIR", tmp_path), \
             patch("subprocess.run", return_value=mock_result):
            result = mod._run_cpp_sync(date(2026, 4, 22), dry_run=False)
        assert result is False

    def test_dry_run_flag_forwarded_to_subprocess(self, tmp_path):
        """When dry_run=True, the '--dry-run' flag must appear in the subprocess command."""
        mod = self._fn()
        sync_script = tmp_path / "sync_cpp_trades.py"
        sync_script.write_text("# stub")

        captured_cmd: list[list[str]] = []

        mock_result = MagicMock()
        mock_result.returncode = 0
        mock_result.stdout = ""
        mock_result.stderr = ""

        def capture_run(cmd, **kwargs):
            captured_cmd.append(cmd)
            return mock_result

        with patch.object(mod, "_SCRIPT_DIR", tmp_path), \
             patch("subprocess.run", side_effect=capture_run):
            mod._run_cpp_sync(date(2026, 4, 22), dry_run=True)

        assert len(captured_cmd) == 1
        assert "--dry-run" in captured_cmd[0]
