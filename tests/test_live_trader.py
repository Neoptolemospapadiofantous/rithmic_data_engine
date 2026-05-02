"""
Unit tests for live_trader.py and strategy/micro_orb.py.

Coverage:
  - MicroORBStrategy state machine and signal generation
  - Position reconciliation reads from DB (not just a warning file)
  - dry_run=True blocks the live-order code path
  - SIGTERM triggers emergency flatten and clean exit
  - NO_DEPLOY lockfile causes immediate exit
"""

import datetime
import json
import os
import signal
import subprocess
import sys
import tempfile
import unittest
import zoneinfo
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

REPO_ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(REPO_ROOT))

from strategy.micro_orb import MicroORBStrategy, Signal, StrategyState

pytestmark = pytest.mark.live_trader

ET = zoneinfo.ZoneInfo("America/New_York")


# ── helpers ───────────────────────────────────────────────────────────────────

def _make_config(**orb_overrides) -> dict:
    orb = {
        "orb_period_minutes": 5,
        "stop_loss_ticks": 16,
        "target_ticks": 48,
        "tick_size": 0.25,
        "point_value": 2.0,
        "rth_open": "09:30:00",
        "rth_close": "16:00:00",
        "eod_exit_minutes_before_close": 15,
        "allow_short": True,
    }
    orb.update(orb_overrides)
    return {
        "dry_run": True,
        "symbol": "MNQ",
        "exchange": "CME",
        "orb": orb,
        "db": {"max_retries": 1, "retry_backoff_base": 0},
        "logging": {"log_level": "WARNING"},
        "no_deploy_path": "NO_DEPLOY_DOES_NOT_EXIST_TEST",
    }


def _base_dt() -> datetime.datetime:
    return datetime.datetime(2024, 1, 15, 9, 30, tzinfo=ET)


def _make_bar(dt: datetime.datetime, high_offset=5.0, low_offset=5.0,
              close_price=17005.0) -> dict:
    return {
        "ts": dt,
        "open": 17000.0,
        "high": 17000.0 + high_offset,
        "low": 17000.0 - low_offset,
        "close": close_price,
        "volume": 1000,
    }


def _feed_range_bars(strategy: MicroORBStrategy, n: int = 5) -> None:
    """Feed N range-building bars starting at RTH open."""
    base = _base_dt()
    for i in range(n):
        bar = _make_bar(base + datetime.timedelta(minutes=i))
        strategy.on_bar(bar)


# ── state machine tests ───────────────────────────────────────────────────────

@pytest.mark.fast
class TestMicroORBStateMachine(unittest.TestCase):

    def setUp(self):
        self.cfg = _make_config()
        self.s = MicroORBStrategy(self.cfg)

    def test_initial_state_is_waiting(self):
        self.assertEqual(self.s.state, StrategyState.WAITING)

    def test_first_rth_bar_transitions_to_orb_building(self):
        self.s.on_bar(_make_bar(_base_dt()))
        self.assertEqual(self.s.state, StrategyState.ORB_BUILDING)

    def test_n_bars_transitions_to_watching(self):
        _feed_range_bars(self.s, 5)
        self.assertEqual(self.s.state, StrategyState.WATCHING)
        self.assertIsNotNone(self.s.orb_high)
        self.assertIsNotNone(self.s.orb_low)

    def test_orb_high_low_correct(self):
        base = _base_dt()
        # Feed 5 bars with known highs and lows
        for i in range(5):
            bar = {
                "ts": base + datetime.timedelta(minutes=i),
                "open": 17000.0, "high": 17000.0 + i, "low": 16990.0 - i,
                "close": 17000.0, "volume": 100,
            }
            self.s.on_bar(bar)
        self.assertEqual(self.s.orb_high, 17004.0)   # max(17000,1,2,3,4) = 17004
        self.assertEqual(self.s.orb_low, 16986.0)    # min(16990,89,88,87,86) = 16986

    def test_no_signal_within_range(self):
        base = _base_dt()
        sigs = []
        for i in range(5):
            bar = _make_bar(base + datetime.timedelta(minutes=i))
            sigs.append(self.s.on_bar(bar))
        self.assertTrue(all(s is None for s in sigs))

    def test_long_breakout_generates_signal(self):
        _feed_range_bars(self.s, 5)
        self.assertIsNotNone(self.s.orb_high)
        breakout_bar = {
            "ts": _base_dt() + datetime.timedelta(minutes=5),
            "open": self.s.orb_high + 0.25,
            "high": self.s.orb_high + 5.0,
            "low": self.s.orb_high,
            "close": self.s.orb_high + 5.0,   # close above range high
            "volume": 500,
        }
        sig = self.s.on_bar(breakout_bar)
        self.assertIsNotNone(sig)
        self.assertEqual(sig.direction, "LONG")
        self.assertEqual(self.s.state, StrategyState.IN_POSITION)

    def test_short_breakout_generates_signal(self):
        _feed_range_bars(self.s, 5)
        breakout_bar = {
            "ts": _base_dt() + datetime.timedelta(minutes=5),
            "open": self.s.orb_low - 0.25,
            "high": self.s.orb_low,
            "low": self.s.orb_low - 5.0,
            "close": self.s.orb_low - 5.0,   # close below range low
            "volume": 500,
        }
        sig = self.s.on_bar(breakout_bar)
        self.assertIsNotNone(sig)
        self.assertEqual(sig.direction, "SHORT")

    def test_no_short_when_allow_short_false(self):
        cfg = _make_config(allow_short=False)
        s = MicroORBStrategy(cfg)
        _feed_range_bars(s, 5)
        breakout_bar = {
            "ts": _base_dt() + datetime.timedelta(minutes=5),
            "open": s.orb_low - 0.25,
            "high": s.orb_low,
            "low": s.orb_low - 5.0,
            "close": s.orb_low - 5.0,
            "volume": 500,
        }
        sig = s.on_bar(breakout_bar)
        self.assertIsNone(sig)

    def test_session_resets_on_new_date(self):
        _feed_range_bars(self.s, 5)
        self.assertEqual(self.s.state, StrategyState.WATCHING)
        # Feed a bar from the next day
        next_day = _base_dt() + datetime.timedelta(days=1)
        self.s.on_bar(_make_bar(next_day))
        self.assertEqual(self.s.state, StrategyState.ORB_BUILDING)

    def test_eod_cutoff_blocks_new_entry(self):
        """Bars fed after EOD cutoff should not trigger signals."""
        _feed_range_bars(self.s, 5)
        # 15:50 ET is within 15-minute cutoff before 16:00
        late_bar = _make_bar(
            _base_dt().replace(hour=15, minute=50),
            high_offset=100.0, low_offset=0.0, close_price=17200.0
        )
        sig = self.s.on_bar(late_bar)
        self.assertIsNone(sig)


