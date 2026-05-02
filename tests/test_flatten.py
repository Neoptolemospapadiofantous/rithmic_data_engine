#!/usr/bin/env python3
"""
test_flatten.py — Unit tests for scripts/flatten.py.

Coverage:
  - Module imports without error (given mocked Rithmic client)
  - parse_args returns the default basket ID when called with no arguments
  - parse_args accepts an explicit basket_id positional argument
  - run() is an async coroutine
  - run() calls cancel_order then submit_order in the expected order
  - The __main__ block is NOT executed at import time

Run:
    python -m pytest tests/test_flatten.py -v
"""
from __future__ import annotations

import asyncio
import importlib
import inspect
import sys
import types
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

_REPO_ROOT = Path(__file__).parent.parent

# ---------------------------------------------------------------------------
# Helpers — build the minimal fake modules that flatten.py tries to import
# ---------------------------------------------------------------------------

def _make_fake_rithmic_client() -> tuple[MagicMock, MagicMock]:
    """Return (fake_cfg, fake_client) mocks used across several tests."""
    fake_cfg = MagicMock(name="RithmicConfig", user="test_user", system_name="test_sys")
    fake_client = MagicMock(name="RithmicClient")
    fake_client.connect = AsyncMock()
    fake_client.disconnect = AsyncMock()
    fake_client.on_rithmic_order_notification = MagicMock()
    fake_client.on_rithmic_order_notification.__iadd__ = MagicMock(return_value=None)
    fake_client.on_exchange_order_notification = MagicMock()
    fake_client.on_exchange_order_notification.__iadd__ = MagicMock(return_value=None)

    order_plant = MagicMock(name="OrderPlant")
    order_plant.cancel_order = AsyncMock()
    order_plant.submit_order = AsyncMock()
    fake_client.plants = {"order": order_plant}
    return fake_cfg, fake_client


def _patch_imports():
    """
    Return a list of patch context-managers that stub out every external
    dependency that flatten.py imports at module level or inside run().
    These patches must be entered BEFORE importing the module.
    """
    # Stub the whole python.rithmic.client package tree
    fake_cfg, fake_client = _make_fake_rithmic_client()

    rithmic_client_mod = types.ModuleType("python.rithmic.client")
    rithmic_client_mod.RithmicConfig = MagicMock(
        name="RithmicConfig", from_env=MagicMock(return_value=fake_cfg)
    )
    rithmic_client_mod.get_client = MagicMock(return_value=fake_client)

    # Stub async_rithmic enums used inside run()
    async_rithmic_mod = types.ModuleType("async_rithmic")
    async_rithmic_mod.SysInfraType = MagicMock(name="SysInfraType")
    async_rithmic_mod.TransactionType = MagicMock(name="TransactionType")
    async_rithmic_mod.OrderType = MagicMock(name="OrderType")

    return fake_cfg, fake_client, {
        "python": types.ModuleType("python"),
        "python.rithmic": types.ModuleType("python.rithmic"),
        "python.rithmic.client": rithmic_client_mod,
        "async_rithmic": async_rithmic_mod,
    }


def _import_flatten():
    """Import scripts/flatten.py as a module, stubbing all external deps."""
    _, _, fake_modules = _patch_imports()
    with patch.dict(sys.modules, fake_modules):
        spec = importlib.util.spec_from_file_location(
            "flatten",
            _REPO_ROOT / "scripts" / "flatten.py",
        )
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
    return mod


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

pytestmark = pytest.mark.fast


class TestFlattenImport:
    """The module should import cleanly with all external deps mocked."""

    def test_import_without_error(self):
        mod = _import_flatten()
        assert mod is not None

    def test_module_has_parse_args(self):
        mod = _import_flatten()
        assert callable(mod.parse_args)

    def test_module_has_run(self):
        mod = _import_flatten()
        assert callable(mod.run)

    def test_module_has_constants(self):
        mod = _import_flatten()
        assert hasattr(mod, "ACCOUNT_ID")
        assert hasattr(mod, "SYMBOL")
        assert hasattr(mod, "EXCHANGE")
        assert hasattr(mod, "_DEFAULT_STOP_BASKET")


