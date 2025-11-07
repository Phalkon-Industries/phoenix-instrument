#ifndef MOCK_MAIN_CONTROLLER_HPP
#define MOCK_MAIN_CONTROLLER_HPP

#include "phoenix_guard.hpp"
#include <stdint.h>

using MockAppStatus = int32_t;

static constexpr MockAppStatus MOCK_APP_STATUS_OK                    = PHX_OK;
static constexpr MockAppStatus MOCK_APP_STATUS_ERROR_INVALID_ARG     = PHX_ERR_INVALID_ARG;
static constexpr MockAppStatus MOCK_APP_STATUS_ERROR_NOT_READY       = PHX_ERR_NOT_INITIALIZED;
static constexpr MockAppStatus MOCK_APP_STATUS_ERROR_NOT_IMPLEMENTED = PHX_ERR_NOT_IMPLEMENTED;

struct MockReferenceMeasurement {
  int32_t  dark_counts;
  int32_t  signal_counts;
  float    absorbance;
  float    temperature_c;
  float    salinity_ppt;
  uint32_t response_delay_ms;
  uint32_t sequence_id;
};

struct MockSampleMeasurement {
  int32_t  dark_counts;
  int32_t  signal_counts;
  float    absorbance;
  float    temperature_c;
  float    salinity_ppt;
  float    ph_value;
  uint32_t response_delay_ms;
  uint32_t sequence_id;
};

struct MockSettingsUpdate {
  float    requested_temperature_c;
  float    requested_salinity_ppt;
  uint32_t measurement_interval_ms;
  bool     enable_alerts;
};

struct MockSettingsSnapshot {
  float    active_temperature_c;
  float    active_salinity_ppt;
  uint32_t measurement_interval_ms;
  bool     alerts_enabled;
  uint32_t configuration_hash;
};

struct MockBatteryStatus {
  uint8_t  percentage;
  float    voltage_v;
  bool     is_low;
  uint32_t response_delay_ms;
};

struct MockAlertNotification {
  uint32_t    alert_code;
  uint32_t    severity;
  const char* message;
};

struct MockSessionSummary {
  uint32_t completed_references;
  uint32_t completed_samples;
  uint32_t uptime_ms;
  bool     ble_connected;
};

static constexpr int32_t  k_mock_reference_dark_base_counts   = 195000;
static constexpr int32_t  k_mock_reference_signal_base_counts = 210000;
static constexpr float    k_mock_reference_temperature_c      = 25.0F;
static constexpr float    k_mock_reference_salinity_ppt       = 35.0F;
static constexpr uint32_t k_mock_reference_response_delay_ms  = 2000U;
static constexpr uint32_t k_mock_seed_disable_jitter          = 0U;

static constexpr int32_t  k_mock_sample_dark_base_counts   = 196250;
static constexpr int32_t  k_mock_sample_signal_base_counts = 224500;
static constexpr float    k_mock_sample_temperature_c      = 25.5F;
static constexpr float    k_mock_sample_salinity_ppt       = 35.2F;
static constexpr float    k_mock_sample_ph_value           = 7.42F;
static constexpr uint32_t k_mock_sample_response_delay_ms  = 2000U;
static constexpr uint32_t k_mock_sample_sequence_id        = 1U;

static constexpr float    k_mock_default_temperature_c           = 25.0F;
static constexpr float    k_mock_default_salinity_ppt            = 35.0F;
static constexpr uint32_t k_mock_default_measurement_interval_ms = 120000U;
static constexpr bool     k_mock_default_alerts_enabled          = true;
static constexpr uint32_t k_mock_default_configuration_hash      = 0xABCD1234U;

static constexpr float    k_mock_updated_temperature_c           = 24.5F;
static constexpr float    k_mock_updated_salinity_ppt            = 34.7F;
static constexpr uint32_t k_mock_updated_measurement_interval_ms = 90000U;
static constexpr bool     k_mock_updated_alerts_enabled          = false;
static constexpr uint32_t k_mock_updated_configuration_hash      = 0xFACEB00CU;

static constexpr uint8_t  k_mock_battery_percentage        = 78U;
static constexpr float    k_mock_battery_voltage_v         = 3.92F;
static constexpr uint32_t k_mock_battery_response_delay_ms = 200U;
static constexpr bool     k_mock_battery_is_low            = false;

static constexpr uint32_t    k_mock_alert_code_sensor_fault    = 0x04U;
static constexpr uint32_t    k_mock_alert_severity_warning     = 1U;
static constexpr const char* k_mock_alert_message_sensor_fault = "Mock sensor fault detected.";

static constexpr uint32_t k_mock_teardown_expected_uptime_ms     = 600000U;
static constexpr bool     k_mock_teardown_expected_ble_connected = false;

static constexpr const char* k_mock_ble_device_name                      = "Phoenix Mock";
static constexpr const char* k_mock_ble_service_uuid                     = "b5c5d4f4-7207-4e8d-9d6b-60b98e65ce09";
static constexpr const char* k_mock_ble_command_characteristic_uuid      = "b5c5d4f5-7207-4e8d-9d6b-60b98e65ce09";
static constexpr const char* k_mock_ble_notification_characteristic_uuid = "b5c5d4f6-7207-4e8d-9d6b-60b98e65ce09";

MockAppStatus mock_app_controller_initialize(void);
MockAppStatus mock_app_controller_set_seed(uint32_t seed);
MockAppStatus mock_app_controller_run_reference(MockReferenceMeasurement* measurement);
MockAppStatus mock_app_controller_run_sample(MockSampleMeasurement* measurement);
MockAppStatus mock_app_controller_update_settings(const MockSettingsUpdate* update,
                                                  MockSettingsSnapshot*     applied_settings);
MockAppStatus mock_app_controller_get_settings(MockSettingsSnapshot* snapshot);
MockAppStatus mock_app_controller_read_battery(MockBatteryStatus* status);
MockAppStatus mock_app_controller_raise_alert(MockAlertNotification* notification);
MockAppStatus mock_app_controller_teardown(MockSessionSummary* summary);

#endif /* MOCK_MAIN_CONTROLLER_HPP */