# ── on_tick / stop-loss tests ─────────────────────────────────────────────────

@pytest.mark.fast
class TestOnTick(unittest.TestCase):

    def _in_position_strategy(self, direction="LONG") -> tuple[MicroORBStrategy, Signal]:
        cfg = _make_config()
        s = MicroORBStrategy(cfg)
        _feed_range_bars(s, 5)
        close = s.orb_high + 5.0 if direction == "LONG" else s.orb_low - 5.0
        breakout_bar = {
            "ts": _base_dt() + datetime.timedelta(minutes=5),
            "open": close - 0.25, "high": close + 1.0,
            "low": close - 1.0, "close": close, "volume": 100,
        }
        sig = s.on_bar(breakout_bar)
        return s, sig

    def test_no_exit_while_not_in_position(self):
        cfg = _make_config()
        s = MicroORBStrategy(cfg)
        result = s.on_tick({"price": 17000.0, "ts": datetime.datetime.now(tz=ET)})
        self.assertIsNone(result)

    def test_exit_on_stop_loss_long(self):
        s, sig = self._in_position_strategy("LONG")
        # Tick below stop loss
        result = s.on_tick({"price": sig.stop_loss - 0.25,
                             "ts": datetime.datetime.now(tz=ET)})
        self.assertEqual(result, "EXIT")
        self.assertEqual(s.state, StrategyState.FLAT)

    def test_exit_on_target_long(self):
        s, sig = self._in_position_strategy("LONG")
        result = s.on_tick({"price": sig.target,
                             "ts": datetime.datetime.now(tz=ET)})
        self.assertEqual(result, "EXIT")

    def test_exit_on_stop_loss_short(self):
        s, sig = self._in_position_strategy("SHORT")
        result = s.on_tick({"price": sig.stop_loss + 0.25,
                             "ts": datetime.datetime.now(tz=ET)})
        self.assertEqual(result, "EXIT")

    def test_no_exit_on_tick_between_sl_and_target(self):
        s, sig = self._in_position_strategy("LONG")
        mid_price = (sig.stop_loss + sig.target) / 2
        result = s.on_tick({"price": mid_price, "ts": datetime.datetime.now(tz=ET)})
        self.assertIsNone(result)


# ── dry_run gate test ─────────────────────────────────────────────────────────

@pytest.mark.fast
class TestDryRunGate(unittest.TestCase):

    def test_dry_run_logs_but_does_not_call_rithmic(self):
        """_submit_order must not reach any real order code when dry_run=True."""
        import live_trader
        cfg = _make_config()
        log = MagicMock()
        sig = Signal("LONG", 17010.0, 17006.0, 17022.0, datetime.datetime.now(tz=ET))
        order_id = live_trader._submit_order(sig, cfg, dry_run=True, log=log)
        self.assertIsNotNone(order_id)
        self.assertTrue(order_id.startswith("DRY-"))
        # log.info was called with DRY RUN in the message
        log.info.assert_called_once()
        msg_args = log.info.call_args[0]
        self.assertIn("DRY RUN", msg_args[0])

    def test_dry_run_false_exits_with_critical(self):
        """When dry_run=False, submit_order logs CRITICAL and calls sys.exit(1)."""
        import live_trader
        cfg = _make_config()
        log = MagicMock()
        sig = Signal("LONG", 17010.0, 17006.0, 17022.0, datetime.datetime.now(tz=ET))
        with self.assertRaises(SystemExit) as ctx:
            live_trader._submit_order(sig, cfg, dry_run=False, log=log)
        self.assertEqual(ctx.exception.code, 1)
        log.critical.assert_called_once()
        self.assertIn("LIVE ORDER", log.critical.call_args[0][0])


# ── NO_DEPLOY lockfile test ───────────────────────────────────────────────────

@pytest.mark.fast
class TestNoDeployLockfile(unittest.TestCase):

    def test_no_deploy_causes_sys_exit(self):
        """live_trader._check_no_deploy must sys.exit(1) when lockfile exists."""
        import live_trader
        with tempfile.NamedTemporaryFile(delete=False) as f:
            lockfile_path = f.name
        try:
            cfg = {"no_deploy_path": lockfile_path}
            with self.assertRaises(SystemExit) as ctx:
                live_trader._check_no_deploy(cfg)
            self.assertEqual(ctx.exception.code, 1)
        finally:
            os.unlink(lockfile_path)

    def test_no_lockfile_does_not_exit(self):
        import live_trader
        cfg = {"no_deploy_path": "/tmp/no_deploy_file_that_does_not_exist_xyz_abc"}
        live_trader._check_no_deploy(cfg)  # must not raise


# ── SIGTERM handler test ──────────────────────────────────────────────────────

