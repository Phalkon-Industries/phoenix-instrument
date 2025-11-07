#include "mock_main_ble_bridge.hpp"

#include "mock_main_controller.hpp"
#include "phoenix_ble_data_packing.hpp"
#include "phoenix_guard.hpp"

#include <Arduino.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr size_t k_mock_request_buffer_size      = 256U;
constexpr size_t k_mock_notification_buffer_size = 256U;
constexpr size_t k_mock_parameters_buffer_size   = 192U;

PhoenixBleServerContext* g_ble_context = NULL;
char                     g_notification_buffer[k_mock_notification_buffer_size] = {};

const char* k_status_ok                = "\"status\":\"ok\"";
const char* k_status_not_ready         = "\"status\":\"not_ready\"";
const char* k_status_invalid_argument  = "\"status\":\"invalid_argument\"";
const char* k_status_error             = "\"status\":\"error\"";
const char* k_status_invalid_command   = "\"status\":\"invalid_command\"";

void apply_response_delay(uint32_t delay_ms) {
#ifndef UNIT_TEST
  if (delay_ms > 0U) {
    delay(delay_ms);
  }
#else
  (void) delay_ms;
#endif
}

bool extract_command_name(const char* json, char* command_buffer, size_t buffer_size) {
  if ((json == NULL) || (command_buffer == NULL) || (buffer_size == 0U)) {
    return false;
  }

  const char* command_token = strstr(json, "\"command\"");
  if (command_token == NULL) {
    return false;
  }

  const char* colon = strchr(command_token, ':');
  if (colon == NULL) {
    return false;
  }

  colon++;
  while ((*colon != '\0') && isspace(static_cast<unsigned char>(*colon))) {
    colon++;
  }

  if (*colon != '"') {
    return false;
  }

  colon++;
  const char* end_quote = strchr(colon, '"');
  if (end_quote == NULL) {
    return false;
  }

  size_t command_length = static_cast<size_t>(end_quote - colon);
  if (command_length >= buffer_size) {
    command_length = buffer_size - 1U;
  }

  memcpy(command_buffer, colon, command_length);
  command_buffer[command_length] = '\0';

  return true;
}

bool locate_field_token(const char* json, const char* field_name, const char** value_start) {
  if ((json == NULL) || (field_name == NULL) || (value_start == NULL)) {
    return false;
  }

  char token[32] = {};
  const int written = snprintf(token, sizeof(token), "\"%s\"", field_name);
  if ((written <= 0) || (static_cast<size_t>(written) >= sizeof(token))) {
    return false;
  }

  const char* field_token = strstr(json, token);
  if (field_token == NULL) {
    return false;
  }

  const char* colon = strchr(field_token, ':');
  if (colon == NULL) {
    return false;
  }

  colon++;
  while ((*colon != '\0') && isspace(static_cast<unsigned char>(*colon))) {
    colon++;
  }

  *value_start = colon;
  return true;
}

double extract_double(const char* json, const char* field_name, bool* found) {
  const char* value_start = NULL;
  if (!locate_field_token(json, field_name, &value_start)) {
    *found = false;
    return 0.0;
  }

  char* end_ptr     = NULL;
  double parsed_val = strtod(value_start, &end_ptr);
  if (value_start == end_ptr) {
    *found = false;
    return 0.0;
  }

  *found = true;
  return parsed_val;
}

uint32_t extract_uint32(const char* json, const char* field_name, bool* found) {
  const char* value_start = NULL;
  if (!locate_field_token(json, field_name, &value_start)) {
    *found = false;
    return 0U;
  }

  char*    end_ptr     = NULL;
  unsigned long parsed = strtoul(value_start, &end_ptr, 10);
  if (value_start == end_ptr) {
    *found = false;
    return 0U;
  }

  *found = true;
  return static_cast<uint32_t>(parsed);
}

bool extract_bool(const char* json, const char* field_name, bool* value) {
  const char* value_start = NULL;
  if (!locate_field_token(json, field_name, &value_start)) {
    return false;
  }

  if (strncmp(value_start, "true", strlen("true")) == 0) {
    *value = true;
    return true;
  }

  if (strncmp(value_start, "false", strlen("false")) == 0) {
    *value = false;
    return true;
  }

  return false;
}

PhoenixBleStatus send_response(const char* command, const char* parameters_json) {
  if ((command == NULL) || (parameters_json == NULL)) {
    return PHX_ERR_INVALID_ARG;
  }

  if (g_ble_context == NULL) {
    return PHX_ERR_NOT_INITIALIZED;
  }

  // Step 1: Pack the command envelope so the phone-side contract stays consistent with production.
  size_t           bytes_written = 0U;
  PhoenixBleStatus return_code   = phoenix_ble_pack_notification_message(command, parameters_json, g_notification_buffer,
                                                                         sizeof(g_notification_buffer), &bytes_written);
  if (return_code != PHX_OK) {
    return return_code;
  }

  // Step 2: Forward the payload through the currently registered BLE backend.
  return phoenix_ble_server_send_notification(g_ble_context, reinterpret_cast<const uint8_t*>(g_notification_buffer),
                                              static_cast<uint16_t>(bytes_written));
}

