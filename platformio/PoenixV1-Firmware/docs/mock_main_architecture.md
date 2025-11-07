# Mock Main Application Architecture

This document outlines the structure and responsibilities for the mock main firmware that targets the `mock_main` PlatformIO environment. It mirrors the interaction contract captured in `docs/phone_instrument_interactions.md` while generating spoofed data so the phone application can develop against hardware without lab accessories.

## High-Level Overview

- **Goal**: Provide a drop-in firmware image that presents the same BLE-facing behaviour as the production main app while sourcing measurements, settings, battery state, and alerts exclusively from spoofed data providers.
- **Approach**: Deliver the phone-facing contract (commands, responses, notifications) without engaging any physical drivers. Instead of talking to ADCs, LEDs, or power rails, the mock pauses for realistic delays, then returns payloads assembled by in-memory generators. Each response introduces small, controlled variability (“fuzziness”) so the phone app experiences realistic measurement drift while still remaining testable.
- **Entrypoints**: Implement the Arduino `setup()` and `loop()` functions in `src/examples/mock_main.cpp`. These functions will:
  - Initialise only the scaffolding needed for BLE communication and mock data caches.
  - Start the BLE stack, register GATT services/characteristics, and advertise the same UUIDs that the phone app expects.
  - Drive a simple state machine that accepts phone commands, waits the configured delay window (e.g., ~2 seconds for measurements), and then replies with spoofed payloads.

## Module Layout

| Module | Purpose | Notes |
| --- | --- | --- |
| `MockAppController` | Owns top-level state (session status, cached reference/sample data, settings) and coordinates feature flows. | Lives under `src/examples/mock_main_controller.hpp/.cpp`. Provides C-style accessors for Unity tests. |
| `MockBleInterface` | Wrap BLE advertising, connection lifecycle, and characteristic handlers used by the phone app. | Implemented under `src/examples/mock_ble_interface.hpp/.cpp`; exposes hooks so the controller can publish measurement results, alerts, and confirms. |
| `MockMeasurementService` | Generates pseudo-randomised reference/sample data, including raw readings (±1000 ADC counts), absorbance, and computed pH with configurable temp/salinity inputs. | Uses helper functions housed in `lib/mock_generators/`. Supports error injection toggles and ensures responses arrive ~2 seconds after request with seeded variability (e.g., ±1 °C jitter on temperature). |
| `MockSettingsService` | Maintains runtime settings and persists session-visible metadata (firmware version strings, current configuration hash). | Purely in-memory; serialises responses to match production payloads. |
| `MockBatteryService` | Produces battery percentage/state updates and low-battery warnings based on a configurable profile. | Injects ±1 % jitter per read (clamped 0–100 %) and supports scripted discharge curves for QA scenarios. |
- **Jitter Ranges**: Default jitter amounts follow these guidelines and can be tuned per test scenario:
  - Battery percentage: ±1 % absolute per read (clamped between 0 % and 100 %).
  - Raw ADC counts: ±1000 codes around the nominal baseline.
  - Derived absorbance/pH: computed from the jittered raw counts, so downstream values reflect the raw variability.
  - Temperature: ±1 °C total swing; salinity/jitter can mirror the temperature scale or remain fixed depending on app needs.
| `MockAlertService` | Emits alerts on demand (e.g., saturation, sensor fault) and throttles repeats. | Works with the BLE interface to push notifications or respond to phone-triggered injections. |

## Command & Telemetry Flow

1. **BLE Availability** – `MockBleInterface` configures advertising packets in `setup()` with the same identifiers as production. Phone app discovery reuses existing service UUIDs.
2. **Connection Handshake** – On connect, `MockAppController` sends a capability summary (supported commands, firmware version, settings hash) via the BLE characteristic, matching the production schema.
3. **Reference Measurement** – The controller delegates to `MockMeasurementService::run_reference()`, waits the configured delay (~2 seconds), caches output, and returns a payload built with the real formatter helpers so field names and units match.
4. **Sample Measurement** – Requires a cached reference. The service synthesises a sample bundle after another delay, referencing configurable coefficients to simulate environmental drift.
5. **Settings Update / Refresh** – `MockSettingsService` applies updates, validates values, and shares snapshots with change counters for optimistic concurrency.
6. **Battery Status** – `MockBatteryService::read()` returns the latest percent/voltage tuple and optional warning codes without touching hardware.
7. **Alerts** – `MockAlertService` can be triggered by user commands or background timers to mimic sensor faults, low battery, or calibration warnings.
8. **Session Teardown** – The controller stops background timers (all mock-level), flushes pending notifications, and drops the BLE connection cleanly.

## Test Strategy

- **Unity Tests**: Place a new suite under `test/test_mock_main/` that exercises each feature via the controller’s public API (command handlers, telemetry getters, alert injection). Tests should not require BLE hardware; they’ll call the lower layers directly.
- **Legacy Test Isolation**: Disable or skip suites that depend on hardware drivers when running under the `mock_main` environment so the mock stays free of device-level symbols.
- **Data Control with Fuzziness**: Default mock measurements should be generated from a seeded pseudo-random sequence so repeated runs are reproducible in tests, while still emitting realistic variability between successive measurements. Expose knobs to adjust the amount of jitter for QA scenarios.

## Outstanding Questions

- Do we want the mock to support the full Phoenix Benchmark command set (`channel_map`, `dwell_sweep`, etc.), or only the phone app flows documented in the feature backlog?
- Should alert injection be scriptable from the phone app (command-based) or only via developer toggles on the firmware side?
- How closely should the mock emulate timing (delays between reference/sample steps, alert cadence) versus returning results immediately?

Answer these as part of Phase 1 sign-off before implementation proceeds.
