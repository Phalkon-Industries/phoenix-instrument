#include "light_readings.hpp"

#include "ad524x.hpp"
#include "phoenix_guard.hpp"
#include <Arduino.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>

static LightReadingsConfig g_light_config                   = {};
static bool                g_is_initialized                 = false;
static bool                g_last_sweep_detected_saturation = false;
static bool                g_force_saturation_for_test      = false;

static constexpr uint8_t k_light_readings_blue_channel  = 0u;
static constexpr uint8_t k_light_readings_green_channel = 1u;
// MCP3564 full-scale boundary codes as documented in datasheet DS20006204.
static constexpr int32_t k_light_readings_adc_positive_full_scale_code = 8388607;
static constexpr int32_t k_light_readings_adc_negative_full_scale_code = -8388608;

LightReadingsSweepSample g_light_readings_sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT] = {};

static bool light_readings_is_code_saturated(int32_t code) {
  return (code >= k_light_readings_adc_positive_full_scale_code) ||
         (code <= k_light_readings_adc_negative_full_scale_code);
}

struct LightReadingsRunningStats {
  uint32_t sample_count;
  double   mean;
  double   m2;
  int32_t  min_value;
  int32_t  max_value;
};

static void light_readings_running_stats_reset(LightReadingsRunningStats* stats) {
  stats->sample_count = 0u;
  stats->mean         = 0.0;
  stats->m2           = 0.0;
  stats->min_value    = INT32_MAX;
  stats->max_value    = INT32_MIN;
}

static void light_readings_running_stats_update(LightReadingsRunningStats* stats, int32_t value) {
  ++stats->sample_count;
  const double double_value = static_cast<double>(value);
  const double delta        = double_value - stats->mean;
  stats->mean += delta / static_cast<double>(stats->sample_count);
  const double delta2 = double_value - stats->mean;
  stats->m2 += delta * delta2;

  if (value < stats->min_value) {
    stats->min_value = value;
  }
  if (value > stats->max_value) {
    stats->max_value = value;
  }
}

static void light_readings_populate_summary(const LightReadingsRunningStats& running_stats,
                                            LightReadingsStatisticSummary*   summary_out) {
  summary_out->sample_count = running_stats.sample_count;
  summary_out->has_samples  = running_stats.sample_count > 0u;
  summary_out->mean         = summary_out->has_samples ? running_stats.mean : 0.0;
  summary_out->min_value    = summary_out->has_samples ? running_stats.min_value : 0;
  summary_out->max_value    = summary_out->has_samples ? running_stats.max_value : 0;

  if (running_stats.sample_count > 1u) {
    summary_out->standard_deviation = sqrt(running_stats.m2 / static_cast<double>(running_stats.sample_count - 1u));
  }
  else {
    summary_out->standard_deviation = 0.0;
  }
}

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

  bool drain_blue_saturated        = false;
  bool drain_green_saturated       = false;
  bool blue_saturated              = false;
  bool green_saturated             = false;
  g_last_sweep_detected_saturation = false;

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

  // Step 9: check saturation
  drain_blue_saturated  = light_readings_is_code_saturated(sweep_out->drain_blue_code);
  drain_green_saturated = light_readings_is_code_saturated(sweep_out->drain_green_code);
  blue_saturated        = light_readings_is_code_saturated(sweep_out->blue_code);
  green_saturated       = light_readings_is_code_saturated(sweep_out->green_code);
  // Force saturation for test if applicable
  if (g_force_saturation_for_test) {
    sweep_out->drain_blue_code  = k_light_readings_adc_positive_full_scale_code;
    sweep_out->drain_green_code = k_light_readings_adc_negative_full_scale_code;
    sweep_out->blue_code        = k_light_readings_adc_positive_full_scale_code;
    sweep_out->green_code       = k_light_readings_adc_negative_full_scale_code;
    drain_blue_saturated        = true;
    drain_green_saturated       = true;
    blue_saturated              = true;
    green_saturated             = true;
  }
  g_last_sweep_detected_saturation = drain_blue_saturated || drain_green_saturated || blue_saturated || green_saturated;

  return LIGHT_READINGS_OK;
}