PhoenixBleStatus send_status_only(const char* command, const char* status_token) {
  char parameters_json[k_mock_parameters_buffer_size] = {};
  const int written = snprintf(parameters_json, sizeof(parameters_json), "{%s}", status_token);
  if ((written < 0) || (static_cast<size_t>(written) >= sizeof(parameters_json))) {
    return PHX_ERR_INVALID_ARG;
  }

  return send_response(command, parameters_json);
}

PhoenixBleStatus handle_reference_start(void) {
  MockReferenceMeasurement measurement = {};
  // Step 1: Request a spoofed reference so the phone receives the expected baseline payload.
  const MockAppStatus      controller_status = mock_app_controller_run_reference(&measurement);

  if (controller_status == MOCK_APP_STATUS_ERROR_NOT_READY) {
    return send_status_only("reference_start", k_status_not_ready);
  }

  if (controller_status != MOCK_APP_STATUS_OK) {
    return send_status_only("reference_start", k_status_error);
  }

  // Step 2: Mirror the production delay profile before emitting the notification.
  apply_response_delay(measurement.response_delay_ms);

  // Step 3: Assemble the JSON payload that mirrors the production schema.
  char parameters_json[k_mock_parameters_buffer_size] = {};
  const int written = snprintf(parameters_json, sizeof(parameters_json),
                               "{\"status\":\"ok\",\"sequence\":%lu,\"dark_counts\":%ld," \
                               "\"signal_counts\":%ld,\"absorbance\":%.5f,\"temperature_c\":%.2f," \
                               "\"salinity_ppt\":%.2f}",
                               static_cast<unsigned long>(measurement.sequence_id),
                               static_cast<long>(measurement.dark_counts),
                               static_cast<long>(measurement.signal_counts),
                               static_cast<double>(measurement.absorbance),
                               static_cast<double>(measurement.temperature_c),
                               static_cast<double>(measurement.salinity_ppt));

  if ((written < 0) || (static_cast<size_t>(written) >= sizeof(parameters_json))) {
    return PHX_ERR_INVALID_ARG;
  }

  return send_response("reference_start", parameters_json);
}

PhoenixBleStatus handle_sample_start(void) {
  MockSampleMeasurement measurement       = {};
  // Step 1: Generate the spoofed sample payload and honour controller guard rails.
  const MockAppStatus   controller_status = mock_app_controller_run_sample(&measurement);

  if (controller_status == MOCK_APP_STATUS_ERROR_NOT_READY) {
    return send_status_only("sample_start", k_status_not_ready);
  }

  if (controller_status != MOCK_APP_STATUS_OK) {
    return send_status_only("sample_start", k_status_error);
  }

  // Step 2: Replicate the on-hardware delay before replying to the phone client.
  apply_response_delay(measurement.response_delay_ms);

  // Step 3: Surface the deterministic payload using the same JSON keys as production firmware.
  char parameters_json[k_mock_parameters_buffer_size] = {};
  const int written = snprintf(parameters_json, sizeof(parameters_json),
                               "{\"status\":\"ok\",\"sequence\":%lu,\"dark_counts\":%ld," \
                               "\"signal_counts\":%ld,\"absorbance\":%.5f,\"temperature_c\":%.2f," \
                               "\"salinity_ppt\":%.2f,\"ph_value\":%.2f}",
                               static_cast<unsigned long>(measurement.sequence_id),
                               static_cast<long>(measurement.dark_counts),
                               static_cast<long>(measurement.signal_counts),
                               static_cast<double>(measurement.absorbance),
                               static_cast<double>(measurement.temperature_c),
                               static_cast<double>(measurement.salinity_ppt),
                               static_cast<double>(measurement.ph_value));

  if ((written < 0) || (static_cast<size_t>(written) >= sizeof(parameters_json))) {
    return PHX_ERR_INVALID_ARG;
  }

  return send_response("sample_start", parameters_json);
}

