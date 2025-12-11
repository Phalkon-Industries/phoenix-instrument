#include "channel_map.hpp"

#include "ad524x.hpp"
#include "adc_hal.hpp"
#include "command_parser.hpp"
#include "led_router.hpp"
#include "light_readings.hpp"
#include "power_control.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <device_setup.hpp>
#include <limits>

static constexpr PhoenixBenchmarkChannelMapStateDescriptor
    k_state_descriptors[k_phoenix_benchmark_channel_map_state_descriptor_count] = {
        {"Drain", PhoenixBenchmarkChannel::kUnknown, true, true},
        {"Blue", PhoenixBenchmarkChannel::kChannelA, true, false},
        {"Green", PhoenixBenchmarkChannel::kChannelB, true, false},
};

static constexpr std::size_t k_accumulator_count = k_phoenix_benchmark_channel_map_state_descriptor_count;

static constexpr const char* k_error_invalid_options  = "invalid options";
static constexpr const char* k_error_hardware_failure = "hardware failure";
static constexpr const char* k_error_sampling_failure = "sampling failure";
static constexpr const char* k_error_adc_saturation   = "adc saturation";

static PhoenixBenchmarkChannelMapDefaults g_defaults             = {};
static const char*                        g_last_sample_error    = nullptr;
static PowerControlConfig                 g_power_control_config = {
                    .led_router_config     = &g_device_led_router_config,
                    .adc_config            = &g_device_adc_hal_config,
                    .wire_bus              = &Wire,
                    .digipot_address       = AD5242_I2C_ADDRESS,
                    .power_enable_pin      = PIN_ENABLE_5V_POWER,
                    .neg_bias_shutdown_pin = PIN_NEG_BIAS_SHUTDOWN,
#if defined(LED_RED)
    .indicator_red_pin = LED_RED,
#else
    .indicator_red_pin = -1,
#endif
#if defined(LED_BLUE)
    .indicator_blue_pin = LED_BLUE,
#else
    .indicator_blue_pin = -1,
#endif
};

static void emit_line(const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks, const char* message) {
  // Step 1: Forward log lines to the registered callback when both pointers are valid.
  if ((callbacks.print_line != nullptr) && (message != nullptr)) {
    callbacks.print_line(message);
  }
}

static bool ensure_hardware_ready(void) {
  // Step 1: Power the analog front-end before attempting any measurements.
  const int power_return_code = power_control_prepare_power_domains(&g_power_control_config);
  if (power_return_code != POWER_CONTROL_OK) {
    return false;
  }

  // Step 2: Park the router in the drain path so downstream helpers see a known baseline.
  const int router_return_code = led_router_set_state(LedRouterState::LED_ROUTER_STATE_DRAIN);
  return router_return_code == LED_ROUTER_OK;
}

static void reset_accumulators(PhoenixBenchmarkStateAccumulator* accumulators) {
  for (std::size_t index = 0; index < k_accumulator_count; ++index) {
    // Step 1: Clear each accumulator so a new sweep starts from a blank slate.
    accumulators[index] = PhoenixBenchmarkStateAccumulator{};
  }
}

static void populate_running_stats_from_summary(const LightReadingsStatisticSummary&   summary,
                                                PhoenixBenchmarkRunningStats<int32_t>& stats,
                                                double&                                slope_destination) {
  stats.count       = summary.sample_count;
  slope_destination = summary.drift_slope;

  if (!summary.has_samples) {
    stats.mean      = 0.0;
    stats.m2        = 0.0;
    stats.min_value = std::numeric_limits<int32_t>::max();
    stats.max_value = std::numeric_limits<int32_t>::lowest();
    return;
  }

  stats.mean = summary.mean;
  if (summary.sample_count > 1u) {
    const double variance = summary.standard_deviation * summary.standard_deviation;
    stats.m2              = variance * static_cast<double>(summary.sample_count - 1u);
  }
  else {
    stats.m2 = 0.0;
  }

  stats.min_value = summary.min_value;
  stats.max_value = summary.max_value;
}

void PhoenixBenchmarkChannelMapOptions::apply_defaults(const PhoenixBenchmarkChannelMapDefaults& defaults) {
  // Step 1: Copy the default sweep count when no override was provided.
  if (!has_sweep_override) {
    sweep_count = defaults.sweep_count;
  }
  // Step 2: Copy the default dwell time when no override was provided.
  if (!has_dwell_override) {
    dwell_us = defaults.dwell_us;
  }
  // Step 3: Copy the default wiper code when no override was provided.
  if (!has_wiper_override) {
    wiper_code = defaults.wiper_code;
  }
}

