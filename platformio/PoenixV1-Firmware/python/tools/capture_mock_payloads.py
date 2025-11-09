"""Utility to print current Phoenix mock BLE payloads for documentation."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Dict, Iterable, List, Optional, Tuple

from mock_ble_tester.transport import BleTransportError, create_ble_transport


@dataclass(frozen=True)
class CommandRequest:
    name: str
    extra_parameters: Optional[Dict[str, Any]] = None
    response_timeout: float = 8.0

    def build_payload(self) -> str:
        payload: Dict[str, Any] = {"command": self.name}
        if self.extra_parameters:
            payload.update(self.extra_parameters)
        return json.dumps(payload)


def _collect_payloads(commands: Iterable[CommandRequest]) -> List[Tuple[str, Dict[str, Any]]]:
    transport = create_ble_transport()
    transport.connect("Phoenix Mock", 15.0)

    results: List[Tuple[str, Dict[str, Any]]] = []
    try:
        for command in commands:
            try:
                response_text = transport.exchange(command.build_payload(), command.response_timeout)
            except BleTransportError as error:
                results.append((command.name, {"_transport_error": str(error)}))
                break

            envelope = json.loads(response_text)
            parameters = envelope.get("parameters", {})
            results.append((command.name, parameters))
    finally:
        transport.disconnect()

    return results


def main() -> None:
    commands = [
    CommandRequest("sample_start", response_timeout=10.0),
    CommandRequest("reference_start", response_timeout=10.0),
    CommandRequest("sample_start", response_timeout=10.0),
    CommandRequest("settings_get"),
        CommandRequest(
            "settings_update",
            {
                "temperature_c": 24.5,
                "salinity_ppt": 34.7,
                "interval_ms": 90000,
                "alerts_enabled": False,
            },
        ),
    CommandRequest("settings_get"),
    CommandRequest("battery_status"),
    CommandRequest("alert_inject"),
    CommandRequest("session_teardown", response_timeout=10.0),
    ]

    results = _collect_payloads(commands)
    for name, parameters in results:
        print(f"\n== {name} ==")
        print(json.dumps(parameters, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
