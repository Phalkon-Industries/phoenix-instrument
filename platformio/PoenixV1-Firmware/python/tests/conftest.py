"""Shared pytest configuration for Phoenix Python tooling tests."""

import sys


def _ensure_python_path() -> None:
    # Step 1: Add the repo's `python` directory to sys.path so in-repo packages resolve during tests.
    tests_dir = __file__.replace("\\", "/")
    python_dir = tests_dir.rsplit("/", 2)[0]
    if python_dir not in sys.path:
        sys.path.insert(0, python_dir)


_ensure_python_path()


pytest_plugins = ["mock_ble_tester.pytest_plugin"]