bool PhoenixBenchmarkChannelMapOptions::validate(char* error_buffer, std::size_t buffer_length) const {
  // Step 1: Detect invalid sweep counts or excessive dwell times.
  const char* message = nullptr;
  if (sweep_count == 0u) {
    message = "sweep_count must be greater than zero";
  }
  else if (dwell_us > 5000000u) {
    message = "dwell_us exceeds limit";
  }

  // Step 2: Copy the diagnostic message into the caller-provided buffer when requested.
  if ((message != nullptr) && (error_buffer != nullptr) && (buffer_length > 0u)) {
    std::strncpy(error_buffer, message, buffer_length - 1u);
    error_buffer[buffer_length - 1u] = '\0';
  }
  // Step 3: Return true only when no validation errors were discovered.
  return message == nullptr;
}

const PhoenixBenchmarkChannelMapStateDescriptor* phoenix_benchmark_channel_map_state_descriptors(void) {
  return k_state_descriptors;
}

void phoenix_benchmark_channel_map_initialise(const PhoenixBenchmarkChannelMapDefaults& defaults) {
  g_defaults = defaults;
}

bool phoenix_benchmark_channel_map_ensure_hardware_ready(void) {
  return ensure_hardware_ready();
}
//
// ============================= Main Function =============================
//

PhoenixBenchmarkChannelMapExecutionStatus phoenix_benchmark_channel_map_run(
    const PhoenixBenchmarkChannelMapOptions& input_options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks) {
  // Step 1: Reject calls that forget to provide accumulators.
  if (accumulators == nullptr) {
    emit_line(callbacks, "# channel_map,error=null_accumulator");
    return {false, PHOENIX_BENCHMARK_ERR_INVALID_ARGUMENT, k_error_invalid_options, false};
  }

  // Step 2: Copy and validate options before touching hardware.
  PhoenixBenchmarkChannelMapOptions options                = input_options;
  char                              validation_message[64] = {};
  if (!options.validate(validation_message, sizeof(validation_message))) {
    emit_line(callbacks, "# channel_map,error=invalid_options");
    return {false, PHOENIX_BENCHMARK_ERR_INVALID_ARGUMENT, k_error_invalid_options, false};
  }

  // Step 3: Power the hardware stack before attempting light readings initialisation.
  if (!ensure_hardware_ready()) {
    emit_line(callbacks, "# channel_map,error=hardware_initialisation_failed");
    return {false, PHOENIX_BENCHMARK_ERR_HARDWARE_FAILURE, k_error_hardware_failure, false};
  }

  // Step 4: Clone the device light readings configuration and apply overrides when present.
  LightReadingsConfig light_config = g_device_light_readings_config;
  if (options.has_dwell_override) {
    light_config.blue_channel.dwell_us  = options.dwell_us;
    light_config.green_channel.dwell_us = options.dwell_us;
  }
  else {
    options.dwell_us = light_config.blue_channel.dwell_us;
  }

  if (options.has_wiper_override) {
    light_config.blue_channel.wiper_code  = options.wiper_code;
    light_config.green_channel.wiper_code = options.wiper_code;
  }
  else {
    options.wiper_code = light_config.blue_channel.wiper_code;
  }

  // Step 5: Bring the light readings helper online using the configured schedule.
  bool light_readings_initialised = false;
  if (light_readings_initialize(&light_config) != LIGHT_READINGS_OK) {
    emit_line(callbacks, "# channel_map,error=light_readings_initialisation_failed");
    return {false, PHOENIX_BENCHMARK_ERR_HARDWARE_FAILURE, k_error_hardware_failure, false};
  }
  light_readings_initialised = true;

  auto shutdown_light_readings = [&]() {
    if (light_readings_initialised) {
      (void) light_readings_shutdown();
      light_readings_initialised = false;
    }
  };

  // Step 6: Reset accumulator state so the sweep starts from a blank slate.
  reset_accumulators(accumulators);
  g_last_sample_error = nullptr;

  // Step 7: Execute the requested sweep count through the light readings helper.
  LightReadingsSweepCollection sweep_collection = {
      .sweep_count = 0u,
      .sweeps      = g_light_readings_sweep_storage,
  };

  const int sweep_return_code = light_readings_sweep_n(options.sweep_count, &sweep_collection);
  if (sweep_return_code != LIGHT_READINGS_OK) {
    emit_line(callbacks, "# channel_map,error=sampling_failed");
    const char* message = (g_last_sample_error != nullptr) ? g_last_sample_error : k_error_sampling_failure;
    shutdown_light_readings();
    return {false, PHOENIX_BENCHMARK_ERR_SAMPLING_FAILURE, message, false};
  }

  // Step 8: Translate the captured samples into per-state statistics using the light readings helper.
  LightReadingsSweepStats sweep_stats       = {};
  const int               stats_return_code = light_readings_compute_sweep_stats(&sweep_collection, &sweep_stats);
  if (stats_return_code != LIGHT_READINGS_OK) {
    emit_line(callbacks, "# channel_map,error=statistics_failed");
    shutdown_light_readings();
    return {false, PHOENIX_BENCHMARK_ERR_SAMPLING_FAILURE, k_error_sampling_failure, false};
  }

  populate_running_stats_from_summary(
      sweep_stats.drain_blue, accumulators[k_phoenix_benchmark_channel_map_drain_state_index].channel_a_codes,
      accumulators[k_phoenix_benchmark_channel_map_drain_state_index].channel_a_drift_slope);
  populate_running_stats_from_summary(
      sweep_stats.drain_green, accumulators[k_phoenix_benchmark_channel_map_drain_state_index].channel_b_codes,
      accumulators[k_phoenix_benchmark_channel_map_drain_state_index].channel_b_drift_slope);

  populate_running_stats_from_summary(
      sweep_stats.blue, accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 1u].channel_a_codes,
      accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 1u].channel_a_drift_slope);
  populate_running_stats_from_summary(
      sweep_stats.drain_green, accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 1u].channel_b_codes,
      accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 1u].channel_b_drift_slope);

  populate_running_stats_from_summary(
      sweep_stats.drain_blue, accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 2u].channel_a_codes,
      accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 2u].channel_a_drift_slope);
  populate_running_stats_from_summary(
      sweep_stats.green, accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 2u].channel_b_codes,
      accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 2u].channel_b_drift_slope);

  // Step 9: Determine whether any saturation was detected during the sweep.
  bool run_has_warnings = light_readings_last_sweep_detected_saturation();

  g_last_sample_error = run_has_warnings ? k_error_adc_saturation : nullptr;

  shutdown_light_readings();
  return {true, PHOENIX_BENCHMARK_OK, nullptr, run_has_warnings};
}

