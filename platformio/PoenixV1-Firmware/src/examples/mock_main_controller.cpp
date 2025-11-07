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
};

MockAppControllerState g_controller_state = {false, k_mock_seed_disable_jitter, 0U, 0U};

constexpr int32_t k_mock_reference_dark_jitter_counts = 1000;
constexpr int32_t k_mock_reference_signal_jitter_counts = 1000;
constexpr int32_t k_mock_temperature_jitter_centideg = 100;
constexpr int32_t k_mock_salinity_jitter_centippt = 100;

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
} // namespace

MockAppStatus mock_app_controller_initialize(void)
{
    g_controller_state.is_initialized = true;
    g_controller_state.seed = k_mock_seed_disable_jitter;
    g_controller_state.jitter_counter = 0U;
    g_controller_state.reference_count = 0U;
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

    // Step 1: Signal that the behaviour has not been implemented yet.
    (void) measurement;
    return MOCK_APP_STATUS_ERROR_NOT_IMPLEMENTED;
}

MockAppStatus mock_app_controller_update_settings(const MockSettingsUpdate* update, MockSettingsSnapshot* applied_settings)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(update);
    GUARD_NONNULL(applied_settings);

    // Step 1: Signal that the behaviour has not been implemented yet.
    (void) update;
    (void) applied_settings;
    return MOCK_APP_STATUS_ERROR_NOT_IMPLEMENTED;
}

MockAppStatus mock_app_controller_get_settings(MockSettingsSnapshot* snapshot)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(snapshot);

    // Step 1: Signal that the behaviour has not been implemented yet.
    (void) snapshot;
    return MOCK_APP_STATUS_ERROR_NOT_IMPLEMENTED;
}

MockAppStatus mock_app_controller_read_battery(MockBatteryStatus* status)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(status);

    // Step 1: Signal that the behaviour has not been implemented yet.
    (void) status;
    return MOCK_APP_STATUS_ERROR_NOT_IMPLEMENTED;
}

MockAppStatus mock_app_controller_raise_alert(MockAlertNotification* notification)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(notification);

    // Step 1: Signal that the behaviour has not been implemented yet.
    (void) notification;
    return MOCK_APP_STATUS_ERROR_NOT_IMPLEMENTED;
}

MockAppStatus mock_app_controller_teardown(MockSessionSummary* summary)
{
    GUARD_INITIALIZED(g_controller_state.is_initialized);
    GUARD_NONNULL(summary);

    // Step 1: Signal that the behaviour has not been implemented yet.
    (void) summary;
    return MOCK_APP_STATUS_ERROR_NOT_IMPLEMENTED;
}