PhoenixBleStatus handle_settings_update(const char* request_json) {
  bool     field_present        = false;
  // Step 1: Parse the incoming JSON so the controller receives validated arguments.
  double   requested_temp       = extract_double(request_json, "temperature_c", &field_present);
  if (!field_present) {
    return send_status_only("settings_update", k_status_invalid_argument);
  }

  double requested_salinity = extract_double(request_json, "salinity_ppt", &field_present);
  if (!field_present) {
    return send_status_only("settings_update", k_status_invalid_argument);
  }

  uint32_t interval_ms = extract_uint32(request_json, "interval_ms", &field_present);
  if (!field_present) {
    return send_status_only("settings_update", k_status_invalid_argument);
  }

  bool alerts_enabled = false;
  if (!extract_bool(request_json, "alerts_enabled", &alerts_enabled)) {
    return send_status_only("settings_update", k_status_invalid_argument);
  }

  MockSettingsUpdate update_request = {};
  update_request.requested_temperature_c = static_cast<float>(requested_temp);
  update_request.requested_salinity_ppt  = static_cast<float>(requested_salinity);
  update_request.measurement_interval_ms = interval_ms;
  update_request.enable_alerts           = alerts_enabled;

  MockSettingsSnapshot applied_settings = {};
  // Step 2: Apply the configuration update to the controller so future reads match phone expectations.
  const MockAppStatus  controller_status = mock_app_controller_update_settings(&update_request, &applied_settings);

  if (controller_status != MOCK_APP_STATUS_OK) {
    return send_status_only("settings_update", k_status_error);
  }

  char hash_buffer[9] = {};
  (void) snprintf(hash_buffer, sizeof(hash_buffer), "%08lX",
                  static_cast<unsigned long>(applied_settings.configuration_hash));

  char parameters_json[k_mock_parameters_buffer_size] = {};
  const int written = snprintf(parameters_json, sizeof(parameters_json),
                               "{\"status\":\"ok\",\"applied\":{\"temperature_c\":%.2f," \
                               "\"salinity_ppt\":%.2f,\"interval_ms\":%lu,\"alerts_enabled\":%s," \
                               "\"configuration_hash\":\"%s\"}}",
                               static_cast<double>(applied_settings.active_temperature_c),
                               static_cast<double>(applied_settings.active_salinity_ppt),
                               static_cast<unsigned long>(applied_settings.measurement_interval_ms),
                               applied_settings.alerts_enabled ? "true" : "false",
                               hash_buffer);

  if ((written < 0) || (static_cast<size_t>(written) >= sizeof(parameters_json))) {
    return PHX_ERR_INVALID_ARG;
  }

  return send_response("settings_update", parameters_json);
}

PhoenixBleStatus handle_settings_get(void) {
  MockSettingsSnapshot snapshot = {};
  // Step 1: Capture the current controller snapshot to mirror the production `settings_get` response.
  const MockAppStatus  controller_status = mock_app_controller_get_settings(&snapshot);

  if (controller_status != MOCK_APP_STATUS_OK) {
    return send_status_only("settings_get", k_status_error);
  }

  char hash_buffer[9] = {};
  (void) snprintf(hash_buffer, sizeof(hash_buffer), "%08lX", static_cast<unsigned long>(snapshot.configuration_hash));

  char parameters_json[k_mock_parameters_buffer_size] = {};
  const int written = snprintf(parameters_json, sizeof(parameters_json),
                               "{\"status\":\"ok\",\"snapshot\":{\"temperature_c\":%.2f," \
                               "\"salinity_ppt\":%.2f,\"interval_ms\":%lu,\"alerts_enabled\":%s," \
                               "\"configuration_hash\":\"%s\"}}",
                               static_cast<double>(snapshot.active_temperature_c),
                               static_cast<double>(snapshot.active_salinity_ppt),
                               static_cast<unsigned long>(snapshot.measurement_interval_ms),
                               snapshot.alerts_enabled ? "true" : "false",
                               hash_buffer);

  if ((written < 0) || (static_cast<size_t>(written) >= sizeof(parameters_json))) {
    return PHX_ERR_INVALID_ARG;
  }

  return send_response("settings_get", parameters_json);
}

PhoenixBleStatus handle_battery_status(void) {
  MockBatteryStatus battery_status     = {};
  // Step 1: Request the cached battery telemetry so BLE replies stay deterministic for QA.
  const MockAppStatus controller_status = mock_app_controller_read_battery(&battery_status);

  if (controller_status != MOCK_APP_STATUS_OK) {
    return send_status_only("battery_status", k_status_error);
  }

  // Step 2: Recreate the production pacing before returning the canned response.
  apply_response_delay(battery_status.response_delay_ms);

  // Step 3: Emit the JSON packet that matches the phone-facing schema.
  char parameters_json[k_mock_parameters_buffer_size] = {};
  const int written = snprintf(parameters_json, sizeof(parameters_json),
                               "{\"status\":\"ok\",\"percentage\":%u,\"voltage_v\":%.2f," \
                               "\"is_low\":%s,\"response_delay_ms\":%lu}",
                               static_cast<unsigned int>(battery_status.percentage),
                               static_cast<double>(battery_status.voltage_v),
                               battery_status.is_low ? "true" : "false",
                               static_cast<unsigned long>(battery_status.response_delay_ms));

  if ((written < 0) || (static_cast<size_t>(written) >= sizeof(parameters_json))) {
    return PHX_ERR_INVALID_ARG;
  }

  return send_response("battery_status", parameters_json);
}

