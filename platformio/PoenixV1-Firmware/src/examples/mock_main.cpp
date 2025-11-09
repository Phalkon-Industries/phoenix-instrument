#include "mock_main_ble_bridge.hpp"
#include "mock_main_controller.hpp"
#include "phoenix_ble_stack.hpp"

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#ifndef UNIT_TEST
namespace {
PhoenixBleServerContext g_ble_context = {};

void log_mock_error(const char* message, PhoenixBleStatus status) {
  Serial.print(message);
  Serial.print(": status=");
  Serial.println(static_cast<int32_t>(status));
}
}  // namespace

void setup(void)
{
    // Step 1: Initialise the TinyUSB serial interface before interacting with the controller.
    Serial.begin(115200);
        delay(100);

    // Step 2: Reset the mock controller so BLE commands surface deterministic payloads.
    if (mock_app_controller_initialize() != MOCK_APP_STATUS_OK) {
        Serial.println("[mock_main] error: controller initialisation failed");
        return;
    }

    (void) mock_app_controller_set_seed(k_mock_seed_disable_jitter);

    // Step 3: Bring up the shared Phoenix BLE stack and attach the mock command bridge.
    PhoenixBleStatus return_code = phoenix_ble_stack_initialize(&g_ble_context);
    if (return_code != PHX_OK) {
        log_mock_error("[mock_main] error: BLE stack initialisation failed", return_code);
        return;
    }

    return_code = mock_main_ble_bridge_initialize(&g_ble_context);
    if (return_code != PHX_OK) {
        log_mock_error("[mock_main] error: BLE bridge registration failed", return_code);
        return;
    }

    return_code = phoenix_ble_stack_start_advertising(&g_ble_context);
    if (return_code != PHX_OK) {
        log_mock_error("[mock_main] error: BLE advertising failed", return_code);
        return;
    }

    Serial.println("[mock_main] ready: advertising Phoenix Mock");
}

void loop(void)
{
    // Step 1: Yield to the scheduler so BLE callbacks execute promptly.
    delay(10);
}
#endif
