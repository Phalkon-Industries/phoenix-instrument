#include "mock_main_controller.hpp"

#include <stddef.h>

#include "phoenix_guard.hpp"

namespace
{
struct MockAppControllerState
{
    bool is_initialized;
    uint32_t seed;
    uint32_t jitter_counter;
    uint32_t reference_count;
    uint32_t sample_count;
    MockSettingsSnapshot settings;
    MockBatteryStatus battery;
    MockAlertNotification alert;
    bool ble_connected;
};

MockAppControllerState g_controller_state = {};

constexpr int32_t k_mock_reference_dark_jitter_counts = 1000;
constexpr int32_t k_mock_reference_signal_jitter_counts = 1000;
constexpr int32_t k_mock_temperature_jitter_centideg = 100;
constexpr int32_t k_mock_salinity_jitter_centippt = 100;
constexpr int32_t k_mock_sample_dark_jitter_counts = 1500;
constexpr int32_t k_mock_sample_signal_jitter_counts = 2000;
constexpr int32_t k_mock_sample_temperature_jitter_centideg = 120;
constexpr int32_t k_mock_sample_salinity_jitter_centippt = 120;
constexpr int32_t k_mock_sample_ph_jitter_centiph = 15;
constexpr int32_t k_mock_battery_percentage_jitter = 4;
constexpr int32_t k_mock_battery_voltage_centivolt_jitter = 8;

uint32_t scramble_seed(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

int32_t generate_offset(int32_t amplitude)
{
    if ((g_controller_state.seed == k_mock_seed_disable_jitter) || (amplitude == 0))
    {
        return 0;
    }

    g_controller_state.jitter_counter++;
    const uint32_t hash_input = g_controller_state.seed + g_controller_state.jitter_counter;
    const uint32_t hashed = scramble_seed(hash_input);
    const uint32_t range = static_cast<uint32_t>((2 * amplitude) + 1);
    const uint32_t span = hashed % range;
    return static_cast<int32_t>(span) - amplitude;
}

float calculate_absorbance(int32_t signal_counts, int32_t dark_counts)
{
    const int32_t net_counts = signal_counts - dark_counts;
    return static_cast<float>(net_counts) / 100000.0F;
}

float apply_centiscale(float base_value, int32_t centi_offset)
{
    return base_value + (static_cast<float>(centi_offset) / 100.0F);
}

void reset_controller_state(void)
{
    g_controller_state.is_initialized = false;
    g_controller_state.seed = k_mock_seed_disable_jitter;
    g_controller_state.jitter_counter = 0U;
    g_controller_state.reference_count = 0U;
    g_controller_state.sample_count = 0U;
    g_controller_state.settings.active_temperature_c = k_mock_default_temperature_c;
    g_controller_state.settings.active_salinity_ppt = k_mock_default_salinity_ppt;
    g_controller_state.settings.measurement_interval_ms = k_mock_default_measurement_interval_ms;
    g_controller_state.settings.alerts_enabled = k_mock_default_alerts_enabled;
    g_controller_state.settings.configuration_hash = k_mock_default_configuration_hash;
    g_controller_state.battery.percentage = k_mock_battery_percentage;
    g_controller_state.battery.voltage_v = k_mock_battery_voltage_v;
    g_controller_state.battery.is_low = k_mock_battery_is_low;
    g_controller_state.battery.response_delay_ms = k_mock_battery_response_delay_ms;
    g_controller_state.alert.alert_code = k_mock_alert_code_sensor_fault;
    g_controller_state.alert.severity = k_mock_alert_severity_warning;
    g_controller_state.alert.message = k_mock_alert_message_sensor_fault;
    g_controller_state.ble_connected = false;
}
} // namespace

MockAppStatus mock_app_controller_initialize(void)
{
    // Step 1: Reset all cached mock data so each session starts from the same baseline.
    reset_controller_state();
    g_controller_state.is_initialized = true;
    g_controller_state.ble_connected = true;
    return MOCK_APP_STATUS_OK;
}

MockAppStatus mock_app_controller_set_seed(uint32_t seed)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    g_controller_state.seed = seed;
    g_controller_state.jitter_counter = 0U;
    return MOCK_APP_STATUS_OK;
}

MockAppStatus mock_app_controller_run_reference(MockReferenceMeasurement* measurement)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(measurement);

    g_controller_state.reference_count++;

    const int32_t dark_offset = generate_offset(k_mock_reference_dark_jitter_counts);
    const int32_t signal_offset = generate_offset(k_mock_reference_signal_jitter_counts);
    const int32_t temperature_offset = generate_offset(k_mock_temperature_jitter_centideg);
    const int32_t salinity_offset = generate_offset(k_mock_salinity_jitter_centippt);

    measurement->dark_counts = k_mock_reference_dark_base_counts + dark_offset;
    measurement->signal_counts = k_mock_reference_signal_base_counts + signal_offset;
    measurement->absorbance = calculate_absorbance(measurement->signal_counts, measurement->dark_counts);
    measurement->temperature_c = apply_centiscale(k_mock_reference_temperature_c, temperature_offset);
    measurement->salinity_ppt = apply_centiscale(k_mock_reference_salinity_ppt, salinity_offset);
    measurement->response_delay_ms = k_mock_reference_response_delay_ms;
    measurement->sequence_id = g_controller_state.reference_count;

    return MOCK_APP_STATUS_OK;
}