PhoenixBleStatus handle_alert_inject(void) {
  MockAlertNotification notification     = {};
  // Step 1: Surface the canned alert payload so the phone exercises its notification UI.
  const MockAppStatus    controller_status = mock_app_controller_raise_alert(&notification);

  if (controller_status != MOCK_APP_STATUS_OK) {
    return send_status_only("alert_inject", k_status_error);
  }

  char alert_code_buffer[5] = {};
  (void) snprintf(alert_code_buffer, sizeof(alert_code_buffer), "%04lX",
                  static_cast<unsigned long>(notification.alert_code));

  const char* message = (notification.message != NULL) ? notification.message : "";

  char parameters_json[k_mock_parameters_buffer_size] = {};
  const int written = snprintf(parameters_json, sizeof(parameters_json),
                               "{\"status\":\"ok\",\"alert_code\":\"%s\",\"severity\":%lu," \
                               "\"message\":\"%s\"}",
                               alert_code_buffer,
                               static_cast<unsigned long>(notification.severity),
                               message);

  if ((written < 0) || (static_cast<size_t>(written) >= sizeof(parameters_json))) {
    return PHX_ERR_INVALID_ARG;
  }

  return send_response("alert_inject", parameters_json);
}

PhoenixBleStatus handle_session_teardown(void) {
  MockSessionSummary summary             = {};
  // Step 1: Ask the controller for its session summary and shut down BLE state.
  const MockAppStatus controller_status = mock_app_controller_teardown(&summary);

  if (controller_status != MOCK_APP_STATUS_OK) {
    return send_status_only("session_teardown", k_status_error);
  }

  (void) phoenix_ble_server_handle_connection_event(g_ble_context, 0U);

  // Step 2: Report the teardown metrics so the phone can display an accurate handoff summary.
  char parameters_json[k_mock_parameters_buffer_size] = {};
  const int written = snprintf(parameters_json, sizeof(parameters_json),
                               "{\"status\":\"ok\",\"summary\":{\"completed_references\":%lu," \
                               "\"completed_samples\":%lu,\"uptime_ms\":%lu,\"ble_connected\":%s}}",
                               static_cast<unsigned long>(summary.completed_references),
                               static_cast<unsigned long>(summary.completed_samples),
                               static_cast<unsigned long>(summary.uptime_ms),
                               summary.ble_connected ? "true" : "false");

  if ((written < 0) || (static_cast<size_t>(written) >= sizeof(parameters_json))) {
    return PHX_ERR_INVALID_ARG;
  }

  return send_response("session_teardown", parameters_json);
}
}  // namespace

PhoenixBleStatus mock_main_ble_bridge_initialize(PhoenixBleServerContext* context) {
  GUARD_NONNULL(context);

  // Step 1: Register the bridge handler so BLE command writes reach the mock controller.
  PhoenixBleStatus return_code = phoenix_ble_server_register_command_handler(context, mock_main_ble_bridge_process_command);
  if (return_code != PHX_OK) {
    return return_code;
  }

  g_ble_context = context;
  return PHX_OK;
}

PhoenixBleStatus mock_main_ble_bridge_process_command(const uint8_t* payload, uint16_t payload_length) {
  GUARD_NONNULL(payload);

  if (payload_length == 0U) {
    return PHX_ERR_INVALID_ARG;
  }

  if (payload_length >= k_mock_request_buffer_size) {
    return PHX_ERR_INVALID_ARG;
  }

  // Step 1: Copy and null-terminate the inbound JSON so it can be parsed safely.
  char request_buffer[k_mock_request_buffer_size] = {};
  memcpy(request_buffer, payload, payload_length);
  request_buffer[payload_length] = '\0';

  // Step 2: Extract the command token to decide which controller pathway to exercise.
  char command_name[32] = {};
  if (!extract_command_name(request_buffer, command_name, sizeof(command_name))) {
    return PHX_ERR_INVALID_ARG;
  }

  if (strcmp(command_name, "reference_start") == 0) {
    return handle_reference_start();
  }

  if (strcmp(command_name, "sample_start") == 0) {
    return handle_sample_start();
  }

  if (strcmp(command_name, "settings_update") == 0) {
    return handle_settings_update(request_buffer);
  }

  if (strcmp(command_name, "settings_get") == 0) {
    return handle_settings_get();
  }

  if (strcmp(command_name, "battery_status") == 0) {
    return handle_battery_status();
  }

  if (strcmp(command_name, "alert_inject") == 0) {
    return handle_alert_inject();
  }

  if (strcmp(command_name, "session_teardown") == 0) {
    return handle_session_teardown();
  }

  return send_status_only(command_name, k_status_invalid_command);
}
