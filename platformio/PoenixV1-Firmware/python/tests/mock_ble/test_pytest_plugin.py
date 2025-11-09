import os


pytest_plugins = ["pytester"]


def _runpytest_with_cleanup(pytester, *args):
    try:
        return pytester.runpytest(*args)
    finally:
        os.environ.pop("PHOENIX_MOCK_BLE_RUNNER_FACTORY", None)


def test_mock_ble_plugin_executes_workflows(pytester):
    pytester.makeconftest(
        """
        import os

        pytest_plugins = ['mock_ble_tester.pytest_plugin']


        def pytest_configure(config):
            os.environ['PHOENIX_MOCK_BLE_RUNNER_FACTORY'] = 'dummy_runner:build_runner'
        """
    )

    pytester.makepyfile(
        """
        import pytest

        @pytest.mark.mock_ble
        def test_mock_ble_summary(mock_ble_summary):
            assert mock_ble_summary.failed == 0
        """
    )

    pytester.makepyfile(
        dummy_runner="""
from mock_ble_tester.runner import BleTestSummary


class _DummyRunner:
    def __init__(self, **_kwargs):
        pass

    def run_all(self):
        return BleTestSummary(passed=5, failed=0, warnings=0, duration_seconds=0.5)


def build_runner(**kwargs):
    return _DummyRunner(**kwargs)
"""
    )

    result = _runpytest_with_cleanup(pytester, "--mock-ble")

    result.assert_outcomes(passed=1)
    assert "5 passed, 0 failed, 0 warnings" in result.stdout.str()


def test_mock_ble_plugin_fails_on_warning(pytester):
    pytester.makeconftest(
        """
        import os

        pytest_plugins = ['mock_ble_tester.pytest_plugin']


        def pytest_configure(config):
            os.environ['PHOENIX_MOCK_BLE_RUNNER_FACTORY'] = 'dummy_runner:build_runner'
        """
    )

    pytester.makepyfile(
        """
        import pytest

        @pytest.mark.mock_ble
        def test_mock_ble_summary(mock_ble_summary):
            pass
        """
    )

    pytester.makepyfile(
        dummy_runner="""
from mock_ble_tester.runner import BleTestSummary


class _WarningRunner:
    def __init__(self, **_kwargs):
        pass

    def run_all(self):
        return BleTestSummary(passed=4, failed=0, warnings=1, duration_seconds=0.4)


def build_runner(**kwargs):
    return _WarningRunner(**kwargs)
"""
    )

    result = _runpytest_with_cleanup(pytester, "--mock-ble")

    assert result.ret == 1
    assert "4 passed, 0 failed, 1 warnings" in result.stdout.str()


def test_mock_ble_plugin_skips_without_flag(pytester):
    pytester.makeconftest("pytest_plugins = ['mock_ble_tester.pytest_plugin']")

    pytester.makepyfile(
        """
        import pytest

        @pytest.mark.mock_ble
        def test_requires_flag(mock_ble_summary):
            pass
        """
    )

    result = _runpytest_with_cleanup(pytester, "-rs")
    result.assert_outcomes(skipped=1)
    result.stdout.fnmatch_lines(["*Pass --mock-ble to enable*"])


def test_mock_ble_plugin_propagates_failures(pytester):
    pytester.makeconftest(
        """
        import os

        pytest_plugins = ['mock_ble_tester.pytest_plugin']


        def pytest_configure(config):
            os.environ['PHOENIX_MOCK_BLE_RUNNER_FACTORY'] = 'dummy_runner:build_runner'
        """
    )

    pytester.makepyfile(
        """
        import pytest

        @pytest.mark.mock_ble
        def test_fails_when_summary_fails(mock_ble_summary):
            pass
        """
    )

    pytester.makepyfile(
        dummy_runner="""
from mock_ble_tester.runner import BleTestSummary


class _FailingRunner:
    def __init__(self, **_kwargs):
        pass

    def run_all(self):
        return BleTestSummary(passed=3, failed=2, warnings=0, duration_seconds=0.25)


def build_runner(**kwargs):
    return _FailingRunner(**kwargs)
"""
    )

    result = _runpytest_with_cleanup(pytester, "--mock-ble")

    assert result.ret == 1
    assert "3 passed, 2 failed, 0 warnings" in result.stdout.str()
