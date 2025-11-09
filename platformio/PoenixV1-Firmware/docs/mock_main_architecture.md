# Mock Main Application Architecture

This document introduces the mock main firmware that targets the `mock_main` PlatformIO environment. It mirrors the interaction contract captured in `docs/phone_instrument_interactions.md` while generating spoofed data so the phone application can develop against hardware without lab accessories.

## High-Level Overview

- **Goal**: Provide a drop-in firmware image that presents the same BLE-facing behaviour as the production main app while sourcing measurements, settings, battery state, and alerts exclusively from spoofed data providers.
- **Approach**: Deliver the phone-facing contract (commands, responses, notifications) without engaging any physical drivers. Instead of talking to ADCs, LEDs, or power rails, the mock pauses for approximately two seconds, then returns payloads assembled by in-memory generators. Each response introduces small, controlled variability (“fuzziness”) so the phone app experiences realistic measurement drift while still remaining testable.
- **Entrypoints**: The Arduino `setup()` and `loop()` functions in `src/examples/mock_main.cpp` initialise the shared Phoenix BLE stack, register the bridge exposed by `lib/mocks/mock_main_ble_bridge.*`, and hand off to the controller. The Unity suite interacts with the controller directly, so new contributors can explore behaviour without flashing hardware.
  - See `docs/mock_main_ble_quickstart.md` for the BLE identifiers, command matrix, and QA-oriented serial logging notes.

## Current Components

| Component | Location | Responsibilities |
| --- | --- | --- |
| `MockAppController` | `lib/mocks/mock_main_controller.hpp/.cpp` | Manages mock state (reference/sample counters, settings, battery cache, alert payloads) and exposes C-style APIs consumed by Unity tests and the BLE bridge. Serial logs prefixed with `[mock_main]` document every action. |
| `MockBleBridge` | `lib/mocks/mock_main_ble_bridge.hpp/.cpp` | Parses command JSON, invokes the controller, formats Phoenix BLE notifications, and mirrors production pacing (response delays, command envelopes). |
| Unity Test Suite | `test/mock_tests/test_mock_main/test_mock_main.cpp` | Exercises both the happy path and guard rails (missing reference, NULL arguments) and now validates the BLE bridge via a stub backend. Provides the executable specification for contributors adjusting behaviour. |

## Planned Extensions

With the BLE bridge now in place, future refactors can focus on breaking the controller into narrower service-specific helpers if the logic grows significantly:

- `MockMeasurementService`, `MockSettingsService`, `MockBatteryService`, `MockAlertService`: optional refactors if the controller grows large. Today, their responsibilities live inline in `mock_main_controller.cpp` and the bridge fans commands across those entry points.

Default jitter ranges already captured in the controller mirror the production tolerances: ±1000 codes for raw counts, ±1 °C for temperature, and battery percentages clamped between 0–100 %. Adjust the seed via `mock_app_controller_set_seed()` to re-enable variability.

## Command & Telemetry Flow

1. **BLE Availability** – The BLE quick start documents the advertising profile (`Phoenix Mock`, UUIDs). The bridge in `lib/mocks/mock_main_ble_bridge.cpp` already surfaces the controller functions over the command characteristic.
2. **Connection Handshake** – Once connected, the bridge calls `mock_app_controller_initialize()` and shares the canned capability summary with the phone app.
3. **Reference Measurement** – `mock_app_controller_run_reference()` waits roughly two seconds, generates deterministic counts, and caches the baseline for downstream samples.
4. **Sample Measurement** – Requires a prior reference. `mock_app_controller_run_sample()` validates readiness, applies optional jitter, and returns absorbance and pH.
5. **Settings Update / Refresh** – `mock_app_controller_update_settings()` mutates the in-memory snapshot; `mock_app_controller_get_settings()` echoes either defaults or the latest overrides.
6. **Battery Status** – `mock_app_controller_read_battery()` synthesises percentage/voltage pairs and marks the reading low when the value drops below 15 %.
7. **Alerts** – `mock_app_controller_raise_alert()` returns the canned “sensor fault” payload; the BLE layer will expose this through the `alert_inject` command described in the quick start.
8. **Session Teardown** – `mock_app_controller_teardown()` reports counts and toggles `ble_connected` false so the bridge can drop the connection cleanly.

## Test Strategy

- **Unity Tests**: The suite under `test/mock_tests/test_mock_main/` drives every controller feature directly. Negative-path checks cover missing references and guard error propagation so newcomers see how failures should surface.
- **BLE Regression (Pytest)**: The hardware-in-the-loop workflow at `python/tests/mock_ble/test_device_workflows.py` now mirrors every BLE command as its own pytest case. Run it with `conda run -n phoenix-python pytest python/tests/mock_ble/test_device_workflows.py --mock-ble -vv` to validate the contract after flashing new firmware.
- **Legacy Test Isolation**: The `mock_main` PlatformIO environment filters out hardware-centric Unity suites, keeping this firmware free from device dependencies.
- **Data Control with Fuzziness**: The seed-based jitter helpers ensure reproducible tests (seed `0`) while letting QA opt into variability when needed.

## Scope Decisions

- **Phoenix Benchmark**: Out of scope for the mock. We only mirror the phone app interactions captured in `docs/phone_instrument_interactions.md`.
- **Alert Injection**: Alerts are triggered via the BLE command surface (`alert_inject`), which maps to `mock_app_controller_raise_alert()`.
- **Timing Guarantees**: Responses target a ~2 s delay to feel realistic, but they only need to be “close enough” for user experience testing; no tight timing guarantees are enforced.
