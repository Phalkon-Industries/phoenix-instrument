#include "mocks/mock_main_ble_bridge.hpp"
#include "mocks/mock_main_controller.hpp"
#include "unity_config.h"
#include "phoenix_ble_server.hpp"
#include "phoenix_ble_stack.hpp"
#include <Adafruit_TinyUSB.h>
#include <string.h>
#include <unity.h>

namespace {
static PhoenixBleServerContext g_bridge_context = {};
static char                    g_last_notification[256] = {};
static size_t                  g_last_notification_length = 0U;
static uint8_t                 g_notification_count        = 0U;

PhoenixBleStatus test_backend_initialize(PhoenixBleServerContext* context, const PhoenixBleConfig* config) {
  (void) context;
  (void) config;
  return PHX_OK;
}

PhoenixBleStatus test_backend_start_advertising(PhoenixBleServerContext* context) {
  (void) context;
  return PHX_OK;
}

PhoenixBleStatus test_backend_stop_advertising(PhoenixBleServerContext* context) {
  (void) context;
  return PHX_OK;
}

PhoenixBleStatus test_backend_send_notification(PhoenixBleServerContext* context, const uint8_t* payload,
                                                uint16_t payload_length) {
  (void) context;
  if ((payload == NULL) || (payload_length == 0U) || (payload_length >= sizeof(g_last_notification))) {
    return PHX_ERR_INVALID_ARG;
  }

  memcpy(g_last_notification, payload, payload_length);
  g_last_notification[payload_length] = '\0';
  g_last_notification_length          = payload_length;
  g_notification_count++;
  return PHX_OK;
}

const PhoenixBleBackend k_test_backend = {
    test_backend_initialize,
    test_backend_start_advertising,
    test_backend_stop_advertising,
    test_backend_send_notification,
};

void prepare_ble_bridge(void) {
  // Step 1: Reset the stub backend state so each test observes fresh notifications.
  memset(&g_bridge_context, 0, sizeof(g_bridge_context));
  memset(g_last_notification, 0, sizeof(g_last_notification));
  g_last_notification_length = 0U;
  g_notification_count        = 0U;

  // Step 2: Register the stub backend and initialise the facade context.
  PhoenixBleStatus return_code = phoenix_ble_server_register_backend(&k_test_backend);
  TEST_ASSERT_EQUAL(PHX_OK, return_code);

  PhoenixBleConfig config = phoenix_ble_stack_make_default_config();

  return_code = phoenix_ble_server_initialize(&g_bridge_context, &config);
  TEST_ASSERT_EQUAL(PHX_OK, return_code);

  // Step 3: Attach the mock bridge so command dispatch mirrors the production firmware.
  return_code = mock_main_ble_bridge_initialize(&g_bridge_context);
  TEST_ASSERT_EQUAL(PHX_OK, return_code);

  // Step 4: Mark the context connected so the notification pathway remains enabled.
  return_code = phoenix_ble_server_handle_connection_event(&g_bridge_context, 1U);
  TEST_ASSERT_EQUAL(PHX_OK, return_code);
}
}  // namespace

void setUp(void) {
  // Step 1: Prepare the controller with deterministic outputs for the current test.
  const MockAppStatus init_status = mock_app_controller_initialize();
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, init_status);
  const MockAppStatus seed_status = mock_app_controller_set_seed(k_mock_seed_disable_jitter);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, seed_status);
}
void tearDown(void) {
}

static void test_reference_measurement_returns_spoofed_packet(void) {
  MockReferenceMeasurement measurement = {};

  const MockAppStatus status = mock_app_controller_run_reference(&measurement);

  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, status);
  TEST_ASSERT_EQUAL_INT32(k_mock_reference_dark_base_counts, measurement.dark_counts);
  TEST_ASSERT_EQUAL_INT32(k_mock_reference_signal_base_counts, measurement.signal_counts);

  const float expected_absorbance = static_cast<float>(measurement.signal_counts - measurement.dark_counts) / 100000.0f;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected_absorbance, measurement.absorbance);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_reference_temperature_c, measurement.temperature_c);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_reference_salinity_ppt, measurement.salinity_ppt);
  TEST_ASSERT_EQUAL_UINT32(k_mock_reference_response_delay_ms, measurement.response_delay_ms);
  TEST_ASSERT_EQUAL_UINT32(1u, measurement.sequence_id);
}

