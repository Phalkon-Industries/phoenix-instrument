"""End-to-end BLE workflow tests for the Phoenix mock instrument."""

import json
from typing import Any, Callable, Dict, Optional

import pytest

from mock_ble_tester.transport import BleTransportError, create_ble_transport


JsonDict = Dict[str, Any]

_DEFAULT_SETTINGS_REQUEST: JsonDict = {
    "temperature_c": 25.0,
    "salinity_ppt": 35.0,
    "interval_ms": 120000,
    "alerts_enabled": True,
}

_DEFAULT_SETTINGS_RESPONSE: JsonDict = dict(_DEFAULT_SETTINGS_REQUEST)

_UPDATED_SETTINGS_REQUEST: JsonDict = {
    "temperature_c": 24.5,
    "salinity_ppt": 34.7,
    "interval_ms": 90000,
    "alerts_enabled": False,
}

_UPDATED_SETTINGS_RESPONSE: JsonDict = dict(_UPDATED_SETTINGS_REQUEST)


def _assert_response_subset(expected: JsonDict, actual: JsonDict, *, path: str = "parameters") -> None:
    for key, expected_value in expected.items():
        if key not in actual:
            pytest.fail(f"Expected key '{path}.{key}' missing from response: {actual}")
        actual_value = actual[key]
        if isinstance(expected_value, dict):
            if not isinstance(actual_value, dict):
                pytest.fail(f"Expected '{path}.{key}' to be a dict, received {type(actual_value)!r}")
            _assert_response_subset(expected_value, actual_value, path=f"{path}.{key}")
        else:
            if actual_value != expected_value:
                pytest.fail(
                    f"Mismatch at '{path}.{key}': expected {expected_value!r}, received {actual_value!r}"
                )


@pytest.fixture(scope="session")
def ble_device_config(pytestconfig: pytest.Config) -> JsonDict:
    return {
        "device_name": pytestconfig.getoption("--mock-ble-device"),
        "connect_timeout": pytestconfig.getoption("--mock-ble-connect-timeout"),
        "response_timeout": pytestconfig.getoption("--mock-ble-response-timeout"),
    }


@pytest.fixture(scope="session")
def ble_transport(ble_device_config: JsonDict):
    transport = create_ble_transport()
    try:
        transport.connect(ble_device_config["device_name"], ble_device_config["connect_timeout"])
    except BleTransportError as error:
        pytest.fail(f"Failed to connect to BLE device: {error}")

    try:
        yield transport
    finally:
        transport.disconnect()


@pytest.fixture(scope="session", name="ble_exchange")
def _ble_exchange(ble_transport, ble_device_config: JsonDict) -> Callable[..., JsonDict]:
    default_timeout = ble_device_config["response_timeout"]

    def _send(
        *,
        command: str,
        expected_subset: Optional[JsonDict] = None,
        request_override: Optional[JsonDict] = None,
        response_timeout: Optional[float] = None,
    ) -> JsonDict:
        request_body: JsonDict = {"command": command}
        if request_override:
            request_body.update(request_override)

        try:
            response_text = ble_transport.exchange(json.dumps(request_body), response_timeout or default_timeout)
        except BleTransportError as error:
            pytest.fail(f"BLE exchange failed for command '{command}': {error}")

        try:
            decoded = json.loads(response_text)
        except json.JSONDecodeError as error:
            pytest.fail(f"Invalid JSON response for '{command}': {error}: {response_text}")

        if decoded.get("command") != command:
            pytest.fail(
                f"Command echo mismatch: expected '{command}', received '{decoded.get('command')}'"
            )

        parameters = decoded.get("parameters")
        if not isinstance(parameters, dict):
            pytest.fail(f"Response for '{command}' missing 'parameters' object: {decoded}")

        if expected_subset:
            _assert_response_subset(expected_subset, parameters)

        return decoded

    return _send


@pytest.mark.mock_ble
def test_mock_ble_full_workflow(ble_exchange):
    # Step 1: Normalize settings to the known default profile.
    default_update = ble_exchange(
        command="settings_update",
        request_override=_DEFAULT_SETTINGS_REQUEST,
        expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
    )
    assert default_update["parameters"]["status"] == "ok"

    # Step 2: Confirm the device reports the default settings snapshot.
    snapshot_default = ble_exchange(
        command="settings_get",
        expected_subset={"status": "ok", "snapshot": _DEFAULT_SETTINGS_RESPONSE},
    )
    assert snapshot_default["parameters"]["status"] == "ok"

    # Step 3: Capture a reference bundle for repeatable sampling.
    reference = ble_exchange(
        command="reference_start",
        expected_subset={
            "status": "ok",
            "dark_counts": 195000,
            "signal_counts": 210000,
            "absorbance": 0.15,
            "temperature_c": 25.0,
            "salinity_ppt": 35.0,
        },
    )
    assert reference["parameters"]["status"] == "ok"

    # Step 4: Take a sample measurement and validate deterministic payload fields.
    sample = ble_exchange(
        command="sample_start",
        expected_subset={
            "status": "ok",
            "dark_counts": 196250,
            "signal_counts": 224500,
            "absorbance": 0.2825,
            "temperature_c": 25.5,
            "salinity_ppt": 35.2,
            "ph_value": 7.42,
        },
    )
    assert sample["parameters"]["status"] == "ok"

    # Step 5: Apply an alternate settings profile and confirm it echoes back.
    applied = ble_exchange(
        command="settings_update",
        request_override=_UPDATED_SETTINGS_REQUEST,
        expected_subset={"status": "ok", "applied": _UPDATED_SETTINGS_RESPONSE},
    )
    assert applied["parameters"]["status"] == "ok"

    # Step 6: Verify the alternate profile is now active.
    snapshot_updated = ble_exchange(
        command="settings_get",
        expected_subset={"status": "ok", "snapshot": _UPDATED_SETTINGS_RESPONSE},
    )
    assert snapshot_updated["parameters"]["status"] == "ok"

    # Step 7: Inspect the battery telemetry payload for level metadata.
    battery = ble_exchange(
        command="battery_status",
        expected_subset={
            "status": "ok",
            "percentage": 78,
            "voltage_v": 3.92,
            "is_low": False,
        },
    )
    assert battery["parameters"]["status"] == "ok"

    # Step 8: Trigger a mock alert and confirm the diagnostics block.
    alert = ble_exchange(
        command="alert_inject",
        expected_subset={
            "status": "ok",
            "alert_code": "0004",
            "severity": 1,
            "message": "Mock sensor fault detected.",
        },
    )
    assert alert["parameters"]["status"] == "ok"

    # Step 9: Restore defaults so subsequent sessions start from a clean baseline.
    restore_defaults = ble_exchange(
        command="settings_update",
        request_override=_DEFAULT_SETTINGS_REQUEST,
        expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
    )
    assert restore_defaults["parameters"]["status"] == "ok"

    # Step 10: Request a teardown summary and tolerate firmware latency during shutdown.
    teardown = ble_exchange(
        command="session_teardown",
        expected_subset={"status": "ok"},
        response_timeout=10.0,
    )
    assert teardown["parameters"]["status"] == "ok"
