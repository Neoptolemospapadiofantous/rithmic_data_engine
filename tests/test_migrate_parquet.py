#!/usr/bin/env python3
"""
test_migrate_parquet.py — Unit tests for migrate_parquet.py helper functions.

Covers:
  - _load_progress()  : missing file returns default dict; existing file returns parsed JSON
  - _save_progress()  : writes JSON atomically (via .tmp rename)
  - _prep_df()        : column normalisation, dtype coercion, deduplication,
                        aggressor_side promotion, missing-column rejection, empty-df guard

All tests marked @pytest.mark.fast — no DB, no network, no subprocess.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path
from unittest.mock import patch

import pandas as pd
import pytest

# ── make migrate_parquet importable ───────────────────────────────────────────
REPO_ROOT = Path(__file__).parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

import migrate_parquet as mp

pytestmark = pytest.mark.fast


# ── helpers ───────────────────────────────────────────────────────────────────

def _make_valid_df() -> pd.DataFrame:
    """Return a minimal valid DataFrame that _prep_df accepts."""
    return pd.DataFrame(
        {
            "ts_event": pd.to_datetime(
                ["2025-01-02 09:30:00+00:00", "2025-01-02 09:30:01+00:00"]
            ),
            "price": [18000.25, 18001.50],
            "size": [1, 2],
            "side": ["B", "A"],
            "is_buy": [True, False],
        }
    )


# ── _load_progress ────────────────────────────────────────────────────────────

class TestLoadProgress:
    def test_missing_file_returns_default(self, tmp_path, monkeypatch):
        """When the progress file does not exist, return the default sentinel dict."""
        monkeypatch.setattr(mp, "PROGRESS_FILE", tmp_path / "nonexistent.json")

        result = mp._load_progress()

        assert result == {"completed": [], "total_inserted": 0}

    def test_existing_file_returns_parsed_json(self, tmp_path, monkeypatch):
        """When the progress file contains valid JSON, return it as a dict."""
        data = {"completed": ["2025-01", "2025-02"], "total_inserted": 42}
        progress_file = tmp_path / "migrate_progress.json"
        progress_file.write_text(json.dumps(data))
        monkeypatch.setattr(mp, "PROGRESS_FILE", progress_file)

        result = mp._load_progress()

        assert result == data

    def test_corrupt_file_returns_default(self, tmp_path, monkeypatch):
        """When the progress file contains invalid JSON, return the default dict."""
        progress_file = tmp_path / "migrate_progress.json"
        progress_file.write_text("NOT VALID JSON {{{")
        monkeypatch.setattr(mp, "PROGRESS_FILE", progress_file)

        result = mp._load_progress()

        assert result == {"completed": [], "total_inserted": 0}


# ── _save_progress ────────────────────────────────────────────────────────────

class TestSaveProgress:
    def test_writes_correct_json(self, tmp_path, monkeypatch):
        """_save_progress() writes the dict as indented JSON and the file is readable back."""
        progress_file = tmp_path / "data" / "migrate_progress.json"
        monkeypatch.setattr(mp, "PROGRESS_FILE", progress_file)

        data = {"completed": ["2025-03"], "total_inserted": 777}
        mp._save_progress(data)

        assert progress_file.exists(), "Progress file was not created"
        loaded = json.loads(progress_file.read_text())
        assert loaded == data

    def test_creates_parent_dirs(self, tmp_path, monkeypatch):
        """_save_progress() creates parent directories if they do not exist."""
        nested = tmp_path / "a" / "b" / "c" / "progress.json"
        monkeypatch.setattr(mp, "PROGRESS_FILE", nested)

        mp._save_progress({"completed": [], "total_inserted": 0})

        assert nested.exists()

    def test_atomic_write_cleans_up_tmp(self, tmp_path, monkeypatch):
        """The intermediate .tmp file must not survive after a successful save."""
        progress_file = tmp_path / "migrate_progress.json"
        monkeypatch.setattr(mp, "PROGRESS_FILE", progress_file)

        mp._save_progress({"completed": [], "total_inserted": 0})

        tmp_file = progress_file.with_suffix(".tmp")
        assert not tmp_file.exists(), ".tmp file was not cleaned up"


# ── _prep_df ──────────────────────────────────────────────────────────────────

class TestPrepDf:
    def _call(self, df: pd.DataFrame) -> "pd.DataFrame | None":
        """Patch pd.read_parquet so _prep_df uses the supplied DataFrame."""
        dummy_path = Path("/fake/2025-01.parquet")
        with patch("migrate_parquet.pd.read_parquet", return_value=df):
            return mp._prep_df(dummy_path)

    # ── basic happy path ──────────────────────────────────────────────────────

    def test_valid_df_passes_through(self):
        """A well-formed DataFrame is returned with correct dtypes."""
        df_in = _make_valid_df()
        result = self._call(df_in)

        assert result is not None
        assert result["price"].dtype == "float64"
        assert result["size"].dtype == "int64"
        assert result["is_buy"].dtype == "bool"

    # ── missing required column ───────────────────────────────────────────────

    def test_missing_price_column_returns_none(self):
        """DataFrame without 'price' column → _prep_df returns None."""
        df = _make_valid_df().drop(columns=["price"])
        result = self._call(df)
        assert result is None

    def test_missing_size_column_returns_none(self):
        """DataFrame without 'size' column → _prep_df returns None."""
        df = _make_valid_df().drop(columns=["size"])
        result = self._call(df)
        assert result is None

    def test_missing_is_buy_column_returns_none(self):
        """DataFrame without 'is_buy' (and no aggressor_side) → returns None."""
        df = _make_valid_df().drop(columns=["is_buy"])
        result = self._call(df)
        assert result is None

    # ── empty DataFrame ───────────────────────────────────────────────────────

    def test_empty_df_returns_none(self):
        """An empty DataFrame (zero rows) → _prep_df returns None."""
        df = pd.DataFrame(columns=["ts_event", "price", "size", "side", "is_buy"])
        result = self._call(df)
        assert result is None

    # ── aggressor_side promotion ──────────────────────────────────────────────

    def test_aggressor_side_promotes_to_side_and_is_buy(self):
        """When only 'aggressor_side' is present, _prep_df derives 'side' and 'is_buy'."""
        df = pd.DataFrame(
            {
                "ts_event": pd.to_datetime(["2025-01-02 09:30:00+00:00"]),
                "price": [18000.0],
                "size": [1],
                "aggressor_side": ["B"],
            }
        )
        result = self._call(df)
        assert result is not None
        assert "side" in result.columns
        assert "is_buy" in result.columns
        assert result["is_buy"].iloc[0] is True or bool(result["is_buy"].iloc[0])

    # ── deduplication ─────────────────────────────────────────────────────────

    def test_within_file_duplicates_are_dropped(self):
        """Rows with the same µs-timestamp, price, and size are deduplicated."""
        ts = pd.Timestamp("2025-01-02 09:30:00.000001+00:00")
        df = pd.DataFrame(
            {
                "ts_event": [ts, ts],          # identical µs timestamp
                "price": [18000.0, 18000.0],   # same price
                "size": [1, 1],                # same size
                "side": ["B", "B"],
                "is_buy": [True, True],
            }
        )
        result = self._call(df)
        assert result is not None
        assert len(result) == 1, f"Expected 1 row after dedup, got {len(result)}"

    def test_distinct_rows_are_kept(self):
        """Rows with different prices are not deduplicated."""
        ts = pd.Timestamp("2025-01-02 09:30:00.000001+00:00")
        df = pd.DataFrame(
            {
                "ts_event": [ts, ts],
                "price": [18000.0, 18001.0],   # different price → keep both
                "size": [1, 1],
                "side": ["B", "A"],
                "is_buy": [True, False],
            }
        )
        result = self._call(df)
        assert result is not None
        assert len(result) == 2

    # ── side normalisation ────────────────────────────────────────────────────

    def test_invalid_side_values_become_none(self):
        """Side values outside {B, A} are replaced with None."""
        df = _make_valid_df().copy()
        df["side"] = ["X", "Y"]               # both invalid
        result = self._call(df)
        assert result is not None
        assert result["side"].isna().all(), "Invalid side values should be NaN/None"
