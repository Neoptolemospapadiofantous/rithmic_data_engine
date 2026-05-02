"""
tests/test_audit_data.py — Unit tests for audit_data.py.

Coverage:
  - Module imports cleanly
  - result() formatting function produces correct padded output
  - section() prints separator lines
  - _pg_connstr() builds connection string from environment variables
  - _load_env() reads key=value pairs from a .env file
  - check_schema() handles missing parquet files (FAIL branch)
  - check_date_range() returns None when PG table is empty
  - check_tick_counts() handles empty parquet data (FAIL branch)
  - check_bars() returns True when bars_1min is empty (non-fatal)
  - check_side_parity() handles empty parquet data (FAIL branch)
  - check_timestamp_precision() always returns True
  - check_gaps() returns True when no PG-only missing days
  - main() exits 1 gracefully when PG is unavailable (no unhandled exception)

All tests marked @pytest.mark.fast — no live DB, no subprocess, no network.
DB-dependent functions use unittest.mock.patch / MagicMock.
"""
from __future__ import annotations

import os
import sys
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

REPO_ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(REPO_ROOT))


# ── Helpers ────────────────────────────────────────────────────────────────────

def _make_conn(fetchone_value=None, fetchall_value=None):
    """Return a MagicMock psycopg2 connection."""
    conn = MagicMock()
    cur = MagicMock()
    cur.fetchone.return_value = fetchone_value
    cur.fetchall.return_value = fetchall_value if fetchall_value is not None else []
    conn.cursor.return_value = cur
    return conn


# ── Import smoke test ──────────────────────────────────────────────────────────

@pytest.mark.fast
def test_module_imports_cleanly():
    """audit_data module must import without side effects or exceptions."""
    import audit_data  # noqa: F401 — import is the test
    assert hasattr(audit_data, "main")
    assert hasattr(audit_data, "check_schema")
    assert hasattr(audit_data, "check_date_range")
    assert hasattr(audit_data, "check_tick_counts")
    assert hasattr(audit_data, "check_bars")
    assert hasattr(audit_data, "check_side_parity")
    assert hasattr(audit_data, "check_timestamp_precision")
    assert hasattr(audit_data, "check_gaps")


# ── _load_env ─────────────────────────────────────────────────────────────────

@pytest.mark.fast
def test_load_env_reads_key_value_pairs(tmp_path, monkeypatch):
    """_load_env() must set env vars from a .env file, skipping comments and blanks."""
    import audit_data

    env_file = tmp_path / ".env"
    env_file.write_text(
        "# this is a comment\n"
        "\n"
        "PG_TEST_HOST=myhost\n"
        "PG_TEST_PORT=9999\n"
    )
    monkeypatch.setattr(audit_data, "ENGINE_DIR", tmp_path)
    # Ensure the keys are not already set
    monkeypatch.delenv("PG_TEST_HOST", raising=False)
    monkeypatch.delenv("PG_TEST_PORT", raising=False)

    audit_data._load_env()

    assert os.environ.get("PG_TEST_HOST") == "myhost"
    assert os.environ.get("PG_TEST_PORT") == "9999"


@pytest.mark.fast
def test_load_env_does_not_override_existing(tmp_path, monkeypatch):
    """_load_env() must not overwrite already-set environment variables."""
    import audit_data

    env_file = tmp_path / ".env"
    env_file.write_text("PG_EXISTING_VAR=from_file\n")
    monkeypatch.setattr(audit_data, "ENGINE_DIR", tmp_path)
    monkeypatch.setenv("PG_EXISTING_VAR", "original")

    audit_data._load_env()

    assert os.environ["PG_EXISTING_VAR"] == "original"


@pytest.mark.fast
def test_load_env_no_file_is_noop(tmp_path, monkeypatch):
    """_load_env() must do nothing if .env does not exist."""
    import audit_data

    monkeypatch.setattr(audit_data, "ENGINE_DIR", tmp_path)
    # Should not raise
    audit_data._load_env()


# ── _pg_connstr ───────────────────────────────────────────────────────────────

