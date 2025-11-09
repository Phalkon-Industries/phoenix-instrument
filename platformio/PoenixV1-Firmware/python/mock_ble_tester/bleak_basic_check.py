"""Minimal Bleak smoke-test script.

Run with ``conda run -n phoenix-python python python/bleak_basic_check.py``.
The script scans for the Phoenix mock device and prints a status line.
"""

from __future__ import annotations

import asyncio
import importlib
import importlib.util
import os
import sys


def _ensure_dependency(name: str, install_hint: str) -> None:
    if importlib.util.find_spec(name) is None:
        raise RuntimeError(
            f"Required module '{name}' not found. Install it inside the phoenix-python environment: {install_hint}"
        )


_ensure_dependency("bleak", "pip install bleak")

if sys.platform == "win32":
    _ensure_dependency("winrt", "pip install 'bleak[dotnet]'")

from bleak import BleakScanner  # noqa: E402  (import after dependency check)

_DEVICE_NAME = os.environ.get("PHOENIX_MOCK_BLE_DEVICE", "Phoenix Mock")
_TIMEOUT = float(os.environ.get("PHOENIX_MOCK_BLE_CONNECT_TIMEOUT", "15"))


async def main() -> None:
    device = await BleakScanner.find_device_by_name(_DEVICE_NAME, timeout=_TIMEOUT)
    if device is None:
        print(f"Device '{_DEVICE_NAME}' not found within {_TIMEOUT} seconds")
    else:
        print(f"Found device '{_DEVICE_NAME}' -> address={device.address}")


if __name__ == "__main__":
    if sys.platform == "win32":
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

    asyncio.run(main())
