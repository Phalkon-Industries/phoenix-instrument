"""Pytest plugin that drives Phoenix mock BLE workflows."""

from __future__ import annotations

import importlib
import os
from typing import Callable, Optional

import pytest

from .runner import BleTestSummary, MockBleTestRunner, build_runner

_RUNNER_FACTORY_ENV = "PHOENIX_MOCK_BLE_RUNNER_FACTORY"
_SUMMARY_STATE_KEY = "_phoenix_mock_ble_summary"


def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("phoenix-mock-ble")
    group.addoption(
        "--mock-ble",
        action="store_true",
        help="Enable Phoenix mock BLE workflow execution during pytest runs.",
    )
    group.addoption(
        "--mock-ble-device",
        action="store",
        default="Phoenix Mock",
        help="Advertised BLE name to connect to when running workflows.",
    )
    group.addoption(
        "--mock-ble-connect-timeout",
        action="store",
        type=float,
        default=15.0,
        help="Seconds to wait while discovering and connecting to the device.",
    )
    group.addoption(
        "--mock-ble-response-timeout",
        action="store",
        type=float,
        default=5.0,
        help="Seconds to wait for each BLE notification response.",
    )


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line(
        "markers", "mock_ble: mark tests that exercise Phoenix mock BLE workflows"
    )
    setattr(config, _SUMMARY_STATE_KEY, None)


@pytest.fixture(scope="session")
def mock_ble_runner(request: pytest.FixtureRequest) -> MockBleTestRunner:
    if not request.config.getoption("--mock-ble"):
        pytest.skip("Pass --mock-ble to enable Phoenix mock BLE workflows.")

    factory = _resolve_runner_factory()
    return factory(
        device_name=request.config.getoption("--mock-ble-device"),
        connect_timeout=request.config.getoption("--mock-ble-connect-timeout"),
        response_timeout=request.config.getoption("--mock-ble-response-timeout"),
    )


@pytest.fixture(scope="session")
def mock_ble_summary(request: pytest.FixtureRequest, mock_ble_runner: MockBleTestRunner) -> BleTestSummary:
    summary = mock_ble_runner.run_all()
    setattr(request.config, _SUMMARY_STATE_KEY, summary)

    if summary.failed > 0 or summary.warnings > 0:
        pytest.fail(summary.pytest_style_line(), pytrace=False)

    return summary


def pytest_terminal_summary(
    terminalreporter,
    exitstatus: pytest.ExitCode,
    config: pytest.Config,
) -> None:
    summary: Optional[BleTestSummary] = getattr(config, _SUMMARY_STATE_KEY, None)
    if summary is None:
        return

    terminalreporter.write_sep("=", "Phoenix Mock BLE Summary")
    for detail in summary.details:
        message = f" - {detail.message}" if detail.message else ""
        terminalreporter.write_line(
            f"{detail.workflow}::{detail.step} {detail.outcome.upper()} "
            f"({detail.duration_seconds:.2f}s){message}"
        )
    terminalreporter.write_sep("-", summary.pytest_style_line())


def _resolve_runner_factory() -> Callable[..., MockBleTestRunner]:
    override = os.environ.get(_RUNNER_FACTORY_ENV)
    if not override:
        return build_runner

    module_name, separator, attr_name = override.partition(":")
    if not module_name or not separator or not attr_name:
        raise pytest.UsageError(
            f"Invalid {_RUNNER_FACTORY_ENV} value '{override}'. Expected format module:callable."
        )

    module = importlib.import_module(module_name)
    factory = getattr(module, attr_name, None)
    if factory is None:
        raise pytest.UsageError(
            f"Unable to resolve runner factory '{override}'. Ensure module and callable exist."
        )
    return factory