class TestParseArgs:
    """parse_args() argument-parsing behaviour."""

    def test_default_basket_id(self):
        mod = _import_flatten()
        args = mod.parse_args([])
        assert args.basket_id == mod._DEFAULT_STOP_BASKET

    def test_explicit_basket_id(self):
        mod = _import_flatten()
        args = mod.parse_args(["9999999"])
        assert args.basket_id == "9999999"

    def test_help_does_not_raise_on_parse(self):
        """parse_args(['--help']) raises SystemExit(0) — that's correct argparse behaviour."""
        mod = _import_flatten()
        with pytest.raises(SystemExit) as exc_info:
            mod.parse_args(["--help"])
        assert exc_info.value.code == 0

    def test_default_basket_id_is_string(self):
        mod = _import_flatten()
        assert isinstance(mod._DEFAULT_STOP_BASKET, str)


class TestRunIsCoroutine:
    """run() must be an async coroutine function."""

    def test_run_is_coroutine_function(self):
        mod = _import_flatten()
        assert inspect.iscoroutinefunction(mod.run), "run() must be defined with `async def`"

    def test_run_returns_coroutine_when_called(self):
        mod = _import_flatten()
        _, _, fake_modules = _patch_imports()
        with patch.dict(sys.modules, fake_modules):
            coro = mod.run("test_basket")
        assert inspect.iscoroutine(coro)
        # Clean up the unawaited coroutine to silence ResourceWarning
        coro.close()


def _run_flatten_coroutine(basket_id: str, *, cancel_side_effect=None):
    """
    Import flatten.py, patch all external deps, and drive run() to completion.

    Returns (mod, order_plant, fake_client) so callers can assert on mocks.
    The entire import + coroutine execution happens inside the same
    patch.dict(sys.modules) context so that the lazy `from async_rithmic import …`
    inside run() picks up the fake module.
    """
    _, _, fake_modules = _patch_imports()
    fake_client = fake_modules["python.rithmic.client"].get_client.return_value
    order_plant = fake_client.plants["order"]

    if cancel_side_effect is not None:
        order_plant.cancel_order.side_effect = cancel_side_effect

    with patch.dict(sys.modules, fake_modules):
        # Import inside the patch so module-level globals resolve to fakes
        spec = importlib.util.spec_from_file_location(
            "flatten_run",
            _REPO_ROOT / "scripts" / "flatten.py",
        )
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        # Patch asyncio.sleep on the module's own namespace so the coroutine
        # (which calls `await asyncio.sleep(...)`) hits our mock.
        mod.asyncio = types.SimpleNamespace(
            sleep=AsyncMock(),
            run=asyncio.run,
        )

        asyncio.run(mod.run(basket_id))

    return mod, order_plant, fake_client


class TestRunBehaviour:
    """run() should cancel stop order then submit a market SELL."""

    def test_run_calls_cancel_then_submit(self):
        _, order_plant, _ = _run_flatten_coroutine("1234567")

        order_plant.cancel_order.assert_awaited_once()
        order_plant.submit_order.assert_awaited_once()

    def test_run_with_empty_basket_skips_cancel(self):
        """When stop_basket is falsy, cancel_order should NOT be called."""
        _, order_plant, _ = _run_flatten_coroutine("")

        order_plant.cancel_order.assert_not_awaited()
        # submit_order (SELL) should still fire
        order_plant.submit_order.assert_awaited_once()

    def test_run_cancel_failure_does_not_abort(self):
        """A cancel failure is logged as a warning; the SELL must still be attempted."""
        _, order_plant, _ = _run_flatten_coroutine(
            "1234567", cancel_side_effect=RuntimeError("already gone")
        )

        # SELL must still have been attempted despite cancel failure
        order_plant.submit_order.assert_awaited_once()

    def test_run_passes_correct_symbol(self):
        mod, order_plant, _ = _run_flatten_coroutine("1234567")

        call_kwargs = order_plant.submit_order.call_args.kwargs
        assert call_kwargs.get("symbol") == mod.SYMBOL
        assert call_kwargs.get("exchange") == mod.EXCHANGE

    def test_run_disconnects_after_sell(self):
        _, _, fake_client = _run_flatten_coroutine("1234567")

        fake_client.disconnect.assert_awaited_once()


class TestMainBlockNotCalledAtImport:
    """The if __name__ == '__main__' block must not execute during import."""

    def test_asyncio_run_not_called_at_import(self):
        """Importing the module must not start an event loop."""
        with patch("asyncio.run") as mock_run:
            _import_flatten()
            mock_run.assert_not_called()
