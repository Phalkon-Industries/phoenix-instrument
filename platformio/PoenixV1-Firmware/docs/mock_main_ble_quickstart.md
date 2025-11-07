# Mock Main BLE Quick Start

This guide explains how to flash the mock firmware, discover it from the phone application, and exercise each feature over BLE. The mock focuses solely on the phone workflows documented in `docs/phone_instrument_interactions.md`; Phoenix Benchmark modules are intentionally out of scope. All payloads originate from the controller defined in `src/examples/mock_main_controller.*`.

## 1. Flash the Mock Firmware

1. Connect the Adafruit Feather nRF52840 to your workstation with USB power/data.
2. From the workspace root, build and upload the mock environment:
   ```
   pio run -e mock_main -t upload
   ```
3. (Optional) Run the Unity suite to confirm the controller behaviours before pairing:
   ```
   pio test -e mock_main -vv
   ```
4. Open a serial monitor at 115200 baud. The controller emits breadcrumbs tagged with `[mock_main]` for every command and response.

## 2. BLE Advertising Profile

| Property | Value |
| --- | --- |
| Device name | `Phoenix Mock` (`k_mock_ble_device_name`) |
| Primary service UUID | `b5c5d4f4-7207-4e8d-9d6b-60b98e65ce09` (`k_mock_ble_service_uuid`) |
| Command characteristic UUID | `b5c5d4f5-7207-4e8d-9d6b-60b98e65ce09` (`k_mock_ble_command_characteristic_uuid`), write-without-response |
| Notification characteristic UUID | `b5c5d4f6-7207-4e8d-9d6b-60b98e65ce09` (`k_mock_ble_notification_characteristic_uuid`), notify |
| MTU | 247 bytes (matches production configuration) |
| Connection interval | 30–50 ms preferred |

The mock reuses the production service layout so existing phone Discovery code can stay unchanged. If you need to segregate the mock in QA builds, filter by the advertised name `Phoenix Mock` or by the manufacturer data vendor ID `0x1209`.

## 3. Command Matrix

All commands are written as UTF-8 JSON payloads to the command characteristic. Each payload contains a `command` field and optional arguments. Responses arrive as JSON packets on the notification characteristic using the same field names that the Unity tests assert.

| Command | Sample request payload | Expected response | Notes |
| --- | --- | --- | --- |
| `reference_start` | `{ "command": "reference_start" }` | `{ "status": "ok", "sequence": 1, "dark_counts": 195000, "signal_counts": 210000, "absorbance": 0.15000, "temperature_c": 25.0, "salinity_ppt": 35.0 }` | Returns the calibrated reference bundle after roughly two seconds. Triggers `[mock_main] reference…` log. |
| `sample_start` | `{ "command": "sample_start" }` | `{ "status": "ok", "sequence": 1, "dark_counts": 196250, "signal_counts": 224500, "absorbance": 0.28250, "temperature_c": 25.5, "salinity_ppt": 35.2, "ph_value": 7.42 }` | Requires a reference first. On missing reference the mock replies `{ "status": "not_ready" }` and logs `[mock_main] sample rejected…`. |
| `settings_update` | `{ "command": "settings_update", "temperature_c": 24.5, "salinity_ppt": 34.7, "interval_ms": 90000, "alerts_enabled": false }` | `{ "status": "ok", "applied": { "temperature_c": 24.5, "salinity_ppt": 34.7, "interval_ms": 90000, "alerts_enabled": false, "configuration_hash": "FACEB00C" } }` | Hash is echoed as eight uppercase hex digits for UI display. |
| `settings_get` | `{ "command": "settings_get" }` | `{ "status": "ok", "snapshot": { "temperature_c": 25.0, "salinity_ppt": 35.0, "interval_ms": 120000, "alerts_enabled": true, "configuration_hash": "ABCD1234" } }` | Returns the current controller snapshot, honoring any prior updates. |
| `battery_status` | `{ "command": "battery_status" }` | `{ "status": "ok", "percentage": 78, "voltage_v": 3.92, "is_low": false, "response_delay_ms": 200 }` | Percentage jitter depends on seed; values clamp between 0 and 100. |
| `alert_inject` | `{ "command": "alert_inject" }` | `{ "status": "ok", "alert_code": "0004", "severity": 1, "message": "Mock sensor fault detected." }` | Mirrors the canned alert. Phone app should surface the message verbatim. |
| `session_teardown` | `{ "command": "session_teardown" }` | `{ "status": "ok", "summary": { "completed_references": 1, "completed_samples": 1, "uptime_ms": 600000, "ble_connected": false } }` | Use when leaving the mock; after this the device drops the BLE link. |

## 4. Telemetry & Troubleshooting

- Every command triggers a structured `Serial` log line with the `[mock_main]` prefix. Capture these at 115200 baud to correlate phone traffic with controller actions.
- The seed (`mock_app_controller_set_seed`) controls jitter reproducibility. Setting the seed to `0` (default) disables jitter so payloads match the Unity expectations above.
- When testing negative paths, attempt a `sample_start` without a prior `reference_start` or send malformed JSON. The controller responds with `not_ready` or `invalid_argument` and the serial log records the rejection cause.
- If BLE discovery fails, verify that the board is running the `mock_main` environment (it advertises `Phoenix Mock`) and not another firmware that might power down TinyUSB.

For further architectural details see `docs/mock_main_architecture.md`. Update both documents if command schemas, UUIDs, or logging formats change.