@pytest.mark.fast
def test_pg_connstr_uses_defaults(monkeypatch):
    """_pg_connstr() must produce a connstr with defaults when env vars absent."""
    import audit_data

    for var in ("PG_HOST", "PG_PORT", "PG_DB", "PG_USER", "PG_PASSWORD"):
        monkeypatch.delenv(var, raising=False)

    connstr = audit_data._pg_connstr()
    assert "host=localhost" in connstr
    assert "port=5432" in connstr
    assert "dbname=rithmic" in connstr
    assert "user=rithmic_user" in connstr


@pytest.mark.fast
def test_pg_connstr_uses_env_overrides(monkeypatch):
    """_pg_connstr() must incorporate custom env vars."""
    import audit_data

    monkeypatch.setenv("PG_HOST", "db.example.com")
    monkeypatch.setenv("PG_PORT", "5433")
    monkeypatch.setenv("PG_DB", "mydb")
    monkeypatch.setenv("PG_USER", "admin")
    monkeypatch.setenv("PG_PASSWORD", "s3cr3t")

    connstr = audit_data._pg_connstr()
    assert "host=db.example.com" in connstr
    assert "port=5433" in connstr
    assert "dbname=mydb" in connstr
    assert "user=admin" in connstr
    assert "password=s3cr3t" in connstr


# ── result() and section() ────────────────────────────────────────────────────

@pytest.mark.fast
def test_result_formats_label_and_status(capsys):
    """result() must print label, status token, and optional detail on one line."""
    import audit_data

    audit_data.result("Schema OK", audit_data.PASS, "detail here")
    captured = capsys.readouterr()
    assert "Schema OK" in captured.out
    assert "detail here" in captured.out


@pytest.mark.fast
def test_result_no_detail(capsys):
    """result() must not crash when detail is omitted."""
    import audit_data

    audit_data.result("Some check", audit_data.FAIL)
    captured = capsys.readouterr()
    assert "Some check" in captured.out


@pytest.mark.fast
def test_section_prints_separator(capsys):
    """section() must print a separator line and the title."""
    import audit_data

    audit_data.section("My Section Title")
    captured = capsys.readouterr()
    assert "My Section Title" in captured.out
    assert "─" in captured.out


# ── check_schema ──────────────────────────────────────────────────────────────

@pytest.mark.fast
def test_check_schema_fail_no_parquet(tmp_path, monkeypatch, capsys):
    """check_schema() returns False when no parquet files exist."""
    import audit_data

    monkeypatch.setattr(audit_data, "PARQUET_DIR", tmp_path / "empty_parquet")
    (tmp_path / "empty_parquet").mkdir()

    conn = _make_conn(fetchall_value=[])
    result = audit_data.check_schema(conn)

    assert result is False
    out = capsys.readouterr().out
    assert "FAIL" in out or "fail" in out.lower()


@pytest.mark.fast
def test_check_schema_pass_with_mocked_parquet(tmp_path, monkeypatch, capsys):
    """check_schema() returns True when PG and parquet both have required columns."""
    import pandas as pd
    import audit_data

    parquet_dir = tmp_path / "parquet"
    parquet_dir.mkdir()

    # Create a minimal parquet file with required columns
    df = pd.DataFrame({
        "ts_event": pd.to_datetime(["2025-01-01"], utc=True),
        "price": [18000.0],
        "size": pd.array([10], dtype="uint32"),
        "side": ["B"],
        "is_buy": [True],
    })
    parquet_file = parquet_dir / "2025-01.parquet"
    df.to_parquet(parquet_file)

    monkeypatch.setattr(audit_data, "PARQUET_DIR", parquet_dir)

    # PG returns all required columns
    pg_cols = [
        ("ts_event", "timestamp with time zone", "NO"),
        ("price", "double precision", "NO"),
        ("size", "bigint", "NO"),
        ("side", "character varying", "YES"),
        ("is_buy", "boolean", "NO"),
    ]
    conn = _make_conn(fetchall_value=pg_cols)
    result = audit_data.check_schema(conn)

    assert result is True


# ── check_date_range ──────────────────────────────────────────────────────────

@pytest.mark.fast
def test_check_date_range_empty_pg(tmp_path, monkeypatch, capsys):
    """check_date_range() returns None when PG ticks table is empty."""
    import audit_data

    monkeypatch.setattr(audit_data, "PARQUET_DIR", tmp_path)
    conn = _make_conn(fetchone_value=(None, None))
    result = audit_data.check_date_range(conn)

    assert result is None


