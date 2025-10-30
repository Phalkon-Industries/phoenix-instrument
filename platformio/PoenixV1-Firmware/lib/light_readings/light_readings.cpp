#include "light_readings.hpp"

#include "ad524x.hpp"
#include "phoenix_guard.hpp"
#include <Arduino.h>
#include <stddef.h>

static LightReadingsConfig g_light_config   = {};
static bool                g_is_initialized = false;

static constexpr uint8_t k_light_readings_blue_channel  = 0u;
static constexpr uint8_t k_light_readings_green_channel = 1u;

int light_readings_initialize(const LightReadingsConfig* config) {
  // Step 1: Validate the configuration pointer before touching shared state.
  GUARD_NONNULL(config);

  // Step 2: Park the router in the configured drain state so readings start from a known baseline.
  GUARD(led_router_set_state(config->drain_state));

  // Step 3: Program the digi-pot so colour-specific LED drive strengths start from a calibrated baseline.
  GUARD(ad524x_set_wiper(k_light_readings_blue_channel, config->blue_channel.wiper_code));
  GUARD(ad524x_set_wiper(k_light_readings_green_channel, config->green_channel.wiper_code));

  // Step 4: Cache the configuration so subsequent operations can reuse it without copying.
  g_light_config   = *config;
  g_is_initialized = true;
  return LIGHT_READINGS_OK;
}

int light_readings_sweep(LightReadingsSweepSample* sweep_out) {
  // Step 1: Validate output storage.
  GUARD_NONNULL(sweep_out);

  // Step 2: Ensure the helper has been initialised before manipulating hardware.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 3: Enter the drain state so both photodiodes share the drain path for the first measurements.
  GUARD(led_router_set_state(g_light_config.drain_state));

  // Step 4: Sample the blue photodiode while the router remains in the drain configuration.
  GUARD(adc_hal_read_single_ended(g_light_config.blue_channel.adc_channel, g_light_config.adc_timeout_us,
                                  &sweep_out->drain_blue_code));

  // Step 5: Sample the green photodiode without altering the routing state.
  GUARD(adc_hal_read_single_ended(g_light_config.green_channel.adc_channel, g_light_config.adc_timeout_us,
                                  &sweep_out->drain_green_code));

  // Step 6: Route to the blue photodiode, honour its dwell period, and capture a direct reading.
  GUARD(led_router_set_state(g_light_config.blue_channel.router_state));
  if (g_light_config.blue_channel.dwell_us > 0u) {
    delayMicroseconds(g_light_config.blue_channel.dwell_us);
  }
  GUARD(adc_hal_read_single_ended(g_light_config.blue_channel.adc_channel, g_light_config.adc_timeout_us,
                                  &sweep_out->blue_code));

  // Step 7: Route to the green photodiode, honour its dwell period, and capture a direct reading.
  GUARD(led_router_set_state(g_light_config.green_channel.router_state));
  if (g_light_config.green_channel.dwell_us > 0u) {
    delayMicroseconds(g_light_config.green_channel.dwell_us);
  }
  GUARD(adc_hal_read_single_ended(g_light_config.green_channel.adc_channel, g_light_config.adc_timeout_us,
                                  &sweep_out->green_code));

  // Step 8: Park the router in the configured drain state so callers see a consistent idle configuration.
  GUARD(led_router_set_state(g_light_config.drain_state));

  return LIGHT_READINGS_OK;
}

int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  // Step 1: Validate the output collection pointer.
  GUARD_NONNULL(results_out);

  // Step 2: Require initialisation before running a sweep sequence.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 3: Guard against callers requesting more sweeps than the fixed-capacity buffer can hold.
  if (sweep_count > LIGHT_READINGS_MAX_SWEEP_COUNT) {
    return LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED;
  }

  // Reset sweep count index
  results_out->sweep_count = 0u;

  // Step 4: Execute each sweep and store the results until all iterations complete or a dependency fails.
  for (uint32_t index = 0u; index < sweep_count; ++index) {
    GUARD(light_readings_sweep(&results_out->sweeps[index]));

    results_out->sweep_count = index + 1u;
  }

  return LIGHT_READINGS_OK;
}

int light_readings_shutdown(void) {
  // Step 1: Reject shutdown requests issued before initialisation.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 2: Leave the router in the configured drain state so other modules see a consistent baseline.
  const int return_code = led_router_set_state(g_light_config.drain_state);

  // Step 3: Clear cached state so future tests or runs start from a blank slate.
  g_light_config   = {};
  g_is_initialized = false;

  GUARD(return_code);  // Need to wait to guard to enforce that config and initialize clear first

  return LIGHT_READINGS_OK;
}

void light_readings_reset_for_test(void) {
  // Step 1: Reset cached state so repeated test cases see a cold-started module.
  g_light_config   = {};
  g_is_initialized = false;
}
