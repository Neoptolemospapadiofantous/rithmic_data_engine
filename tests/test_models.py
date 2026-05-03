"""
tests/test_models.py — Unit tests for models.py Trade and SessionSummary.

These tests use MagicMock to simulate psycopg2 connections; no real DB required.

Coverage:
  - Trade.ensure_schema runs without error (schema SQL executed)
  - Trade.save constructs correct INSERT (via mock cursor)
  - Trade.open_position returns None when cursor returns nothing
  - SessionSummary.ensure_schema adds crash_exit column idempotently
  - SessionSummary.save includes crash_exit in INSERT
  - SessionSummary.write_crash_safe is idempotent (two calls → same result)
  - SessionSummary.write_crash_safe handles None/empty trades (crash with 0 trades)
  - SessionSummary.build_from_trades computes correct aggregates
"""
from __future__ import annotations

import sys
import unittest
from datetime import date, datetime, timezone
from pathlib import Path
from unittest.mock import MagicMock

import pytest

pytestmark = pytest.mark.fast

REPO_ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(REPO_ROOT))

from unittest.mock import patch

from models import Trade, SessionSummary, _row_to_trade_kwargs, get_conn


# ── helpers ───────────────────────────────────────────────────────────────────

def _make_conn(fetchone_return=None, fetchall_return=None):
    """Return a MagicMock psycopg2 connection with a cursor context manager."""
    cursor = MagicMock()
    cursor.fetchone.return_value = fetchone_return
    cursor.fetchall.return_value = fetchall_return or []

    conn = MagicMock()
    # Support `with conn.cursor() as cur:` pattern
    conn.cursor.return_value.__enter__ = MagicMock(return_value=cursor)
    conn.cursor.return_value.__exit__ = MagicMock(return_value=False)
    return conn, cursor


def _make_trade(**kwargs) -> Trade:
    defaults = dict(
        session_date=date(2026, 4, 23),
        symbol="NQ",
        direction="LONG",
        entry_price=17850.0,
        entry_time=datetime(2026, 4, 23, 9, 35, tzinfo=timezone.utc),
    )
    defaults.update(kwargs)
    return Trade(**defaults)


# ── Trade tests ───────────────────────────────────────────────────────────────

class TestTradeEnsureSchema(unittest.TestCase):

    def test_ensure_schema_executes_create_table(self):
        conn, cur = _make_conn()
        Trade.ensure_schema(conn)
        # Should have called execute at least once
        self.assertTrue(cur.execute.called)
        sql = cur.execute.call_args_list[0][0][0]
        self.assertIn("CREATE TABLE IF NOT EXISTS trades", sql)

    def test_ensure_schema_commits(self):
        conn, _ = _make_conn()
        Trade.ensure_schema(conn)
        conn.commit.assert_called()

    def test_ensure_schema_idempotent(self):
        """Two calls should not raise and should both commit."""
        conn, _ = _make_conn()
        Trade.ensure_schema(conn)
        Trade.ensure_schema(conn)
        self.assertEqual(conn.commit.call_count, 2)


class TestTradeSave(unittest.TestCase):

    def test_save_returns_id_from_db(self):
        conn, cur = _make_conn(fetchone_return={"id": 42})
        trade = _make_trade()
        result = trade.save(conn)
        self.assertEqual(result, 42)
        self.assertEqual(trade.id, 42)

    def test_save_includes_new_fields_in_insert(self):
        conn, cur = _make_conn(fetchone_return={"id": 1})
        trade = _make_trade(dry_run=True, target=17900.0, exit_reason="TARGET_HIT")
        trade.save(conn)
        sql = cur.execute.call_args_list[0][0][0]
        self.assertIn("dry_run", sql)
        self.assertIn("target", sql)
        self.assertIn("exit_reason", sql)

    def test_save_commits(self):
        conn, cur = _make_conn(fetchone_return={"id": 1})
        _make_trade().save(conn)
        conn.commit.assert_called()

    def test_save_sets_id_on_instance(self):
        conn, cur = _make_conn(fetchone_return={"id": 99})
        trade = _make_trade()
        self.assertIsNone(trade.id)
        trade.save(conn)
        self.assertEqual(trade.id, 99)


