# Phoenix Mock BLE Testing

The Phoenix mock BLE workflows execute through pytest rather than a bespoke
CLI. This guide walks a new teammate from zero context to running the
per-command regression that covers every BLE interaction documented in
`docs/phone_instrument_interactions.md`.

## Prerequisites
- Activate the shared Python environment: `conda run -n phoenix-python …`
- Ensure the mock instrument is advertising with the expected BLE name (default:
  `Phoenix Mock`).
- Connect the BLE adapter that hosts the `bleak` transport.

## Quick Start (New Teammate Primer)
- **Step 1**: Flash the mock firmware with `pio run -e mock_main -t upload`
  (see `docs/mock_main_ble_quickstart.md` for a full walkthrough).
- **Step 2**: Confirm your workstation can see the board by running
  `conda run -n phoenix-python python python/bleak_basic_check.py`.
- **Step 3**: Run the BLE regression suite directly against hardware:

  ```powershell
  conda run -n phoenix-python pytest python/tests/mock_ble/test_device_workflows.py --mock-ble -vv
  ```

  The `-vv` flag keeps pytest verbose so each BLE command reports as an
  individual test (e.g. `settings_get_defaults`, `sample_start_after_reference`).

## Running the Workflows
The canonical command above expands to the general pattern:

```powershell
conda run -n phoenix-python pytest <paths> --mock-ble [pytest-options]
```

Pytest discovers every test tagged with `@pytest.mark.mock_ble`. The
session-scoped fixture `ble_exchange` (defined in
`python/tests/mock_ble/test_device_workflows.py`) connects to the device,
exercises the command surface directly, and validates each response payload.

### Why per-command tests?
Each command scenario executes independently with any prerequisite calls staged
automatically. When a contributor tweaks, say, `alert_inject`, only the
`test_mock_ble_command_scenarios[alert_inject]` case fails, making regressions
obvious in CI results and local runs alike.

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
- Pytest enumerates each BLE command as its own test case when you use `-vv`.
- Failures print the decoded response so firmware and mobile engineers can
  diagnose the mismatch quickly.
- Pytest exits with code `1` if any command does not respond as expected,
  allowing CI and pre-commit hooks to gate on the result.

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