PhoenixBenchmarkChannelMapParseResult phoenix_benchmark_channel_map_parse_command(const char* line) {
  // Step 1: Seed options with defaults so unspecified values inherit the baseline configuration.
  PhoenixBenchmarkChannelMapOptions options = {};
  options.apply_defaults(g_defaults);

  // Step 2: Parse the incoming command using the shared command-line handler.
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, "channel_map");
  if (!outcome.success) {
    return {false, options, outcome.error_message};
  }

  // Step 3: Apply overrides reported by the parser outcome structure.
  if (outcome.arguments.has_sweep_override) {
    options.sweep_count        = outcome.arguments.sweep_count;
    options.has_sweep_override = true;
  }
  if (outcome.arguments.has_dwell_override) {
    options.dwell_us           = outcome.arguments.dwell_us;
    options.has_dwell_override = true;
  }
  if (outcome.arguments.has_wiper_override) {
    options.wiper_code         = outcome.arguments.wiper_code;
    options.has_wiper_override = true;
  }

  // Step 4: Reapply defaults for any fields still unset before returning success.
  options.apply_defaults(g_defaults);
  return {true, options, nullptr};
}

void phoenix_benchmark_channel_map_reset_state(void) {
  // Step 1: Clear persistent defaults and cached hardware status.
  g_defaults          = PhoenixBenchmarkChannelMapDefaults{};
  g_last_sample_error = nullptr;
  power_control_reset_for_test();
  light_readings_reset_for_test();
  light_readings_force_saturation_for_test(false);
  led_router_reset_for_test();
  adc_hal_reset_for_test();
  ad524x_deinitialize();
}