class TestTradeOpenPosition(unittest.TestCase):

    def test_open_position_returns_none_when_no_row(self):
        conn, _ = _make_conn(fetchone_return=None)
        result = Trade.open_position(conn)
        self.assertIsNone(result)

    def test_open_position_returns_trade_when_found(self):
        row = {
            "id": 7,
            "session_date": date(2026, 4, 23),
            "symbol": "NQ",
            "direction": "LONG",
            "entry_price": 17850.0,
            "entry_time": datetime(2026, 4, 23, 9, 35, tzinfo=timezone.utc),
            "exit_price": None,
            "exit_time": None,
            "quantity": 1,
            "pnl": None,
            "pnl_points": None,
            "stop_loss": None,
            "target": None,
            "exit_reason": None,
            "dry_run": True,
            "source": "python",
            "session_id": None,
            "ml_prediction": None,
            "ml_confidence": None,
        }
        conn, _ = _make_conn(fetchone_return=row)
        result = Trade.open_position(conn)
        self.assertIsNotNone(result)
        self.assertIsInstance(result, Trade)
        self.assertEqual(result.direction, "LONG")


# ── SessionSummary tests ──────────────────────────────────────────────────────

class TestSessionSummaryEnsureSchema(unittest.TestCase):

    def test_ensure_schema_creates_table(self):
        conn, cur = _make_conn()
        SessionSummary.ensure_schema(conn)
        sqls = [c[0][0] for c in cur.execute.call_args_list]
        self.assertTrue(any("CREATE TABLE IF NOT EXISTS session_summary" in s for s in sqls))

    def test_ensure_schema_adds_crash_exit_column(self):
        conn, cur = _make_conn()
        SessionSummary.ensure_schema(conn)
        sqls = [c[0][0] for c in cur.execute.call_args_list]
        self.assertTrue(
            any("crash_exit" in s and "ADD COLUMN IF NOT EXISTS" in s for s in sqls),
            "ensure_schema must issue ADD COLUMN IF NOT EXISTS for crash_exit",
        )

    def test_ensure_schema_commits(self):
        conn, _ = _make_conn()
        SessionSummary.ensure_schema(conn)
        conn.commit.assert_called()

    def test_ensure_schema_idempotent(self):
        conn, _ = _make_conn()
        SessionSummary.ensure_schema(conn)
        SessionSummary.ensure_schema(conn)
        self.assertEqual(conn.commit.call_count, 2)


class TestSessionSummarySave(unittest.TestCase):

    def _make_summary(self, **kwargs) -> SessionSummary:
        defaults = dict(
            session_id="2026-04-23_python",
            date=date(2026, 4, 23),
        )
        defaults.update(kwargs)
        return SessionSummary(**defaults)

    def test_save_includes_crash_exit_in_insert(self):
        conn, cur = _make_conn()
        self._make_summary(crash_exit=True).save(conn)
        sql = cur.execute.call_args_list[0][0][0]
        self.assertIn("crash_exit", sql)

    def test_save_commits(self):
        conn, _ = _make_conn()
        self._make_summary().save(conn)
        conn.commit.assert_called()

    def test_save_on_conflict_do_update_present(self):
        conn, cur = _make_conn()
        self._make_summary().save(conn)
        sql = cur.execute.call_args_list[0][0][0]
        self.assertIn("ON CONFLICT", sql)
        self.assertIn("DO UPDATE", sql)