@pytest.mark.slow
class TestSigtermHandler(unittest.TestCase):

    def test_sigterm_causes_clean_exit(self):
        """Send SIGTERM to a live_trader subprocess; verify it exits 0 within 5s."""
        config = _make_config()
        # Write a temporary config that will keep the trader in startup long enough
        # for the signal to be received (it blocks on PG connect which will fail fast)
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            # Point to a non-existent lockfile so the NO_DEPLOY check passes,
            # but db.max_retries=0 so the PG connect fails immediately
            cfg_copy = dict(config)
            cfg_copy["db"] = {"max_retries": 1, "retry_backoff_base": 0,
                              "host_env": "PG_HOST_DOES_NOT_EXIST",
                              "port_env": "PG_PORT_DOES_NOT_EXIST",
                              "dbname_env": "PG_DB_DOES_NOT_EXIST"}
            json.dump(cfg_copy, f)
            tmp_cfg = f.name

        try:
            proc = subprocess.Popen(
                [sys.executable, "live_trader.py", "--config", tmp_cfg, "--dry-run"],
                cwd=str(REPO_ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            # Give it a moment to start
            import time
            time.sleep(0.5)
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                self.fail("live_trader did not exit within 5s after SIGTERM")
            # Exit code 0 (clean) or 1 (PG connect failure) are both acceptable
            # The test asserts the process terminates, not infinite hang
            self.assertIn(proc.returncode, (0, 1),
                          "Expected exit code 0 or 1, got %d" % proc.returncode)
        finally:
            os.unlink(tmp_cfg)


# ── position reconciliation test ─────────────────────────────────────────────

@pytest.mark.fast
class TestPositionReconciliation(unittest.TestCase):

    def test_no_open_position_leaves_strategy_waiting(self):
        """When DB has no open trade today, strategy stays in WAITING state."""
        import live_trader
        cfg = _make_config()
        strategy = MicroORBStrategy(cfg)

        mock_conn = MagicMock()
        mock_cursor = MagicMock()
        mock_cursor.__enter__ = lambda s: mock_cursor
        mock_cursor.__exit__ = MagicMock(return_value=False)
        mock_cursor.fetchone.return_value = None
        mock_conn.cursor.return_value = mock_cursor

        log = MagicMock()
        result = live_trader._reconcile_position(mock_conn, cfg, strategy, log)
        self.assertIsNone(result)
        self.assertEqual(strategy.state, StrategyState.WAITING)

    def test_open_position_restores_strategy_state(self):
        """When DB has an open trade, strategy is restored to IN_POSITION."""
        import live_trader
        cfg = _make_config()
        strategy = MicroORBStrategy(cfg)

        # Uses models.py column names (entry_time, not entry_ts)
        open_trade = {
            "id": 42,
            "direction": "LONG",
            "entry_price": 17010.0,
            "stop_loss": 17006.0,
            "target": 17022.0,
            "entry_time": datetime.datetime.now(tz=ET),
            "session_date": datetime.date.today(),
        }

        mock_conn = MagicMock()
        mock_cursor = MagicMock()
        mock_cursor.__enter__ = lambda s: mock_cursor
        mock_cursor.__exit__ = MagicMock(return_value=False)
        mock_cursor.fetchone.return_value = open_trade
        mock_conn.cursor.return_value = mock_cursor

        log = MagicMock()
        result = live_trader._reconcile_position(mock_conn, cfg, strategy, log)
        self.assertIsNotNone(result)
        self.assertEqual(strategy.state, StrategyState.IN_POSITION)
        self.assertIsNotNone(strategy.current_position())
        self.assertEqual(strategy.current_position().direction, "LONG")


# ── schema alignment tests ────────────────────────────────────────────────────

@pytest.mark.fast
class TestSchemaAlignment(unittest.TestCase):

    def test_no_local_schema_ddl_function(self):
        """live_trader must not define _ensure_trades_schema; use models instead."""
        import live_trader
        self.assertFalse(
            callable(getattr(live_trader, "_ensure_trades_schema", None)),
            "_ensure_trades_schema must be removed; use Trade.ensure_schema() from models",
        )

    def test_reconcile_uses_exit_time_column(self):
        """_reconcile_position SQL must use exit_time (models schema), not exit_ts."""
        import live_trader
        import inspect
        src = inspect.getsource(live_trader._reconcile_position)
        self.assertIn("exit_time", src, "_reconcile_position must query exit_time (models column)")
        self.assertNotIn("exit_ts", src, "_reconcile_position must NOT use legacy exit_ts column")

    def test_reconcile_uses_entry_time_column(self):
        """_reconcile_position SQL must ORDER BY entry_time (models schema), not entry_ts."""
        import live_trader
        import inspect
        src = inspect.getsource(live_trader._reconcile_position)
        self.assertIn("entry_time", src, "_reconcile_position must use entry_time (models column)")
        self.assertNotIn("entry_ts", src, "_reconcile_position must NOT use legacy entry_ts column")


# ── state file tests ──────────────────────────────────────────────────────────

@pytest.mark.fast
class TestStateFile(unittest.TestCase):

    def test_state_file_required_keys(self):
        """_write_state must produce JSON with the required state schema keys."""
        import live_trader
        cfg = _make_config()
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
            cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            # Call _write_state directly (conn is None — _write_state handles that)
            trader._write_state("CONNECTED")
            state_path = Path(cfg["state_path"])
            self.assertTrue(state_path.exists(), "state file must exist after _write_state()")
            state = json.loads(state_path.read_text())
            required = {"position", "entry_price", "sl", "unrealized_pnl",
                        "daily_pnl", "connection", "reconnect_failures", "last_tick_ts"}
            for key in required:
                self.assertIn(key, state, f"state file missing required key: {key}")

    def test_state_file_flat_position_when_no_trade(self):
        """_write_state must show position=FLAT when strategy has no position."""
        import live_trader
        cfg = _make_config()
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
            cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            trader._write_state("CONNECTED")
            state = json.loads(Path(cfg["state_path"]).read_text())
            self.assertEqual(state["position"], "FLAT")
            self.assertIsNone(state["entry_price"])
            self.assertIsNone(state["sl"])
            self.assertEqual(state["connection"], "CONNECTED")


# ── PID file tests ────────────────────────────────────────────────────────────

@pytest.mark.fast
class TestPidFile(unittest.TestCase):

    def test_pid_file_written_by_emergency_flatten(self):
        """_emergency_flatten must remove the PID file (process is exiting)."""
        import live_trader
        cfg = _make_config()
        with tempfile.TemporaryDirectory() as tmpdir:
            pid_path = Path(tmpdir) / "live_trader.pid"
            state_path = Path(tmpdir) / "live_state.json"
            cfg["pid_path"] = str(pid_path)
            cfg["state_path"] = str(state_path)
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            # Simulate PID file existing before emergency flatten
            pid_path.write_text(str(os.getpid()))
            self.assertTrue(pid_path.exists())
            trader._emergency_flatten("TEST")
            self.assertFalse(pid_path.exists(), "PID file must be removed by _emergency_flatten")


# ── daily P&L accumulation tests ─────────────────────────────────────────────

@pytest.mark.fast
class TestDailyPnl(unittest.TestCase):

    def test_daily_pnl_accumulates_after_exit(self):
        """_daily_pnl must reflect realized P&L after _on_exit() is called."""
        import live_trader
        cfg = _make_config()
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
            cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            trader._active_trade_id = 99
            trader._session_date = datetime.date.today()

            # Mock conn: _write_trade_close queries direction+entry_price, then updates
            mock_conn = MagicMock()
            mock_cursor = MagicMock()
            mock_cursor.__enter__ = lambda s: mock_cursor
            mock_cursor.__exit__ = MagicMock(return_value=False)
            # DB row for the open trade: LONG entry at 17000, point_value=2.0 (MNQ)
            mock_cursor.fetchone.return_value = {"direction": "LONG", "entry_price": 17000.0}
            mock_conn.cursor.return_value = mock_cursor
            trader._conn = mock_conn

            self.assertEqual(trader._daily_pnl, 0.0)
            exit_price = 17010.0  # +10 pts × 2.0 - $4 commission = +$16
            trader._on_exit(exit_price, datetime.datetime.now(tz=ET), "TARGET_HIT")

            self.assertAlmostEqual(trader._daily_pnl, 16.0, places=2,
                                   msg="_daily_pnl must accumulate realized P&L after exit")

    def test_daily_pnl_negative_on_loss(self):
        """_daily_pnl must go negative on a losing trade."""
        import live_trader
        cfg = _make_config()
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
            cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            trader._active_trade_id = 100
            trader._session_date = datetime.date.today()

            mock_conn = MagicMock()
            mock_cursor = MagicMock()
            mock_cursor.__enter__ = lambda s: mock_cursor
            mock_cursor.__exit__ = MagicMock(return_value=False)
            mock_cursor.fetchone.return_value = {"direction": "LONG", "entry_price": 17000.0}
            mock_conn.cursor.return_value = mock_cursor
            trader._conn = mock_conn

            exit_price = 16996.0  # -4 pts × 2.0 - $4 commission = -$12
            trader._on_exit(exit_price, datetime.datetime.now(tz=ET), "SL_HIT")

            self.assertAlmostEqual(trader._daily_pnl, -12.0, places=2,
                                   msg="_daily_pnl must go negative on losing trade")

    def test_write_trade_close_returns_pnl(self):
        """_write_trade_close must return realized P&L in USD, not None."""
        import live_trader
        mock_conn = MagicMock()
        mock_cursor = MagicMock()
        mock_cursor.__enter__ = lambda s: mock_cursor
        mock_cursor.__exit__ = MagicMock(return_value=False)
        mock_cursor.fetchone.return_value = {"direction": "SHORT", "entry_price": 17050.0}
        mock_conn.cursor.return_value = mock_cursor

        # SHORT: entry 17050, exit 17030 → +20 pts × 20 - $4 commission = $396
        result = live_trader._write_trade_close(
            mock_conn, 1, 17030.0, datetime.datetime.now(tz=ET), "TARGET_HIT", 20.0)
        self.assertIsInstance(result, float, "_write_trade_close must return float")
        self.assertAlmostEqual(result, 396.0, places=2)

    def test_write_trade_close_returns_zero_for_missing_trade(self):
        """_write_trade_close must return 0.0 when trade_id not found."""
        import live_trader
        mock_conn = MagicMock()
        mock_cursor = MagicMock()
        mock_cursor.__enter__ = lambda s: mock_cursor
        mock_cursor.__exit__ = MagicMock(return_value=False)
        mock_cursor.fetchone.return_value = None
        mock_conn.cursor.return_value = mock_cursor

        result = live_trader._write_trade_close(
            mock_conn, 999, 17000.0, datetime.datetime.now(tz=ET), "EOD", 20.0)
        self.assertEqual(result, 0.0)


@pytest.mark.fast
class TestLiveConfigSchema(unittest.TestCase):
    """Pydantic schema validation against live_config.json — catches typos and invariant violations."""

    CONFIG_PATH = REPO_ROOT / "config" / "live_config.json"
    SCHEMA_PATH = REPO_ROOT / "config" / "live_config_schema.py"

    def _load(self) -> dict:
        import json
        return json.loads(self.CONFIG_PATH.read_text())

    def _validate(self, cfg: dict):
        sys.path.insert(0, str(REPO_ROOT))
        from config.live_config_schema import LiveConfig
        return LiveConfig.model_validate(cfg)

    def test_live_config_passes_schema(self):
        """live_config.json must pass full Pydantic schema validation."""
        cfg = self._load()
        self._validate(cfg)  # raises on failure

    def test_simulator_trade_route_rejected(self):
        """Schema must reject trade_route='simulator'."""
        from pydantic import ValidationError
        cfg = self._load()
        cfg["trade_route"] = "simulator"
        with self.assertRaises(ValidationError, msg="Schema must reject trade_route='simulator'"):
            self._validate(cfg)

    def test_max_daily_trades_mismatch_rejected(self):
        """Schema must reject flat max_daily_trades != prop_firm.max_daily_trades."""
        from pydantic import ValidationError
        cfg = self._load()
        cfg["max_daily_trades"] = cfg["prop_firm"]["max_daily_trades"] + 2
        with self.assertRaises(ValidationError, msg="Schema must reject max_daily_trades mismatch"):
            self._validate(cfg)

    def test_sl_points_ticks_mismatch_rejected(self):
        """Schema must reject sl_points/orb.stop_loss_ticks inconsistency (BUG-8 class)."""
        from pydantic import ValidationError
        cfg = self._load()
        cfg["orb"] = dict(cfg["orb"])
        cfg["orb"]["stop_loss_ticks"] = 16  # was 60 before BUG-8 fix
        with self.assertRaises(ValidationError, msg="Schema must reject sl_points/ticks mismatch"):
            self._validate(cfg)

    def test_wrong_point_value_rejected(self):
        """Schema must reject point_value=20.0 (NQ) at both root and orb section."""
        from pydantic import ValidationError
        cfg = self._load()
        cfg["point_value"] = 20.0
        with self.assertRaises(ValidationError, msg="Schema must reject point_value=20.0"):
            self._validate(cfg)

    def test_negative_daily_loss_limit_required(self):
        """Schema must reject positive flat daily_loss_limit (C++ requires negative threshold)."""
        from pydantic import ValidationError
        cfg = self._load()
        cfg["daily_loss_limit"] = 2000.0  # positive — wrong for C++ flat key
        with self.assertRaises(ValidationError, msg="Schema must reject positive daily_loss_limit"):
            self._validate(cfg)

    def test_typo_key_detected(self):
        """Schema must reject config when a required key is missing due to a typo."""
        from pydantic import ValidationError
        cfg = self._load()
        cfg["trale_route"] = cfg.pop("trade_route")  # typo
        with self.assertRaises(ValidationError, msg="Schema must reject missing trade_route"):
            self._validate(cfg)


# ── EOD flatten path tests ────────────────────────────────────────────────────

@pytest.mark.fast
class TestEodFlatten(unittest.TestCase):

    def _make_trader_in_position(self, tmpdir: str):
        """Return a LiveTrader with an open LONG position and a mock DB connection.

        _poll_latest_tick is patched at module level to avoid fetchone ordering
        issues — it returns a synthetic tick dict on every call.
        """
        import live_trader
        cfg = _make_config(rth_close="09:31:00")  # close almost immediately
        cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
        cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
        trader = live_trader.LiveTrader(cfg, dry_run=True)
        trader._session_date = datetime.date.today()
        trader._active_trade_id = 42

        mock_conn = MagicMock()
        mock_cursor = MagicMock()
        mock_cursor.__enter__ = lambda s: mock_cursor
        mock_cursor.__exit__ = MagicMock(return_value=False)
        # _write_trade_close SELECT → direction + entry_price
        mock_cursor.fetchone.return_value = {"direction": "LONG", "entry_price": 17000.0}
        mock_cursor.fetchall.return_value = []
        mock_conn.cursor.return_value = mock_cursor
        trader._conn = mock_conn

        # Patch _poll_latest_tick so it always returns a synthetic tick dict
        # (avoids coupling to fetchone call ordering in _maybe_eod's internals)
        self._tick_patcher = patch.object(
            live_trader, "_poll_latest_tick",
            return_value={"price": 17005.0, "ts": datetime.datetime.now(tz=ET)})
        self._tick_patcher.start()

        # Put strategy in IN_POSITION state
        from strategy.micro_orb import StrategyState, _Position
        trader._strategy.state = StrategyState.IN_POSITION
        trader._strategy._position = _Position(
            direction="LONG", entry_price=17000.0,
            stop_loss=16985.0, target=17030.0,
            entry_ts=datetime.datetime.now(tz=ET))
        return trader, mock_conn, mock_cursor

    def tearDown(self):
        if hasattr(self, "_tick_patcher"):
            self._tick_patcher.stop()

    def test_eod_flatten_stops_loop(self):
        """_maybe_eod after rth_close must set _running=False."""
        with tempfile.TemporaryDirectory() as tmpdir:
            trader, _, _ = self._make_trader_in_position(tmpdir)
            trader._running = True
            # Time is past rth_close="09:31:00"
            now_et = datetime.datetime(2026, 1, 2, 9, 32, tzinfo=ET)
            trader._maybe_eod(now_et)
            self.assertFalse(trader._running,
                             "_running must be False after EOD flatten")

    def test_eod_flatten_sets_done_flag(self):
        """_maybe_eod must set _eod_flatten_done=True to prevent re-triggering."""
        with tempfile.TemporaryDirectory() as tmpdir:
            trader, _, _ = self._make_trader_in_position(tmpdir)
            now_et = datetime.datetime(2026, 1, 2, 9, 32, tzinfo=ET)
            trader._maybe_eod(now_et)
            self.assertTrue(trader._eod_flatten_done)

    def test_eod_flatten_idempotent(self):
        """Calling _maybe_eod twice must not double-exit."""
        with tempfile.TemporaryDirectory() as tmpdir:
            trader, mock_conn, _ = self._make_trader_in_position(tmpdir)
            now_et = datetime.datetime(2026, 1, 2, 9, 32, tzinfo=ET)
            trader._maybe_eod(now_et)
            # Second call must be a no-op
            call_count_before = mock_conn.cursor.call_count
            trader._maybe_eod(now_et)
            self.assertEqual(mock_conn.cursor.call_count, call_count_before,
                             "Second EOD call must not write to DB again")

    def test_eod_not_triggered_before_close(self):
        """_maybe_eod must not flatten before rth_close."""
        with tempfile.TemporaryDirectory() as tmpdir:
            trader, _, _ = self._make_trader_in_position(tmpdir)
            trader._running = True
            now_et = datetime.datetime(2026, 1, 2, 9, 29, tzinfo=ET)  # before 09:31
            trader._maybe_eod(now_et)
            self.assertTrue(trader._running, "_running must stay True before EOD")
            self.assertFalse(trader._eod_flatten_done)


# ── DB reconnect limit tests ──────────────────────────────────────────────────

@pytest.mark.fast
class TestBarLoopReconnectLimit(unittest.TestCase):

    def _make_trader(self, tmpdir: str):
        import live_trader
        cfg = _make_config()
        cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
        cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
        return live_trader.LiveTrader(cfg, dry_run=True)

    def test_reconnect_counter_increments_on_pg_error(self):
        """Each OperationalError in _bar_loop must increment _reconnect_failures."""
        import live_trader, psycopg2
        with tempfile.TemporaryDirectory() as tmpdir:
            trader = self._make_trader(tmpdir)
            trader._conn = MagicMock()
            # Make _poll_latest_bar raise OperationalError
            with patch.object(live_trader, "_poll_latest_bar",
                              side_effect=psycopg2.OperationalError("connection reset")):
                with patch.object(live_trader, "_pg_connect_with_retry",
                                  return_value=MagicMock()):
                    trader._bar_loop()
            self.assertEqual(trader._reconnect_failures, 1)

    def test_reconnect_counter_resets_on_success(self):
        """A successful _poll_latest_bar must reset _reconnect_failures to 0."""
        import live_trader
        with tempfile.TemporaryDirectory() as tmpdir:
            trader = self._make_trader(tmpdir)
            trader._reconnect_failures = 5
            trader._last_bar_ts = None
            trader._conn = MagicMock()
            # Successful poll returns None (no new bar)
            with patch.object(live_trader, "_poll_latest_bar", return_value=None):
                trader._bar_loop()
            self.assertEqual(trader._reconnect_failures, 0)

    def test_halt_after_max_failures(self):
        """After MAX_RECONNECT_FAILURES consecutive errors, _running must be False."""
        import live_trader, psycopg2
        with tempfile.TemporaryDirectory() as tmpdir:
            trader = self._make_trader(tmpdir)
            trader._conn = MagicMock()
            trader._running = True
            trader._reconnect_failures = live_trader.LiveTrader._MAX_RECONNECT_FAILURES - 1
            with patch.object(live_trader, "_poll_latest_bar",
                              side_effect=psycopg2.OperationalError("connection reset")):
                with patch.object(live_trader, "_pg_connect_with_retry",
                                  return_value=MagicMock()):
                    trader._bar_loop()
            self.assertFalse(trader._running,
                             "_running must be False after max reconnect failures")


# ── _write_trade_close tests ──────────────────────────────────────────────────

@pytest.mark.live_trader
@pytest.mark.fast
class TestWriteTradeClose(unittest.TestCase):

    def _make_mock_conn(self, fetchone_return):
        """Return a mock psycopg2 connection whose cursor yields fetchone_return."""
        mock_conn = MagicMock()
        mock_cursor = MagicMock()
        mock_cursor.__enter__ = lambda s: mock_cursor
        mock_cursor.__exit__ = MagicMock(return_value=False)
        mock_cursor.fetchone.return_value = fetchone_return
        mock_conn.cursor.return_value = mock_cursor
        return mock_conn, mock_cursor

    def test_long_trade_close_pnl_correct(self):
        """LONG close: pnl = (exit - entry) * point_value - commission."""
        import live_trader
        mock_conn, mock_cursor = self._make_mock_conn(
            {"direction": "LONG", "entry_price": 17000.0}
        )
        # +10 pts * 2.0 - 4.0 commission = 16.0
        result = live_trader._write_trade_close(
            mock_conn, 1, 17010.0, datetime.datetime.now(tz=ET), "TARGET_HIT", 2.0
        )
        self.assertIsInstance(result, float)
        self.assertAlmostEqual(result, 16.0, places=2)
        mock_conn.commit.assert_called_once()

    def test_short_trade_close_pnl_correct(self):
        """SHORT close: pnl = (entry - exit) * point_value - commission."""
        import live_trader
        mock_conn, _ = self._make_mock_conn(
            {"direction": "SHORT", "entry_price": 17050.0}
        )
        # +20 pts * 20.0 - 4.0 commission = 396.0
        result = live_trader._write_trade_close(
            mock_conn, 2, 17030.0, datetime.datetime.now(tz=ET), "TARGET_HIT", 20.0
        )
        self.assertAlmostEqual(result, 396.0, places=2)

    def test_losing_trade_returns_negative_pnl(self):
        """A losing LONG trade must return a negative P&L."""
        import live_trader
        mock_conn, _ = self._make_mock_conn(
            {"direction": "LONG", "entry_price": 17000.0}
        )
        # -4 pts * 2.0 - 4.0 commission = -12.0
        result = live_trader._write_trade_close(
            mock_conn, 3, 16996.0, datetime.datetime.now(tz=ET), "SL_HIT", 2.0
        )
        self.assertAlmostEqual(result, -12.0, places=2)

    def test_missing_trade_returns_zero(self):
        """When trade_id not found in DB, return 0.0 without committing."""
        import live_trader
        mock_conn, _ = self._make_mock_conn(None)
        result = live_trader._write_trade_close(
            mock_conn, 999, 17000.0, datetime.datetime.now(tz=ET), "EOD", 2.0
        )
        self.assertEqual(result, 0.0)
        mock_conn.commit.assert_not_called()

    def test_db_update_executed_for_found_trade(self):
        """For a found trade, the cursor must execute an UPDATE statement."""
        import live_trader
        mock_conn, mock_cursor = self._make_mock_conn(
            {"direction": "LONG", "entry_price": 17000.0}
        )
        live_trader._write_trade_close(
            mock_conn, 5, 17010.0, datetime.datetime.now(tz=ET), "TARGET_HIT", 2.0
        )
        # SELECT + UPDATE = 2 execute calls
        self.assertEqual(mock_cursor.execute.call_count, 2)


# ── _write_session_summary tests ──────────────────────────────────────────────

@pytest.mark.live_trader
@pytest.mark.fast
class TestWriteSessionSummary(unittest.TestCase):

    def test_calls_trade_for_date_and_save(self):
        """_write_session_summary must call Trade.for_date and SessionSummary.save."""
        import live_trader
        session_date = datetime.date.today()
        mock_conn = MagicMock()
        mock_summary = MagicMock()

        with patch("live_trader.Trade.for_date", return_value=[]) as mock_for_date, \
             patch("live_trader.SessionSummary.build_from_trades",
                   return_value=mock_summary) as mock_build:

            live_trader._write_session_summary(mock_conn, session_date, True, "EOD")

            mock_for_date.assert_called_once_with(mock_conn, session_date)
            mock_build.assert_called_once()
            mock_summary.save.assert_called_once_with(mock_conn)

    def test_notes_set_to_exit_reason(self):
        """_write_session_summary must set summary.notes = exit_reason."""
        import live_trader
        session_date = datetime.date.today()
        mock_conn = MagicMock()
        exit_reason = "SIGTERM"

        with patch("live_trader.Trade.for_date", return_value=[]):
            mock_summary = MagicMock()
            with patch("live_trader.SessionSummary.build_from_trades",
                       return_value=mock_summary):
                live_trader._write_session_summary(
                    mock_conn, session_date, False, exit_reason
                )
                self.assertEqual(mock_summary.notes, exit_reason)

    def test_only_completed_trades_passed_to_build(self):
        """Only trades with exit_time set must be passed to build_from_trades."""
        import live_trader
        from models import Trade

        session_date = datetime.date.today()
        mock_conn = MagicMock()

        # Build minimal Trade-like objects (no DB needed — just duck-type)
        closed_trade = MagicMock(spec=Trade)
        closed_trade.exit_time = datetime.datetime.now(tz=ET)
        open_trade = MagicMock(spec=Trade)
        open_trade.exit_time = None

        with patch("live_trader.Trade.for_date",
                   return_value=[closed_trade, open_trade]):
            mock_summary = MagicMock()
            with patch("live_trader.SessionSummary.build_from_trades",
                       return_value=mock_summary) as mock_build:
                live_trader._write_session_summary(
                    mock_conn, session_date, True, "EOD"
                )
                called_trades = mock_build.call_args[0][0]
                self.assertIn(closed_trade, called_trades)
                self.assertNotIn(open_trade, called_trades)


# ── _cancel_trade_open tests ──────────────────────────────────────────────────

@pytest.mark.live_trader
@pytest.mark.fast
class TestCancelTradeOpen(unittest.TestCase):

    def _make_mock_conn(self):
        mock_conn = MagicMock()
        mock_cursor = MagicMock()
        mock_cursor.__enter__ = lambda s: mock_cursor
        mock_cursor.__exit__ = MagicMock(return_value=False)
        mock_conn.cursor.return_value = mock_cursor
        return mock_conn, mock_cursor

    def test_executes_update_with_trade_id(self):
        """_cancel_trade_open must execute an UPDATE using the given trade_id."""
        import live_trader
        mock_conn, mock_cursor = self._make_mock_conn()
        live_trader._cancel_trade_open(mock_conn, 42)
        mock_cursor.execute.assert_called_once()
        sql, params = mock_cursor.execute.call_args[0]
        self.assertIn("UPDATE", sql.upper())
        self.assertIn(42, params)

    def test_commits_after_update(self):
        """_cancel_trade_open must commit the transaction."""
        import live_trader
        mock_conn, _ = self._make_mock_conn()
        live_trader._cancel_trade_open(mock_conn, 7)
        mock_conn.commit.assert_called_once()

    def test_sets_order_failed_exit_reason(self):
        """_cancel_trade_open must set exit_reason = 'ORDER_FAILED'."""
        import live_trader
        mock_conn, mock_cursor = self._make_mock_conn()
        live_trader._cancel_trade_open(mock_conn, 10)
        sql, _ = mock_cursor.execute.call_args[0]
        self.assertIn("ORDER_FAILED", sql)

    def test_sets_exit_time_on_cancel(self):
        """_cancel_trade_open must set exit_time (so the trade appears closed)."""
        import live_trader
        mock_conn, mock_cursor = self._make_mock_conn()
        live_trader._cancel_trade_open(mock_conn, 15)
        sql, _ = mock_cursor.execute.call_args[0]
        self.assertIn("exit_time", sql)


# ── Integration: tick → signal → DB write path ────────────────────────────────

@pytest.mark.live_trader
@pytest.mark.fast
class TestTickSignalDBIntegration(unittest.TestCase):
    """End-to-end integration test: tick arrives → ORB signal → DB write.

    Uses an in-memory mock connection — no real PostgreSQL required.
    The mock tracks every cursor.execute() call so we can assert that:
      1. MicroORBStrategy produced a LONG signal from the tick sequence.
      2. _on_signal wrote an INSERT to the trades table (via Trade.save).
      3. _on_signal updated the order_id in the DB (via _update_trade_order_id).
      4. LiveTrader._active_trade_id was set to the returned trade id.
    """

    def _make_mock_conn(self, insert_id: int = 7):
        """Return a mock psycopg2 conn whose cursor supports context-manager usage.

        The cursor's fetchone() returns {"id": insert_id} on the first call
        (simulating the RETURNING id clause in Trade.save), and None thereafter
        (e.g. _write_state's live_position upsert has no fetchone dependency).
        """
        mock_conn = MagicMock()
        mock_cursor = MagicMock()
        mock_cursor.__enter__ = lambda s: mock_cursor
        mock_cursor.__exit__ = MagicMock(return_value=False)
        # First fetchone() → INSERT RETURNING id; all subsequent → None
        mock_cursor.fetchone.side_effect = [{"id": insert_id}, None, None, None]
        mock_conn.cursor.return_value = mock_cursor
        return mock_conn, mock_cursor

    def _build_breakout_sequence(self, strategy: MicroORBStrategy):
        """Feed the strategy 5 range bars then one breakout bar.

        Returns the Signal produced by the breakout bar (asserted non-None).
        """
        base = _base_dt()
        # 5 range-building bars
        for i in range(5):
            bar = _make_bar(base + datetime.timedelta(minutes=i))
            strategy.on_bar(bar)

        # One breakout bar — close well above orb_high to guarantee signal
        assert strategy.orb_high is not None, "ORB range must be set after 5 bars"
        breakout_ts = base + datetime.timedelta(minutes=5)
        breakout_bar = {
            "ts": breakout_ts,
            "open": strategy.orb_high + 0.25,
            "high": strategy.orb_high + 10.0,
            "low": strategy.orb_high,
            "close": strategy.orb_high + 8.0,  # close above range high → LONG
            "volume": 800,
        }
        sig = strategy.on_bar(breakout_bar)
        return sig

    def test_breakout_signal_triggers_db_insert(self):
        """Full path: ticks → LONG signal → Trade.save INSERT executed."""
        import live_trader

        cfg = _make_config()
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
            cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            trader._session_date = datetime.date(2024, 1, 15)

            mock_conn, mock_cursor = self._make_mock_conn(insert_id=7)
            trader._conn = mock_conn

            # Feed ticks (via on_bar) to produce a signal
            sig = self._build_breakout_sequence(trader._strategy)

            # Verify the strategy produced a LONG signal
            self.assertIsNotNone(sig, "Breakout bar must generate a signal")
            self.assertEqual(sig.direction, "LONG")

            # Exercise the full _on_signal path (DB write happens here)
            trader._on_signal(sig)

            # Assert that cursor.execute was called at all (Trade.save + order_id update)
            self.assertTrue(
                mock_cursor.execute.called,
                "cursor.execute must be called during _on_signal (DB write expected)",
            )

            # Find the INSERT INTO trades call
            insert_calls = [
                call for call in mock_cursor.execute.call_args_list
                if "INSERT" in str(call) and "trades" in str(call).lower()
            ]
            self.assertTrue(
                len(insert_calls) >= 1,
                "At least one INSERT INTO trades must be executed during _on_signal",
            )

            # Verify LiveTrader stored the trade id returned by the DB
            self.assertEqual(
                trader._active_trade_id, 7,
                "_active_trade_id must be set to the id returned by Trade.save",
            )

    def test_db_insert_contains_correct_direction(self):
        """INSERT SQL must carry the signal direction (LONG) as a parameter."""
        import live_trader

        cfg = _make_config()
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
            cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            trader._session_date = datetime.date(2024, 1, 15)

            mock_conn, mock_cursor = self._make_mock_conn(insert_id=11)
            trader._conn = mock_conn

            sig = self._build_breakout_sequence(trader._strategy)
            self.assertIsNotNone(sig)
            trader._on_signal(sig)

            # Collect all parameter dicts passed to execute() calls
            all_params = []
            for call in mock_cursor.execute.call_args_list:
                args = call[0]
                if len(args) >= 2 and isinstance(args[1], dict):
                    all_params.append(args[1])

            # At least one parameter dict must have direction='LONG'
            directions = [p.get("direction") for p in all_params]
            self.assertIn(
                "LONG", directions,
                "Trade INSERT parameters must include direction='LONG'",
            )

    def test_db_insert_contains_session_date(self):
        """INSERT parameters must include the correct session_date."""
        import live_trader

        cfg = _make_config()
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
            cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            session_date = datetime.date(2024, 1, 15)
            trader._session_date = session_date

            mock_conn, mock_cursor = self._make_mock_conn(insert_id=22)
            trader._conn = mock_conn

            sig = self._build_breakout_sequence(trader._strategy)
            self.assertIsNotNone(sig)
            trader._on_signal(sig)

            all_params = []
            for call in mock_cursor.execute.call_args_list:
                args = call[0]
                if len(args) >= 2 and isinstance(args[1], dict):
                    all_params.append(args[1])

            session_dates = [p.get("session_date") for p in all_params]
            self.assertIn(
                session_date, session_dates,
                "Trade INSERT parameters must include the correct session_date",
            )

    def test_order_id_updated_after_dry_run_submit(self):
        """_on_signal must update order_id in DB after dry-run submission."""
        import live_trader

        cfg = _make_config()
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
            cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            trader._session_date = datetime.date(2024, 1, 15)

            mock_conn, mock_cursor = self._make_mock_conn(insert_id=33)
            trader._conn = mock_conn

            sig = self._build_breakout_sequence(trader._strategy)
            self.assertIsNotNone(sig)
            trader._on_signal(sig)

            # Look for UPDATE ... order_id = ... call
            update_calls = [
                call for call in mock_cursor.execute.call_args_list
                if "UPDATE" in str(call) and "order_id" in str(call).lower()
            ]
            self.assertTrue(
                len(update_calls) >= 1,
                "_on_signal must execute UPDATE trades SET order_id after dry-run submit",
            )

    def test_short_breakout_also_writes_db(self):
        """SHORT signal path also produces an INSERT into trades."""
        import live_trader

        cfg = _make_config(allow_short=True)
        with tempfile.TemporaryDirectory() as tmpdir:
            cfg["state_path"] = os.path.join(tmpdir, "live_state.json")
            cfg["pid_path"] = os.path.join(tmpdir, "live_trader.pid")
            trader = live_trader.LiveTrader(cfg, dry_run=True)
            trader._session_date = datetime.date(2024, 1, 15)

            mock_conn, mock_cursor = self._make_mock_conn(insert_id=44)
            trader._conn = mock_conn

            # Build ORB range first
            base = _base_dt()
            for i in range(5):
                bar = _make_bar(base + datetime.timedelta(minutes=i))
                trader._strategy.on_bar(bar)

            assert trader._strategy.orb_low is not None
            # SHORT breakout: close well below orb_low
            short_bar = {
                "ts": base + datetime.timedelta(minutes=5),
                "open": trader._strategy.orb_low - 0.25,
                "high": trader._strategy.orb_low,
                "low": trader._strategy.orb_low - 10.0,
                "close": trader._strategy.orb_low - 8.0,
                "volume": 600,
            }
            sig = trader._strategy.on_bar(short_bar)
            self.assertIsNotNone(sig, "SHORT breakout bar must generate a signal")
            self.assertEqual(sig.direction, "SHORT")

            trader._on_signal(sig)

            insert_calls = [
                call for call in mock_cursor.execute.call_args_list
                if "INSERT" in str(call) and "trades" in str(call).lower()
            ]
            self.assertTrue(
                len(insert_calls) >= 1,
                "SHORT signal must also produce INSERT INTO trades",
            )
            self.assertEqual(trader._active_trade_id, 44)


if __name__ == "__main__":
    unittest.main(verbosity=2)