@pytest.mark.fast
def test_check_date_range_no_parquet_files(tmp_path, monkeypatch, capsys):
    """check_date_range() returns None when parquet dir has no files."""
    import audit_data

    parquet_dir = tmp_path / "parquet"
    parquet_dir.mkdir()
    monkeypatch.setattr(audit_data, "PARQUET_DIR", parquet_dir)

    ts_start = datetime(2025, 1, 1, tzinfo=timezone.utc)
    ts_end = datetime(2025, 6, 1, tzinfo=timezone.utc)
    conn = _make_conn(fetchone_value=(ts_start, ts_end))
    result = audit_data.check_date_range(conn)

    assert result is None


# ── check_tick_counts ─────────────────────────────────────────────────────────

@pytest.mark.fast
def test_check_tick_counts_fail_empty_parquet(tmp_path, monkeypatch, capsys):
    """check_tick_counts() returns False when parquet data is empty for the window."""
    import audit_data

    parquet_dir = tmp_path / "parquet"
    parquet_dir.mkdir()
    monkeypatch.setattr(audit_data, "PARQUET_DIR", parquet_dir)

    start = datetime(2025, 3, 1, tzinfo=timezone.utc)
    end = datetime(2025, 3, 31, tzinfo=timezone.utc)

    conn = _make_conn(fetchall_value=[("2025-03-01", 1000)])

    with patch.object(audit_data, "_load_parquet") as mock_load:
        import pandas as pd
        mock_load.return_value = pd.DataFrame()
        result = audit_data.check_tick_counts(conn, start, end)

    assert result is False


@pytest.mark.fast
def test_check_tick_counts_pass_within_threshold(tmp_path, monkeypatch, capsys):
    """check_tick_counts() returns True when PG and parquet totals are within 20%."""
    import pandas as pd
    import audit_data

    start = datetime(2025, 3, 1, tzinfo=timezone.utc)
    end = datetime(2025, 3, 2, tzinfo=timezone.utc)

    conn = _make_conn(fetchall_value=[("2025-03-01", 1000)])

    parquet_df = pd.DataFrame({
        "ts_event": pd.to_datetime(["2025-03-01 15:00:00"] * 950, utc=True),
        "price": [18000.0] * 950,
        "is_buy": [True] * 950,
        "size": [1] * 950,
    })

    with patch.object(audit_data, "_load_parquet", return_value=parquet_df):
        result = audit_data.check_tick_counts(conn, start, end)

    assert result is True


# ── check_bars ────────────────────────────────────────────────────────────────

@pytest.mark.fast
def test_check_bars_nonfatal_when_no_pg_bars(capsys):
    """check_bars() returns True (non-fatal) when bars_1min has no rows."""
    import audit_data

    start = datetime(2025, 3, 1, tzinfo=timezone.utc)
    end = datetime(2025, 3, 5, tzinfo=timezone.utc)

    conn = _make_conn(fetchall_value=[])

    result = audit_data.check_bars(conn, start, end)

    assert result is True
    out = capsys.readouterr().out
    assert "WARN" in out or "empty" in out.lower() or "not populated" in out.lower()


# ── check_side_parity ─────────────────────────────────────────────────────────

@pytest.mark.fast
def test_check_side_parity_fail_empty_parquet(capsys):
    """check_side_parity() returns False when parquet is empty."""
    import pandas as pd
    import audit_data

    start = datetime(2025, 3, 1, tzinfo=timezone.utc)
    end = datetime(2025, 3, 2, tzinfo=timezone.utc)

    conn = _make_conn(fetchall_value=[(True, 500), (False, 500)])

    with patch.object(audit_data, "_load_parquet") as mock_load:
        mock_load.return_value = pd.DataFrame()
        result = audit_data.check_side_parity(conn, start, end)

    assert result is False


@pytest.mark.fast
def test_check_side_parity_pass_close_ratios(capsys):
    """check_side_parity() returns True when buy ratios differ by < 2pp."""
    import pandas as pd
    import audit_data

    start = datetime(2025, 3, 1, tzinfo=timezone.utc)
    end = datetime(2025, 3, 2, tzinfo=timezone.utc)

    # PG: 500 buy, 500 sell → 50% buy
    conn = _make_conn(fetchall_value=[(True, 500), (False, 500)])

    # Parquet: 510 buy, 490 sell → 51% buy (delta = 1pp < 2pp threshold)
    parquet_df = pd.DataFrame({
        "ts_event": pd.to_datetime(["2025-03-01"] * 1000, utc=True),
        "price": [18000.0] * 1000,
        "size": [1] * 1000,
        "is_buy": [True] * 510 + [False] * 490,
    })

    with patch.object(audit_data, "_load_parquet", return_value=parquet_df):
        result = audit_data.check_side_parity(conn, start, end)

    assert result is True