static void test_sample_measurement_requires_reference_then_returns_spoofed_packet(void) {
  MockReferenceMeasurement reference_measurement = {};
  MockSampleMeasurement    sample_measurement    = {};

  // Step 1: Acquire a reference baseline so the sample pathway has calibration data.
  const MockAppStatus reference_status = mock_app_controller_run_reference(&reference_measurement);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, reference_status);

  // Step 2: Request a sample measurement and expect the canned payload.
  const MockAppStatus sample_status = mock_app_controller_run_sample(&sample_measurement);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, sample_status);
  TEST_ASSERT_EQUAL_INT32(k_mock_sample_dark_base_counts, sample_measurement.dark_counts);
  TEST_ASSERT_EQUAL_INT32(k_mock_sample_signal_base_counts, sample_measurement.signal_counts);

  const float expected_absorbance =
      static_cast<float>(k_mock_sample_signal_base_counts - k_mock_sample_dark_base_counts) / 100000.0f;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected_absorbance, sample_measurement.absorbance);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_sample_temperature_c, sample_measurement.temperature_c);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_sample_salinity_ppt, sample_measurement.salinity_ppt);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_sample_ph_value, sample_measurement.ph_value);
  TEST_ASSERT_EQUAL_UINT32(k_mock_sample_response_delay_ms, sample_measurement.response_delay_ms);
  TEST_ASSERT_EQUAL_UINT32(k_mock_sample_sequence_id, sample_measurement.sequence_id);
}

static void test_settings_update_applies_and_confirms_changes(void) {
  MockSettingsUpdate update_request      = {};
  update_request.requested_temperature_c = k_mock_updated_temperature_c;
  update_request.requested_salinity_ppt  = k_mock_updated_salinity_ppt;
  update_request.measurement_interval_ms = k_mock_updated_measurement_interval_ms;
  update_request.enable_alerts           = k_mock_updated_alerts_enabled;

  MockSettingsSnapshot applied_settings = {};

  // Step 1: Apply the new configuration and expect the controller to confirm the change.
  const MockAppStatus update_status = mock_app_controller_update_settings(&update_request, &applied_settings);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, update_status);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_updated_temperature_c, applied_settings.active_temperature_c);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_updated_salinity_ppt, applied_settings.active_salinity_ppt);
  TEST_ASSERT_EQUAL_UINT32(k_mock_updated_measurement_interval_ms, applied_settings.measurement_interval_ms);
  TEST_ASSERT_EQUAL(k_mock_updated_alerts_enabled, applied_settings.alerts_enabled);
  TEST_ASSERT_EQUAL_UINT32(k_mock_updated_configuration_hash, applied_settings.configuration_hash);
}

static void test_settings_refresh_reports_current_state(void) {
  MockSettingsSnapshot snapshot = {};

  // Step 1: Read the active configuration and compare it to the factory defaults.
  const MockAppStatus snapshot_status = mock_app_controller_get_settings(&snapshot);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, snapshot_status);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_default_temperature_c, snapshot.active_temperature_c);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_default_salinity_ppt, snapshot.active_salinity_ppt);
  TEST_ASSERT_EQUAL_UINT32(k_mock_default_measurement_interval_ms, snapshot.measurement_interval_ms);
  TEST_ASSERT_EQUAL(k_mock_default_alerts_enabled, snapshot.alerts_enabled);
  TEST_ASSERT_EQUAL_UINT32(k_mock_default_configuration_hash, snapshot.configuration_hash);
}

static void test_battery_status_reports_jittered_reading(void) {
  MockBatteryStatus battery_status = {};

  // Step 1: Request a mock battery sample and verify the canned response.
  const MockAppStatus status = mock_app_controller_read_battery(&battery_status);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, status);
  TEST_ASSERT_EQUAL_UINT8(k_mock_battery_percentage, battery_status.percentage);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, k_mock_battery_voltage_v, battery_status.voltage_v);
  TEST_ASSERT_EQUAL(k_mock_battery_is_low, battery_status.is_low);
  TEST_ASSERT_EQUAL_UINT32(k_mock_battery_response_delay_ms, battery_status.response_delay_ms);
}