MockAppStatus mock_app_controller_run_sample(MockSampleMeasurement* measurement)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(measurement);

    // Step 1: Ensure a reference baseline exists so sample spoofing stays calibrated.
    if (g_controller_state.reference_count == 0U)
    {
        return MOCK_APP_STATUS_ERROR_NOT_READY;
    }

    // Step 2: Derive deterministic jitter so repeated samples vary when enabled.
    const int32_t dark_offset = generate_offset(k_mock_sample_dark_jitter_counts);
    const int32_t signal_offset = generate_offset(k_mock_sample_signal_jitter_counts);
    const int32_t temperature_offset = generate_offset(k_mock_sample_temperature_jitter_centideg);
    const int32_t salinity_offset = generate_offset(k_mock_sample_salinity_jitter_centippt);
    const int32_t ph_offset = generate_offset(k_mock_sample_ph_jitter_centiph);

    // Step 3: Populate the mock payload with canned data plus optional jitter.
    measurement->dark_counts = k_mock_sample_dark_base_counts + dark_offset;
    measurement->signal_counts = k_mock_sample_signal_base_counts + signal_offset;
    measurement->absorbance = calculate_absorbance(measurement->signal_counts, measurement->dark_counts);
    measurement->temperature_c = apply_centiscale(k_mock_sample_temperature_c, temperature_offset);
    measurement->salinity_ppt = apply_centiscale(k_mock_sample_salinity_ppt, salinity_offset);
    measurement->ph_value = apply_centiscale(k_mock_sample_ph_value, ph_offset);
    measurement->response_delay_ms = k_mock_sample_response_delay_ms;
    measurement->sequence_id = k_mock_sample_sequence_id + g_controller_state.sample_count;

    g_controller_state.sample_count++;

    return MOCK_APP_STATUS_OK;
}

MockAppStatus mock_app_controller_update_settings(const MockSettingsUpdate* update, MockSettingsSnapshot* applied_settings)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(update);
    GUARD_NONNULL(applied_settings);

    // Step 1: Apply the requested configuration so subsequent reads expose the new state.
    g_controller_state.settings.active_temperature_c = update->requested_temperature_c;
    g_controller_state.settings.active_salinity_ppt = update->requested_salinity_ppt;
    g_controller_state.settings.measurement_interval_ms = update->measurement_interval_ms;
    g_controller_state.settings.alerts_enabled = update->enable_alerts;
    g_controller_state.settings.configuration_hash = k_mock_updated_configuration_hash;

    *applied_settings = g_controller_state.settings;

    return MOCK_APP_STATUS_OK;
}

MockAppStatus mock_app_controller_get_settings(MockSettingsSnapshot* snapshot)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(snapshot);

    *snapshot = g_controller_state.settings;

    return MOCK_APP_STATUS_OK;
}

MockAppStatus mock_app_controller_read_battery(MockBatteryStatus* status)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(status);

    // Step 1: Apply deterministic jitter to the battery percentage so QA can experiment with seeds.
    const int32_t percentage_offset = generate_offset(k_mock_battery_percentage_jitter);
    int32_t percentage_value = static_cast<int32_t>(k_mock_battery_percentage) + percentage_offset;
    if (percentage_value < 0)
    {
        percentage_value = 0;
    }
    if (percentage_value > 100)
    {
        percentage_value = 100;
    }

    // Step 2: Simulate controller jitter so QA can observe drift when the seed is non-zero.
    const int32_t voltage_offset = generate_offset(k_mock_battery_voltage_centivolt_jitter);

    status->percentage = static_cast<uint8_t>(percentage_value);
    status->voltage_v = apply_centiscale(k_mock_battery_voltage_v, voltage_offset);
    status->is_low = (percentage_value <= 15) ? true : k_mock_battery_is_low;
    status->response_delay_ms = k_mock_battery_response_delay_ms;

    g_controller_state.battery = *status;

    return MOCK_APP_STATUS_OK;
}

MockAppStatus mock_app_controller_raise_alert(MockAlertNotification* notification)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(notification);

    // Step 1: Surface the canned alert payload to mimic the BLE notification.
    *notification = g_controller_state.alert;

    return MOCK_APP_STATUS_OK;
}

MockAppStatus mock_app_controller_teardown(MockSessionSummary* summary)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(summary);

    // Step 1: Report the completed work so the phone app can summarise the session.
    summary->completed_references = g_controller_state.reference_count;
    summary->completed_samples = g_controller_state.sample_count;
    summary->uptime_ms = k_mock_teardown_expected_uptime_ms;
    summary->ble_connected = k_mock_teardown_expected_ble_connected;

    g_controller_state.ble_connected = false;

    return MOCK_APP_STATUS_OK;
}
