"""Basic BLE connectivity smoke test for the Phoenix mock instrument."""

import asyncio
import importlib
import importlib.util
import os
import sys

try:
    _bleak = importlib.import_module("bleak")
except ModuleNotFoundError as error:  # pragma: no cover - environment guard
    raise RuntimeError(
        "The 'bleak' dependency is required for BLE connectivity tests. "
        "Install it in the phoenix-python environment before running this test."
    ) from error

BleakClient = getattr(_bleak, "BleakClient")
BleakScanner = getattr(_bleak, "BleakScanner")

from mock_ble_tester.transport import (
    COMMAND_CHARACTERISTIC_UUID,
    NOTIFICATION_CHARACTERISTIC_UUID,
)

if sys.platform == "win32" and importlib.util.find_spec("winrt") is None:
    raise RuntimeError(
        "Bleak requires the 'winrt' package on Windows. Install it with "
        "`pip install bleak[dotnet]` inside the phoenix-python environment."
    )

_DEVICE_NAME = os.environ.get("PHOENIX_MOCK_BLE_DEVICE", "Phoenix Mock")
_CONNECT_TIMEOUT = float(os.environ.get("PHOENIX_MOCK_BLE_CONNECT_TIMEOUT", "15"))


async def _verify_characteristics() -> None:
    device = await BleakScanner.find_device_by_name(_DEVICE_NAME, timeout=_CONNECT_TIMEOUT)
    if device is None:
        raise AssertionError(f"Device '{_DEVICE_NAME}' not found within {_CONNECT_TIMEOUT} seconds")

    async with BleakClient(device) as client:
        services_collection = getattr(client, "services", None)
        if services_collection is None:
            if hasattr(client, "get_services"):
                services_collection = await client.get_services()  # pragma: no cover - fallback for older Bleak versions
            else:  # pragma: no cover - defensive
                raise AssertionError("Bleak client exposes no services collection")
        try:
            next(iter(services_collection))
        except StopIteration as error:  # pragma: no cover - defensive
            if hasattr(client, "get_services"):
                services_collection = await client.get_services()
                try:
                    next(iter(services_collection))
                except StopIteration as nested_error:
                    raise AssertionError("Bleak client services collection is empty") from nested_error
            else:
                raise AssertionError("Bleak client services collection is empty") from error

        command_characteristic = services_collection.get_characteristic(COMMAND_CHARACTERISTIC_UUID)
        if command_characteristic is None:
            raise AssertionError(
                f"Command characteristic {COMMAND_CHARACTERISTIC_UUID} not exposed by '{_DEVICE_NAME}'"
            )
        if "write" not in command_characteristic.properties and "write-without-response" not in command_characteristic.properties:
            raise AssertionError(
                f"Command characteristic {COMMAND_CHARACTERISTIC_UUID} does not support writes: {command_characteristic.properties}"
            )

        notification_characteristic = services_collection.get_characteristic(NOTIFICATION_CHARACTERISTIC_UUID)
        if notification_characteristic is None:
            raise AssertionError(
                f"Notification characteristic {NOTIFICATION_CHARACTERISTIC_UUID} not exposed by '{_DEVICE_NAME}'"
            )
        if "notify" not in notification_characteristic.properties:
            raise AssertionError(
                f"Notification characteristic {NOTIFICATION_CHARACTERISTIC_UUID} does not support notifications: {notification_characteristic.properties}"
            )


def test_ble_device_exposes_expected_characteristics() -> None:
    if sys.platform == "win32":
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

    asyncio.run(_verify_characteristics())