# ── check_timestamp_precision ─────────────────────────────────────────────────

@pytest.mark.fast
def test_check_timestamp_precision_always_true(capsys):
    """check_timestamp_precision() always returns True (informational only)."""
    import pandas as pd
    import audit_data

    start = datetime(2025, 3, 1, tzinfo=timezone.utc)
    end = datetime(2025, 3, 2, tzinfo=timezone.utc)

    conn = _make_conn(fetchone_value=(1,))

    parquet_df = pd.DataFrame({
        "ts_event": pd.to_datetime(["2025-03-01 15:00:00.000001"] * 5, utc=True),
        "price": [18000.0] * 5,
        "size": [1] * 5,
        "is_buy": [True] * 5,
    })

    with patch.object(audit_data, "_load_parquet", return_value=parquet_df):
        result = audit_data.check_timestamp_precision(conn, start, end)

    assert result is True


# ── check_gaps ────────────────────────────────────────────────────────────────

@pytest.mark.fast
def test_check_gaps_pass_no_pg_only_missing(capsys):
    """check_gaps() returns True when no days are missing from PG only."""
    import pandas as pd
    import audit_data

    start = datetime(2025, 3, 3, tzinfo=timezone.utc)   # Monday
    end = datetime(2025, 3, 7, tzinfo=timezone.utc)     # Friday (5 weekdays total Mon-Fri)

    # PG has all weekdays
    pg_days = [
        ("2025-03-03",), ("2025-03-04",), ("2025-03-05",),
        ("2025-03-06",), ("2025-03-07",),
    ]
    conn = _make_conn(fetchall_value=pg_days)

    parquet_df = pd.DataFrame({
        "ts_event": pd.to_datetime(
            ["2025-03-03", "2025-03-04", "2025-03-05", "2025-03-06", "2025-03-07"],
            utc=True,
        ),
        "price": [18000.0] * 5,
        "size": [1] * 5,
        "is_buy": [True] * 5,
    })

    with patch.object(audit_data, "_load_parquet", return_value=parquet_df):
        result = audit_data.check_gaps(conn, start, end)

    assert result is True


# ── main() CLI ────────────────────────────────────────────────────────────────

@pytest.mark.fast
def test_main_exits_1_on_pg_connection_failure(monkeypatch, tmp_path):
    """main() must exit with code 1 when PG is unreachable — no unhandled exception."""
    import audit_data

    parquet_dir = tmp_path / "parquet"
    parquet_dir.mkdir()
    monkeypatch.setattr(audit_data, "PARQUET_DIR", parquet_dir)

    # Make _pg_connect raise a connection error
    def _fail_connect():
        raise Exception("Connection refused (mocked)")

    monkeypatch.setattr(audit_data, "_pg_connect", _fail_connect)
    monkeypatch.setattr(audit_data, "_load_env", lambda: None)

    with pytest.raises(SystemExit) as exc_info:
        with patch("sys.argv", ["audit_data.py"]):
            audit_data.main()

    assert exc_info.value.code == 1


@pytest.mark.fast
def test_main_exits_1_on_missing_parquet_dir(monkeypatch, tmp_path):
    """main() must exit with code 1 when PARQUET_DIR does not exist."""
    import audit_data

    missing_dir = tmp_path / "does_not_exist"
    monkeypatch.setattr(audit_data, "PARQUET_DIR", missing_dir)

    # PG connection succeeds but parquet dir is absent
    mock_conn = MagicMock()
    monkeypatch.setattr(audit_data, "_pg_connect", lambda: mock_conn)
    monkeypatch.setattr(audit_data, "_load_env", lambda: None)

    with pytest.raises(SystemExit) as exc_info:
        with patch("sys.argv", ["audit_data.py"]):
            audit_data.main()

    assert exc_info.value.code == 1
