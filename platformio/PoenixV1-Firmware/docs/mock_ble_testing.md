# Phoenix Mock BLE Testing

The Phoenix mock BLE workflows now execute through pytest instead of a standalone
CLI. This page documents how to run the regression suite, which lives under
`python/tests/mock_ble/test_device_workflows.py`, and how to interpret the
output.

## Prerequisites
- Activate the shared Python environment: `conda run -n phoenix-python …`
- Ensure the mock instrument is advertising with the expected BLE name (default:
  `Phoenix Mock`).
- Connect the BLE adapter that hosts the `bleak` transport.

## Running the Workflows
Use pytest with the dedicated flag that enables the BLE fixtures:

```powershell
conda run -n phoenix-python pytest --mock-ble
```

Pytest discovers any tests marked with `@pytest.mark.mock_ble`. The session-scoped
fixture `ble_exchange` (defined in `python/tests/mock_ble/test_device_workflows.py`)
connects to the device, exercises the command surface directly, and validates
each response payload. If a step fails, pytest raises an assertion immediately
with the offending payload so CI can surface the mismatch.

### Command-Line Options
- `--mock-ble-device` – Advertised BLE name (default `Phoenix Mock`).
- `--mock-ble-connect-timeout` – Seconds to wait while discovering and connecting
  to the device (default `15.0`).
- `--mock-ble-response-timeout` – Seconds to wait for each notification response
  (default `5.0`).

Example:

```powershell
conda run -n phoenix-python pytest --mock-ble \
  --mock-ble-device "Phoenix QA" \
  --mock-ble-connect-timeout 30 \
  --mock-ble-response-timeout 10
```

### Output
The pytest terminal summary reports the usual passed/failed counts. Each failure
prints the decoded response so firmware and mobile engineers can diagnose the
mismatch quickly. Pytest exits with code `1` if any command does not respond as
expected, allowing CI to gate on the result.

### Stubbing for Development
Set the environment variable `PHOENIX_MOCK_BLE_RUNNER_FACTORY` to
`<module>:<callable>` to override the runner factory used by the plugin. The
callable must return an object that exposes `run_all()` and produces a
`BleTestSummary`. This is useful when writing unit tests or experimenting with
a simulated transport.

```powershell
set PHOENIX_MOCK_BLE_RUNNER_FACTORY=python.tests.test_mock_ble_tester:build_stub_runner
conda run -n phoenix-python pytest --mock-ble
```

Remember to clear the variable before running against real hardware so the
actual BLE transport is exercised.