class TestSessionSummaryWriteCrashSafe(unittest.TestCase):

    def test_crash_safe_with_no_trades(self):
        conn, cur = _make_conn()
        result = SessionSummary.write_crash_safe(
            conn, date(2026, 4, 23), trades=None
        )
        self.assertIsInstance(result, SessionSummary)
        self.assertEqual(result.trade_count, 0)
        self.assertTrue(result.crash_exit)
        conn.commit.assert_called()

    def test_crash_safe_with_empty_trades(self):
        conn, _ = _make_conn()
        result = SessionSummary.write_crash_safe(
            conn, date(2026, 4, 23), trades=[]
        )
        self.assertEqual(result.trade_count, 0)
        self.assertTrue(result.crash_exit)

    def test_crash_safe_computes_aggregates(self):
        conn, _ = _make_conn()
        t1 = _make_trade(
            pnl=100.0,
            exit_time=datetime(2026, 4, 23, 10, 0, tzinfo=timezone.utc),
        )
        t2 = _make_trade(
            pnl=-40.0,
            exit_time=datetime(2026, 4, 23, 11, 0, tzinfo=timezone.utc),
        )
        result = SessionSummary.write_crash_safe(
            conn, date(2026, 4, 23), trades=[t1, t2]
        )
        self.assertEqual(result.trade_count, 2)
        self.assertAlmostEqual(result.gross_pnl, 60.0)
        self.assertEqual(result.win_count, 1)

    def test_crash_safe_idempotent(self):
        """Two calls to write_crash_safe must produce the same session_id row."""
        conn1, cur1 = _make_conn()
        conn2, cur2 = _make_conn()
        r1 = SessionSummary.write_crash_safe(conn1, date(2026, 4, 23))
        r2 = SessionSummary.write_crash_safe(conn2, date(2026, 4, 23))
        # Both must write the same session_id
        self.assertEqual(r1.session_id, r2.session_id)
        # Both must use ON CONFLICT DO UPDATE (idempotent upsert)
        sql1 = cur1.execute.call_args_list[0][0][0]
        sql2 = cur2.execute.call_args_list[0][0][0]
        self.assertIn("ON CONFLICT", sql1)
        self.assertIn("ON CONFLICT", sql2)

    def test_crash_safe_sets_crash_exit_true(self):
        conn, _ = _make_conn()
        result = SessionSummary.write_crash_safe(conn, date(2026, 4, 23))
        self.assertTrue(result.crash_exit)

    def test_crash_safe_accepts_notes(self):
        conn, _ = _make_conn()
        result = SessionSummary.write_crash_safe(
            conn, date(2026, 4, 23), notes="Killed by SIGTERM at 14:32"
        )
        self.assertEqual(result.notes, "Killed by SIGTERM at 14:32")


class TestSessionSummaryBuildFromTrades(unittest.TestCase):

    def test_empty_trades(self):
        s = SessionSummary.build_from_trades([])
        self.assertEqual(s.trade_count, 0)
        self.assertEqual(s.gross_pnl, 0.0)
        self.assertEqual(s.win_count, 0)

    def test_win_loss_count(self):
        trades = [
            _make_trade(pnl=100.0, exit_time=datetime(2026, 4, 23, 10, tzinfo=timezone.utc)),
            _make_trade(pnl=-50.0, exit_time=datetime(2026, 4, 23, 11, tzinfo=timezone.utc)),
            _make_trade(pnl=200.0, exit_time=datetime(2026, 4, 23, 12, tzinfo=timezone.utc)),
        ]
        s = SessionSummary.build_from_trades(trades)
        self.assertEqual(s.trade_count, 3)
        self.assertEqual(s.win_count, 2)
        self.assertAlmostEqual(s.gross_pnl, 250.0)

    def test_max_drawdown(self):
        # equity sequence: 0 → +100 → +50 → +250; peak=250, trough=50, dd=0
        # Actually: 0 → 100 (peak 100) → 50 (dd 50) → 250 (no new dd)
        trades = [
            _make_trade(pnl=100.0, entry_time=datetime(2026, 4, 23, 9, 35, tzinfo=timezone.utc),
                        exit_time=datetime(2026, 4, 23, 10, 0, tzinfo=timezone.utc)),
            _make_trade(pnl=-50.0, entry_time=datetime(2026, 4, 23, 10, 5, tzinfo=timezone.utc),
                        exit_time=datetime(2026, 4, 23, 11, 0, tzinfo=timezone.utc)),
            _make_trade(pnl=200.0, entry_time=datetime(2026, 4, 23, 11, 5, tzinfo=timezone.utc),
                        exit_time=datetime(2026, 4, 23, 12, 0, tzinfo=timezone.utc)),
        ]
        s = SessionSummary.build_from_trades(trades, start_equity=0.0)
        self.assertAlmostEqual(s.max_drawdown, 50.0)


# ── _row_to_trade_kwargs tests ────────────────────────────────────────────────

class TestRowToTradeKwargs(unittest.TestCase):

    def test_strips_non_dataclass_fields(self):
        row = {
            "id": 1,
            "session_date": date(2026, 4, 23),
            "symbol": "NQ",
            "direction": "LONG",
            "entry_price": 17850.0,
            "entry_time": datetime(2026, 4, 23, 9, 35, tzinfo=timezone.utc),
            "created_at": datetime(2026, 4, 23, 9, 35, tzinfo=timezone.utc),  # not in dataclass
            "updated_at": datetime(2026, 4, 23, 9, 35, tzinfo=timezone.utc),  # not in dataclass
        }
        kwargs = _row_to_trade_kwargs(row)
        self.assertNotIn("created_at", kwargs)
        self.assertNotIn("updated_at", kwargs)
        self.assertIn("id", kwargs)
        self.assertIn("entry_time", kwargs)