static void test_alert_flow_pushes_notification_to_phone(void) {
  MockAlertNotification notification = {};

  // Step 1: Raise a canned alert and ensure the notification mirrors the production schema.
  const MockAppStatus status = mock_app_controller_raise_alert(&notification);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, status);
  TEST_ASSERT_EQUAL_UINT32(k_mock_alert_code_sensor_fault, notification.alert_code);
  TEST_ASSERT_EQUAL_UINT32(k_mock_alert_severity_warning, notification.severity);
  TEST_ASSERT_NOT_NULL(notification.message);
  TEST_ASSERT_EQUAL_STRING(k_mock_alert_message_sensor_fault, notification.message);
}

static void test_ble_session_teardown_cleans_up_state(void) {
  MockReferenceMeasurement reference_measurement = {};
  MockSampleMeasurement    sample_measurement    = {};
  MockSessionSummary       session_summary       = {};

  // Step 1: Exercise the measurement flows so the teardown summary has history to report.
  const MockAppStatus reference_status = mock_app_controller_run_reference(&reference_measurement);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, reference_status);
  const MockAppStatus sample_status = mock_app_controller_run_sample(&sample_measurement);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, sample_status);

  // Step 2: Teardown the session and ensure the summary reflects the work performed.
  const MockAppStatus teardown_status = mock_app_controller_teardown(&session_summary);
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_OK, teardown_status);
  TEST_ASSERT_EQUAL_UINT32(1u, session_summary.completed_references);
  TEST_ASSERT_EQUAL_UINT32(1u, session_summary.completed_samples);
  TEST_ASSERT_EQUAL_UINT32(k_mock_teardown_expected_uptime_ms, session_summary.uptime_ms);
  TEST_ASSERT_EQUAL(k_mock_teardown_expected_ble_connected, session_summary.ble_connected);
}

static void test_sample_measurement_without_reference_returns_not_ready(void) {
  MockSampleMeasurement sample_measurement = {};

  // Step 1: Request a sample before the reference baseline exists.
  const MockAppStatus status = mock_app_controller_run_sample(&sample_measurement);

  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_ERROR_NOT_READY, status);
}

static void test_guard_macros_reject_null_arguments(void) {
  // Step 1: Ensure each controller API returns the guard error on NULL input.
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_ERROR_INVALID_ARG, mock_app_controller_run_reference(NULL));
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_ERROR_INVALID_ARG, mock_app_controller_run_sample(NULL));
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_ERROR_INVALID_ARG, mock_app_controller_update_settings(NULL, NULL));

  MockSettingsUpdate update = {};
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_ERROR_INVALID_ARG, mock_app_controller_update_settings(&update, NULL));

  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_ERROR_INVALID_ARG, mock_app_controller_get_settings(NULL));
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_ERROR_INVALID_ARG, mock_app_controller_read_battery(NULL));
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_ERROR_INVALID_ARG, mock_app_controller_raise_alert(NULL));
  TEST_ASSERT_EQUAL(MOCK_APP_STATUS_ERROR_INVALID_ARG, mock_app_controller_teardown(NULL));
}

static void test_ble_bridge_reference_start_publishes_notification(void) {
  prepare_ble_bridge();

  const char request[] = "{\"command\":\"reference_start\"}";

  const PhoenixBleStatus return_code =
      mock_main_ble_bridge_process_command(reinterpret_cast<const uint8_t*>(request), strlen(request));

  TEST_ASSERT_EQUAL(PHX_OK, return_code);
  TEST_ASSERT_EQUAL_UINT8(1U, g_notification_count);
  TEST_ASSERT_NOT_EQUAL(0U, g_last_notification_length);
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"command\":\"reference_start\""));
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"status\":\"ok\""));
}

static void test_ble_bridge_sample_requires_reference(void) {
  prepare_ble_bridge();

  const char request[] = "{\"command\":\"sample_start\"}";

  const PhoenixBleStatus return_code =
      mock_main_ble_bridge_process_command(reinterpret_cast<const uint8_t*>(request), strlen(request));

  TEST_ASSERT_EQUAL(PHX_OK, return_code);
  TEST_ASSERT_EQUAL_UINT8(1U, g_notification_count);
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"status\":\"not_ready\""));
}

