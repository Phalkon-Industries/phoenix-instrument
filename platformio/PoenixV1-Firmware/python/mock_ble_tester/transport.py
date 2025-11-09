"""BLE transport abstractions for the Phoenix mock tester."""

from __future__ import annotations

import asyncio
from typing import Optional, Protocol

COMMAND_CHARACTERISTIC_UUID = "c1883ec3-d984-4dcd-9d67-41ade54c5f2a"
NOTIFICATION_CHARACTERISTIC_UUID = "cd1411cc-4ab8-46e8-9ad5-c41a871caf41"


class BleTransportError(RuntimeError):
    """Raised when BLE transport operations fail."""


class BleTransport(Protocol):
    """Protocol describing the BLE transport contract used by the runner."""

    def connect(self, device_name: str, timeout: float) -> None:
        ...

    def disconnect(self) -> None:
        ...

    def exchange(self, payload: str, response_timeout: float) -> str:
        ...


class BleakBleTransport:
    """Concrete BLE transport backed by the bleak library."""

    def __init__(self) -> None:
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        self._client = None
        self._notification_event: Optional[asyncio.Event] = None
        self._last_notification: Optional[str] = None

    def connect(self, device_name: str, timeout: float) -> None:
        try:
            self._run(self._connect(device_name, timeout))
        except BleTransportError:
            raise
        except Exception as error:  # pragma: no cover - exercised on hardware only
            raise BleTransportError(str(error)) from error

    def disconnect(self) -> None:
        try:
            self._run(self._disconnect())
        finally:
            if not self._loop.is_closed():
                self._loop.close()

    def exchange(self, payload: str, response_timeout: float) -> str:
        try:
            return self._run(self._exchange(payload, response_timeout))
        except BleTransportError:
            raise
        except Exception as error:  # pragma: no cover - exercised on hardware only
            raise BleTransportError(str(error)) from error

    def _run(self, coroutine):
        return self._loop.run_until_complete(coroutine)

    async def _connect(self, device_name: str, timeout: float) -> None:
        from bleak import BleakClient, BleakScanner  # Lazy import to keep tests lightweight

        device = await BleakScanner.find_device_by_name(device_name, timeout=timeout)
        if device is None:
            raise BleTransportError(f"Device '{device_name}' not found within {timeout} seconds")

        client = BleakClient(device)
        await client.connect(timeout=timeout)
        await client.start_notify(NOTIFICATION_CHARACTERISTIC_UUID, self._handle_notification)

        self._client = client

    async def _disconnect(self) -> None:
        if self._client is None:
            return

        try:
            await self._client.stop_notify(NOTIFICATION_CHARACTERISTIC_UUID)
        finally:
            await self._client.disconnect()
            self._client = None

    async def _exchange(self, payload: str, response_timeout: float) -> str:
        if self._client is None:
            raise BleTransportError("BLE client is not connected")

        if self._notification_event is None or self._notification_event.is_set():
            self._notification_event = asyncio.Event()

        await self._client.write_gatt_char(COMMAND_CHARACTERISTIC_UUID, payload.encode("utf-8"), response=False)

        try:
            await asyncio.wait_for(self._notification_event.wait(), timeout=response_timeout)
        except asyncio.TimeoutError as error:
            raise BleTransportError("Timed out waiting for BLE notification") from error

        if self._last_notification is None:
            raise BleTransportError("Notification received without payload")

        return self._last_notification

    def _handle_notification(self, _sender: int, data: bytearray) -> None:
        self._last_notification = data.decode("utf-8")
        if self._notification_event is not None:
            self._notification_event.set()


def create_ble_transport() -> BleTransport:
    """Factory for the default bleak-backed transport."""

    return BleakBleTransport()