# ── pytest-marker tests for write_crash_safe and for_date ────────────────────
# These four tests are tagged @pytest.mark.fast so they run in the
# pre-commit fast gate (pytest -m fast).



@pytest.mark.fast
def test_write_crash_safe_inserts_on_new():
    """write_crash_safe on a fresh conn issues an INSERT (no prior row)."""
    conn, cur = _make_conn(fetchone_return=None)
    result = SessionSummary.write_crash_safe(conn, date(2026, 4, 23))
    assert isinstance(result, SessionSummary)
    # cur.execute is called at least once — the INSERT inside save()
    assert cur.execute.called
    sql = cur.execute.call_args_list[0][0][0]
    assert "INSERT INTO session_summary" in sql


@pytest.mark.fast
def test_write_crash_safe_updates_on_existing():
    """write_crash_safe always uses ON CONFLICT DO UPDATE (idempotent upsert)."""
    conn, cur = _make_conn(fetchone_return=None)
    # First call
    SessionSummary.write_crash_safe(conn, date(2026, 4, 23))
    # Second call on a fresh mock simulating an existing row
    conn2, cur2 = _make_conn(fetchone_return=None)
    SessionSummary.write_crash_safe(conn2, date(2026, 4, 23))
    sql2 = cur2.execute.call_args_list[0][0][0]
    # The upsert SQL must contain ON CONFLICT DO UPDATE (not a bare INSERT)
    assert "ON CONFLICT" in sql2
    assert "DO UPDATE" in sql2


@pytest.mark.fast
def test_for_date_returns_empty_when_missing():
    """for_date returns an empty list (not None) when no rows exist."""
    conn, cur = _make_conn(fetchall_return=[])
    result = SessionSummary.for_date(conn, date(2026, 4, 23))
    assert result == []


@pytest.mark.fast
def test_for_date_returns_summary_when_found():
    """for_date returns a populated SessionSummary list when a row exists."""
    row = {
        "session_id": "2026-04-23_python",
        "date": date(2026, 4, 23),
        "source": "python",
        "gross_pnl": 250.0,
        "trade_count": 3,
        "win_count": 2,
        "max_drawdown": 50.0,
        "start_equity": 50000.0,
        "end_equity": 50250.0,
        "notes": None,
        "crash_exit": False,
    }
    conn, cur = _make_conn(fetchall_return=[row])
    results = SessionSummary.for_date(conn, date(2026, 4, 23))
    assert len(results) == 1
    s = results[0]
    assert isinstance(s, SessionSummary)
    assert s.session_id == "2026-04-23_python"
    assert s.trade_count == 3
    assert s.gross_pnl == 250.0
    assert s.win_count == 2


# ── Trade.for_date and Trade.get unit tests  (M6) ────────────────────────────

def _full_trade_row(**overrides) -> dict:
    """Return a complete row dict matching all Trade dataclass fields."""
    row = {
        "id": 1,
        "session_date": date(2026, 4, 23),
        "symbol": "NQ",
        "direction": "LONG",
        "entry_price": 17850.0,
        "entry_time": datetime(2026, 4, 23, 9, 35, tzinfo=timezone.utc),
        "exit_price": 17900.0,
        "exit_time": datetime(2026, 4, 23, 10, 0, tzinfo=timezone.utc),
        "quantity": 1,
        "pnl": 250.0,
        "pnl_points": 50.0,
        "stop_loss": 17820.0,
        "target": 17900.0,
        "exit_reason": "TARGET_HIT",
        "dry_run": False,
        "source": "python",
        "session_id": "2026-04-23_python",
        "ml_prediction": None,
        "ml_confidence": None,
    }
    row.update(overrides)
    return row


@pytest.mark.fast
def test_trade_for_date_returns_empty_when_no_rows():
    """Trade.for_date returns [] when cursor.fetchall() returns no rows."""
    conn, cur = _make_conn(fetchall_return=[])
    result = Trade.for_date(conn, date(2026, 4, 23))
    assert result == []


