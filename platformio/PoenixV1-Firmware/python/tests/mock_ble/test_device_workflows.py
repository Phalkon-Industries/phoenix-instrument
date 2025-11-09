"""End-to-end BLE workflow tests for the Phoenix mock instrument."""

import json
from dataclasses import dataclass
from typing import Any, Callable, Dict, Optional, Sequence, Tuple

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

_REFERENCE_EXPECTED: JsonDict = {
    "status": "ok",
    "dark_counts": 195000,
    "signal_counts": 210000,
    "absorbance": 0.15,
    "temperature_c": 25.0,
    "salinity_ppt": 35.0,
}

_SAMPLE_EXPECTED: JsonDict = {
    "status": "ok",
    "dark_counts": 196250,
    "signal_counts": 224500,
    "absorbance": 0.2825,
    "temperature_c": 25.5,
    "salinity_ppt": 35.2,
    "ph_value": 7.42,
}

_BATTERY_EXPECTED: JsonDict = {
    "status": "ok",
    "percentage": 78,
    "voltage_v": 3.92,
    "is_low": False,
}

_ALERT_EXPECTED: JsonDict = {
    "status": "ok",
    "alert_code": "0004",
    "severity": 1,
    "message": "Mock sensor fault detected.",
}


@dataclass(frozen=True)
class CommandCall:
    command: str
    expected_subset: JsonDict
    request_override: Optional[JsonDict] = None
    response_timeout: Optional[float] = None


@dataclass(frozen=True)
class CommandScenario:
    name: str
    command: str
    expected_subset: JsonDict
    request_override: Optional[JsonDict] = None
    response_timeout: Optional[float] = None
    preparation: Sequence[CommandCall] = ()


_COMMAND_SCENARIOS: Tuple[CommandScenario, ...] = (
    CommandScenario(
        name="settings_update_defaults",
        command="settings_update",
        request_override=_DEFAULT_SETTINGS_REQUEST,
        expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
    ),
    CommandScenario(
        name="settings_get_defaults",
        command="settings_get",
        expected_subset={"status": "ok", "snapshot": _DEFAULT_SETTINGS_RESPONSE},
        preparation=(
            CommandCall(
                command="settings_update",
                request_override=_DEFAULT_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
            ),
        ),
    ),
    CommandScenario(
        name="reference_start",
        command="reference_start",
        expected_subset=_REFERENCE_EXPECTED,
        preparation=(
            CommandCall(
                command="settings_update",
                request_override=_DEFAULT_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
            ),
        ),
    ),
    CommandScenario(
        name="sample_start_after_reference",
        command="sample_start",
        expected_subset=_SAMPLE_EXPECTED,
        preparation=(
            CommandCall(
                command="settings_update",
                request_override=_DEFAULT_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
            ),
            CommandCall(
                command="reference_start",
                expected_subset=_REFERENCE_EXPECTED,
            ),
        ),
    ),
    CommandScenario(
        name="settings_update_alternate",
        command="settings_update",
        request_override=_UPDATED_SETTINGS_REQUEST,
        expected_subset={"status": "ok", "applied": _UPDATED_SETTINGS_RESPONSE},
        preparation=(
            CommandCall(
                command="settings_update",
                request_override=_DEFAULT_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
            ),
        ),
    ),
    CommandScenario(
        name="settings_get_alternate",
        command="settings_get",
        expected_subset={"status": "ok", "snapshot": _UPDATED_SETTINGS_RESPONSE},
        preparation=(
            CommandCall(
                command="settings_update",
                request_override=_DEFAULT_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
            ),
            CommandCall(
                command="settings_update",
                request_override=_UPDATED_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _UPDATED_SETTINGS_RESPONSE},
            ),
        ),
    ),
    CommandScenario(
        name="restore_default_settings",
        command="settings_update",
        request_override=_DEFAULT_SETTINGS_REQUEST,
        expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
        preparation=(
            CommandCall(
                command="settings_update",
                request_override=_UPDATED_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _UPDATED_SETTINGS_RESPONSE},
            ),
        ),
    ),
    CommandScenario(
        name="battery_status",
        command="battery_status",
        expected_subset=_BATTERY_EXPECTED,
        preparation=(
            CommandCall(
                command="settings_update",
                request_override=_DEFAULT_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
            ),
        ),
    ),
    CommandScenario(
        name="alert_inject",
        command="alert_inject",
        expected_subset=_ALERT_EXPECTED,
        preparation=(
            CommandCall(
                command="settings_update",
                request_override=_DEFAULT_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
            ),
        ),
    ),
    CommandScenario(
        name="session_teardown",
        command="session_teardown",
        expected_subset={"status": "ok"},
        response_timeout=10.0,
        preparation=(
            CommandCall(
                command="settings_update",
                request_override=_DEFAULT_SETTINGS_REQUEST,
                expected_subset={"status": "ok", "applied": _DEFAULT_SETTINGS_RESPONSE},
            ),
            CommandCall(
                command="reference_start",
                expected_subset=_REFERENCE_EXPECTED,
            ),
            CommandCall(
                command="sample_start",
                expected_subset=_SAMPLE_EXPECTED,
            ),
        ),
    ),
)

_SCENARIO_IDS = [scenario.name for scenario in _COMMAND_SCENARIOS]


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
@pytest.mark.parametrize("scenario", _COMMAND_SCENARIOS, ids=_SCENARIO_IDS)
def test_mock_ble_command_scenarios(ble_exchange, scenario: CommandScenario) -> None:
    # Step 1: Execute any preparation commands so the controller enters the required state.
    for preparation in scenario.preparation:
        ble_exchange(
            command=preparation.command,
            expected_subset=preparation.expected_subset,
            request_override=preparation.request_override,
            response_timeout=preparation.response_timeout,
        )

    # Step 2: Run the target command and verify its deterministic payload.
    response = ble_exchange(
        command=scenario.command,
        expected_subset=scenario.expected_subset,
        request_override=scenario.request_override,
        response_timeout=scenario.response_timeout,
    )

    # Step 3: Confirm the status line advertises success so the pytest report is concise.
    parameters = response.get("parameters", {})
    if "status" in parameters:
        assert parameters["status"] == scenario.expected_subset.get("status", parameters["status"])
