#include "unity_config.h"

#include <Adafruit_TinyUSB.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static void test_reference_measurement_returns_spoofed_packet(void)
{
    TEST_FAIL_MESSAGE("reference measurement not implemented for mock main");
}

static void test_sample_measurement_requires_reference_then_returns_spoofed_packet(void)
{
    TEST_FAIL_MESSAGE("sample measurement not implemented for mock main");
}

static void test_settings_update_applies_and_confirms_changes(void)
{
    TEST_FAIL_MESSAGE("settings update not implemented for mock main");
}

static void test_settings_refresh_reports_current_state(void)
{
    TEST_FAIL_MESSAGE("settings refresh not implemented for mock main");
}

static void test_battery_status_reports_jittered_reading(void)
{
    TEST_FAIL_MESSAGE("battery status not implemented for mock main");
}

static void test_alert_flow_pushes_notification_to_phone(void)
{
    TEST_FAIL_MESSAGE("alert propagation not implemented for mock main");
}

static void test_ble_session_teardown_cleans_up_state(void)
{
    TEST_FAIL_MESSAGE("session teardown not implemented for mock main");
}

void setup(void)
{
    // Step 1: Bring up TinyUSB-backed serial before starting Unity.
    UNITY_SETUP_SERIAL_DEFAULT();
    // Step 2: Kick off the Unity harness for the mock-only suite.

    RUN_TEST(test_reference_measurement_returns_spoofed_packet);
    RUN_TEST(test_sample_measurement_requires_reference_then_returns_spoofed_packet);
    RUN_TEST(test_settings_update_applies_and_confirms_changes);
    RUN_TEST(test_settings_refresh_reports_current_state);
    RUN_TEST(test_battery_status_reports_jittered_reading);
    RUN_TEST(test_alert_flow_pushes_notification_to_phone);
    RUN_TEST(test_ble_session_teardown_cleans_up_state);

    // Step 3: Finalize the Unity session so loop() can idle.
    UNITY_END();
}

void loop(void)
{
}