@pytest.mark.fast
def test_trade_for_date_returns_list_when_found():
    """Trade.for_date returns a list with one Trade when one row is returned."""
    row = _full_trade_row()
    conn, cur = _make_conn(fetchall_return=[row])
    results = Trade.for_date(conn, date(2026, 4, 23))
    assert len(results) == 1
    t = results[0]
    assert isinstance(t, Trade)
    assert t.direction == "LONG"
    assert t.entry_price == 17850.0
    assert t.pnl == 250.0


@pytest.mark.fast
def test_trade_get_returns_none_when_missing():
    """Trade.get returns None when cursor.fetchone() returns None."""
    conn, cur = _make_conn(fetchone_return=None)
    result = Trade.get(conn, trade_id=999)
    assert result is None


@pytest.mark.fast
def test_trade_get_returns_trade_when_found():
    """Trade.get returns a Trade object when cursor.fetchone() returns a row dict."""
    row = _full_trade_row(id=42, direction="SHORT", entry_price=18000.0)
    conn, cur = _make_conn(fetchone_return=row)
    result = Trade.get(conn, trade_id=42)
    assert result is not None
    assert isinstance(result, Trade)
    assert result.id == 42
    assert result.direction == "SHORT"
    assert result.entry_price == 18000.0


# ── get_conn() unit tests ─────────────────────────────────────────────────────

@pytest.mark.fast
def test_get_conn_passes_pg_env_vars_to_connect():
    """get_conn() forwards PG_* env vars to psycopg2.connect."""
    env = {
        "PG_HOST": "db.example.com",
        "PG_PORT": "5433",
        "PG_DB": "mydb",
        "PG_USER": "myuser",
        "PG_PASSWORD": "secret",
    }
    with patch.dict("os.environ", env, clear=False), \
         patch("models.psycopg2.connect") as mock_connect:
        get_conn()
    mock_connect.assert_called_once()
    kwargs = mock_connect.call_args[1]
    assert kwargs["host"] == "db.example.com"
    assert kwargs["port"] == 5433
    assert kwargs["dbname"] == "mydb"
    assert kwargs["user"] == "myuser"
    assert kwargs["password"] == "secret"


@pytest.mark.fast
def test_get_conn_uses_defaults_when_env_absent():
    """get_conn() falls back to defaults when PG_* vars are not set.

    _load_env is patched to no-op so .env file values don't interfere.
    """
    no_pg = {k: v for k, v in __import__("os").environ.items()
             if not k.startswith("PG_")}
    with patch("models._load_env"), \
         patch.dict("os.environ", no_pg, clear=True), \
         patch("models.psycopg2.connect") as mock_connect:
        get_conn()
    kwargs = mock_connect.call_args[1]
    assert kwargs["host"] == "localhost"
    assert kwargs["port"] == 5432
    assert kwargs["dbname"] == "rithmic"
    assert kwargs["user"] == "rithmic_user"


# ── build_from_trades edge cases ──────────────────────────────────────────────

@pytest.mark.fast
def test_build_from_trades_all_loss():
    """build_from_trades with only losing trades: win_count=0, gross_pnl negative."""
    trades = [
        _make_trade(pnl=-100.0),
        _make_trade(pnl=-50.0),
    ]
    s = SessionSummary.build_from_trades(trades)
    assert s.win_count == 0
    assert s.trade_count == 2
    assert s.gross_pnl == pytest.approx(-150.0)


@pytest.mark.fast
def test_build_from_trades_start_equity_affects_end_equity():
    """build_from_trades uses start_equity to compute end_equity."""
    trades = [_make_trade(pnl=200.0)]
    s = SessionSummary.build_from_trades(trades, start_equity=50000.0)
    assert s.start_equity == 50000.0
    assert s.end_equity == pytest.approx(50200.0)


@pytest.mark.fast
def test_build_from_trades_max_drawdown_from_positive_start():
    """Drawdown calculation uses start_equity as initial equity baseline."""
    # equity: 50000 → 49900 (dd=100) → 49800 (dd=200) → 50050 (no new dd)
    trades = [
        _make_trade(pnl=-100.0),
        _make_trade(pnl=-100.0),
        _make_trade(pnl=250.0),
    ]
    s = SessionSummary.build_from_trades(trades, start_equity=50000.0)
    assert s.max_drawdown == pytest.approx(200.0)


if __name__ == "__main__":
    unittest.main()