static void test_ble_bridge_sample_after_reference_returns_payload(void) {
  prepare_ble_bridge();

  const char reference_request[] = "{\"command\":\"reference_start\"}";
  const PhoenixBleStatus reference_code = mock_main_ble_bridge_process_command(
      reinterpret_cast<const uint8_t*>(reference_request), strlen(reference_request));
  TEST_ASSERT_EQUAL(PHX_OK, reference_code);

  const char sample_request[] = "{\"command\":\"sample_start\"}";
  const PhoenixBleStatus sample_code = mock_main_ble_bridge_process_command(
      reinterpret_cast<const uint8_t*>(sample_request), strlen(sample_request));

  TEST_ASSERT_EQUAL(PHX_OK, sample_code);
  TEST_ASSERT_EQUAL_UINT8(2U, g_notification_count);
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"command\":\"sample_start\""));
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"ph_value\""));
}

static void test_ble_bridge_settings_update_surfaces_snapshot(void) {
  prepare_ble_bridge();

  const char request[] =
      "{\"command\":\"settings_update\",\"temperature_c\":24.5,\"salinity_ppt\":34.7,"
      "\"interval_ms\":90000,\"alerts_enabled\":false}";

  const PhoenixBleStatus return_code =
      mock_main_ble_bridge_process_command(reinterpret_cast<const uint8_t*>(request), strlen(request));

  TEST_ASSERT_EQUAL(PHX_OK, return_code);
  TEST_ASSERT_EQUAL_UINT8(1U, g_notification_count);
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"command\":\"settings_update\""));
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"configuration_hash\":\"FACEB00C\""));
}

static void test_ble_bridge_session_teardown_reports_summary(void) {
  prepare_ble_bridge();

  const char reference_request[] = "{\"command\":\"reference_start\"}";
  const PhoenixBleStatus reference_code = mock_main_ble_bridge_process_command(
      reinterpret_cast<const uint8_t*>(reference_request), strlen(reference_request));
  TEST_ASSERT_EQUAL(PHX_OK, reference_code);

  const char sample_request[] = "{\"command\":\"sample_start\"}";
  const PhoenixBleStatus sample_code = mock_main_ble_bridge_process_command(
      reinterpret_cast<const uint8_t*>(sample_request), strlen(sample_request));
  TEST_ASSERT_EQUAL(PHX_OK, sample_code);

  const char teardown_request[] = "{\"command\":\"session_teardown\"}";
  const PhoenixBleStatus teardown_code = mock_main_ble_bridge_process_command(
      reinterpret_cast<const uint8_t*>(teardown_request), strlen(teardown_request));

  TEST_ASSERT_EQUAL(PHX_OK, teardown_code);
  TEST_ASSERT_EQUAL_UINT8(3U, g_notification_count);
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"command\":\"session_teardown\""));
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"completed_samples\":1"));
  TEST_ASSERT_NOT_NULL(strstr(g_last_notification, "\"ble_connected\":false"));
}

void setup(void) {
  // Step 1: Bring up TinyUSB-backed serial before starting Unity.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2: Kick off the Unity harness for the mock-only suite.
  UNITY_BEGIN();

  RUN_TEST(test_reference_measurement_returns_spoofed_packet);
  RUN_TEST(test_sample_measurement_requires_reference_then_returns_spoofed_packet);
  RUN_TEST(test_settings_update_applies_and_confirms_changes);
  RUN_TEST(test_settings_refresh_reports_current_state);
  RUN_TEST(test_battery_status_reports_jittered_reading);
  RUN_TEST(test_alert_flow_pushes_notification_to_phone);
  RUN_TEST(test_ble_session_teardown_cleans_up_state);
  RUN_TEST(test_sample_measurement_without_reference_returns_not_ready);
  RUN_TEST(test_guard_macros_reject_null_arguments);
  RUN_TEST(test_ble_bridge_reference_start_publishes_notification);
  RUN_TEST(test_ble_bridge_sample_requires_reference);
  RUN_TEST(test_ble_bridge_sample_after_reference_returns_payload);
  RUN_TEST(test_ble_bridge_settings_update_surfaces_snapshot);
  RUN_TEST(test_ble_bridge_session_teardown_reports_summary);

  // Step 3: Finalize the Unity session so loop() can idle.
  UNITY_END();
}

void loop(void) {
}
