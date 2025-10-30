#include "light_readings.hpp"

#include <stddef.h>

static LightReadingsConfig g_cached_config  = {};
static bool                g_is_initialized = false;

static int light_readings_route_and_sample(LedRouterState state, AdcHalChannel channel, uint32_t timeout_us,
                                           int32_t* code_out) {
  // Step 1: Route the LED mux so the requested photodiode connects to the ADC path.
  int return_code = led_router_set_state(state);
  if (return_code != LED_ROUTER_OK) {
    return return_code;
  }

  // Step 2: Capture the ADC sample using the configured timeout.
  return_code = adc_hal_read_single_ended(channel, timeout_us, code_out);
  if (return_code != ADC_HAL_OK) {
    return return_code;
  }

  return LIGHT_READINGS_OK;
}

static int light_readings_restore_drain(void) {
  // Step 1: Park the router in the drain state so callers see a consistent idle configuration.
  int return_code = led_router_set_state(g_cached_config.drain_state);
  if (return_code != LED_ROUTER_OK) {
    return return_code;
  }

  return LIGHT_READINGS_OK;
}

int light_readings_initialize(const LightReadingsConfig* config) {
  // Step 1: Validate the configuration pointer before touching shared state.
  if (config == NULL) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 2: Park the router in the configured drain state so readings start from a known baseline.
  int return_code = led_router_set_state(config->drain_state);
  if (return_code != LED_ROUTER_OK) {
    return return_code;
  }

  // Step 3: Cache the configuration so subsequent operations can reuse it without copying.
  g_cached_config  = *config;
  g_is_initialized = true;
  return LIGHT_READINGS_OK;
}

int light_readings_read_channel(LightReadingsChannel channel, int32_t* code_out) {
  // Step 1: Validate output storage supplied by the caller.
  if (code_out == NULL) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 2: Ensure the helper has been initialised before touching hardware.
  if (!g_is_initialized) {
    return LIGHT_READINGS_ERR_NOT_INITIALIZED;
  }

  // Step 3: Map the logical channel to the configured router state and ADC input.
  LedRouterState target_state;
  AdcHalChannel  target_channel;
  switch (channel) {
    case LightReadingsChannel::LIGHT_READINGS_CHANNEL_A:
      target_state   = g_cached_config.channel_a_state;
      target_channel = g_cached_config.channel_a_adc;
      break;
    case LightReadingsChannel::LIGHT_READINGS_CHANNEL_B:
      target_state   = g_cached_config.channel_b_state;
      target_channel = g_cached_config.channel_b_adc;
      break;
    default:
      return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 4: Drive the router to the requested channel and capture a sample.
  const int sample_return_code =
      light_readings_route_and_sample(target_state, target_channel, g_cached_config.adc_timeout_us, code_out);

  // Step 5: Park the router in the configured drain state before returning to the caller.
  const int drain_return_code = light_readings_restore_drain();

  if (sample_return_code != LIGHT_READINGS_OK) {
    return sample_return_code;
  }

  if (drain_return_code != LIGHT_READINGS_OK) {
    return drain_return_code;
  }

  return LIGHT_READINGS_OK;
}

int light_readings_sweep(LightReadingsSweepSample* sweep_out) {
  // Step 1: Validate output storage.
  if (sweep_out == NULL) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 2: Ensure the helper has been initialised before manipulating hardware.
  if (!g_is_initialized) {
    return LIGHT_READINGS_ERR_NOT_INITIALIZED;
  }

  // Step 3: Enter the drain state so both photodiodes share the drain path for the first measurements.
  int return_code = led_router_set_state(g_cached_config.drain_state);
  if (return_code != LED_ROUTER_OK) {
    return return_code;
  }

  // Step 4: Sample photodiode A while the router remains in the drain configuration.
  return_code = adc_hal_read_single_ended(g_cached_config.channel_a_adc, g_cached_config.adc_timeout_us,
                                          &sweep_out->drain_channel_a_code);
  if (return_code != ADC_HAL_OK) {
    return return_code;
  }

  // Step 5: Sample photodiode B without altering the routing state.
  return_code = adc_hal_read_single_ended(g_cached_config.channel_b_adc, g_cached_config.adc_timeout_us,
                                          &sweep_out->drain_channel_b_code);
  if (return_code != ADC_HAL_OK) {
    return return_code;
  }

  // Step 6: Capture the dedicated channel A reading using the helper API so drain parking stays consistent.
  return_code = light_readings_read_channel(LightReadingsChannel::LIGHT_READINGS_CHANNEL_A, &sweep_out->channel_a_code);
  if (return_code != LIGHT_READINGS_OK) {
    return return_code;
  }

  // Step 7: Capture the dedicated channel B reading and leave the router parked in the drain state.
  return_code = light_readings_read_channel(LightReadingsChannel::LIGHT_READINGS_CHANNEL_B, &sweep_out->channel_b_code);
  if (return_code != LIGHT_READINGS_OK) {
    return return_code;
  }

  return LIGHT_READINGS_OK;
}

int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  // Step 1: Validate the output collection pointer.
  if (results_out == NULL) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 2: Require initialisation before running a sweep sequence.
  if (!g_is_initialized) {
    return LIGHT_READINGS_ERR_NOT_INITIALIZED;
  }

  // Step 3: Guard against callers requesting more sweeps than the fixed-capacity buffer can hold.
  if (sweep_count > LIGHT_READINGS_MAX_SWEEP_COUNT) {
    return LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED;
  }

  results_out->sweep_count = 0u;

  // Step 4: Execute each sweep and store the results until all iterations complete or a dependency fails.
  for (uint32_t index = 0u; index < sweep_count; ++index) {
    int return_code = light_readings_sweep(&results_out->sweeps[index]);
    if (return_code != LIGHT_READINGS_OK) {
      return return_code;
    }

    results_out->sweep_count = index + 1u;
  }

  return LIGHT_READINGS_OK;
}

int light_readings_shutdown(void) {
  // Step 1: Reject shutdown requests issued before initialisation.
  if (!g_is_initialized) {
    return LIGHT_READINGS_ERR_NOT_INITIALIZED;
  }

  // Step 2: Leave the router in the configured drain state so other modules see a consistent baseline.
  int return_code = light_readings_restore_drain();

  // Step 3: Clear cached state so future tests or runs start from a blank slate.
  g_cached_config  = {};
  g_is_initialized = false;

  if (return_code != LIGHT_READINGS_OK) {
    return return_code;
  }

  return LIGHT_READINGS_OK;
}

void light_readings_reset_for_test(void) {
  // Step 1: Reset cached state so repeated test cases see a cold-started module.
  g_cached_config  = {};
  g_is_initialized = false;
}