int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  // Step 1: Validate the output collection pointer.
  GUARD_NONNULL(results_out);

  // Step 2: Require initialisation before running a sweep sequence.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 3: Ensure callers provide backing storage when requesting one or more sweeps.
  if ((results_out->sweeps == NULL) && (sweep_count > 0u)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 4: Guard against callers requesting more sweeps than the fixed-capacity buffer can hold.
  if (sweep_count > LIGHT_READINGS_MAX_SWEEP_COUNT) {
    return LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED;
  }

  // Step 5: Reset sweep count index.
  results_out->sweep_count = 0u;

  bool saw_saturation = false;

  // Step 6: Execute each sweep and store the results until all iterations complete or a dependency fails.
  for (uint32_t index = 0u; index < sweep_count; ++index) {
    GUARD(light_readings_sweep(&results_out->sweeps[index]));

    results_out->sweep_count = index + 1u;
    saw_saturation |= g_last_sweep_detected_saturation;
  }

  g_last_sweep_detected_saturation = saw_saturation;

  return LIGHT_READINGS_OK;
}

void light_readings_force_saturation_for_test(bool enabled) {
  g_force_saturation_for_test = enabled;
}

bool light_readings_last_sweep_detected_saturation(void) {
  return g_last_sweep_detected_saturation;
}

int light_readings_compute_sweep_stats(const LightReadingsSweepCollection* sweep_collection,
                                       LightReadingsSweepStats*            stats_out) {
  // Step 1: Validate arguments before touching output storage.
  GUARD_NONNULL(sweep_collection);
  GUARD_NONNULL(stats_out);

  if ((sweep_collection->sweeps == NULL) && (sweep_collection->sweep_count > 0u)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 2: Clear the output structure so unpopulated fields carry deterministic values.
  *stats_out             = {};
  stats_out->sweep_count = sweep_collection->sweep_count;

  // Step 3: Initialise running statistics accumulators for each sweep channel field.
  LightReadingsRunningStats drain_blue_stats;
  LightReadingsRunningStats drain_green_stats;
  LightReadingsRunningStats blue_stats;
  LightReadingsRunningStats green_stats;
  light_readings_running_stats_reset(&drain_blue_stats);
  light_readings_running_stats_reset(&drain_green_stats);
  light_readings_running_stats_reset(&blue_stats);
  light_readings_running_stats_reset(&green_stats);

  // Step 4: Fold each sweep sample into the corresponding running statistics accumulator.
  for (uint32_t index = 0u; index < sweep_collection->sweep_count; ++index) {
    const LightReadingsSweepSample& sweep = sweep_collection->sweeps[index];
    light_readings_running_stats_update(&drain_blue_stats, sweep.drain_blue_code);
    light_readings_running_stats_update(&drain_green_stats, sweep.drain_green_code);
    light_readings_running_stats_update(&blue_stats, sweep.blue_code);
    light_readings_running_stats_update(&green_stats, sweep.green_code);
  }

  // Step 5: Populate caller-visible summaries for each channel.
  light_readings_populate_summary(drain_blue_stats, &stats_out->drain_blue);
  light_readings_populate_summary(drain_green_stats, &stats_out->drain_green);
  light_readings_populate_summary(blue_stats, &stats_out->blue);
  light_readings_populate_summary(green_stats, &stats_out->green);

  return LIGHT_READINGS_OK;
}

int light_readings_shutdown(void) {
  // Step 1: Reject shutdown requests issued before initialisation.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 2: Leave the router in the configured drain state so other modules see a consistent baseline.
  const int return_code = led_router_set_state(g_light_config.drain_state);

  // Step 3: Clear cached state so future tests or runs start from a blank slate.
  g_light_config                   = {};
  g_is_initialized                 = false;
  g_last_sweep_detected_saturation = false;
  g_force_saturation_for_test      = false;

  GUARD(return_code);  // Need to wait to guard to enforce that config and initialize clear first

  return LIGHT_READINGS_OK;
}

void light_readings_reset_for_test(void) {
  // Step 1: Reset cached state so repeated test cases see a cold-started module.
  g_light_config                   = {};
  g_is_initialized                 = false;
  g_last_sweep_detected_saturation = false;
  g_force_saturation_for_test      = false;
}
