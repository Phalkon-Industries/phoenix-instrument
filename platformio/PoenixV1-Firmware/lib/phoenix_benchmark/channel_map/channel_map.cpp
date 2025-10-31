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

static constexpr uint8_t     k_digipot_channels[]    = {0u, 1u};
static constexpr std::size_t k_digipot_channel_count = sizeof(k_digipot_channels) / sizeof(k_digipot_channels[0]);

static constexpr PhoenixBenchmarkChannelMapStateDescriptor
    k_state_descriptors[k_phoenix_benchmark_channel_map_state_descriptor_count] = {
        {"Drain", PhoenixBenchmarkChannel::kUnknown, true, true},
        {"LED1", PhoenixBenchmarkChannel::kChannelA, true, false},
        {"LED2", PhoenixBenchmarkChannel::kChannelB, true, false},
};

static constexpr std::size_t k_accumulator_count = k_phoenix_benchmark_channel_map_state_descriptor_count;

static constexpr const char* k_error_invalid_options  = "invalid options";
static constexpr const char* k_error_hardware_failure = "hardware failure";
static constexpr const char* k_error_sampling_failure = "sampling failure";
static constexpr const char* k_error_adc_saturation   = "adc saturation";

static PhoenixBenchmarkChannelMapDefaults g_defaults             = {};
static const char*                        g_last_sample_error    = nullptr;
static PowerControlConfig                 g_power_control_config = {
                    .led_router_config = &g_device_led_router_config,
                    .adc_config        = &g_device_adc_hal_config,
                    .wire_bus          = &Wire,
                    .digipot_address   = AD5242_I2C_ADDRESS,
                    .power_enable_pin  = PIN_ENABLE_POWER,
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

static bool apply_wiper_code(uint8_t wiper_code) {
  for (std::size_t index = 0; index < k_digipot_channel_count; ++index) {
    // Step 1: Program each digi-pot channel so brightness adjustments stay consistent across both LEDs.
    const int return_code = ad524x_set_wiper(k_digipot_channels[index], wiper_code);
    if (return_code != AD524X_OK) {
      return false;
    }
  }
  return true;
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

static void accumulate_channel_measurement(PhoenixBenchmarkStateAccumulator& accumulator, bool for_channel_a,
                                           int32_t code) {
  // Step 1: Update running statistics for the selected channel with the latest ADC code.
  if (for_channel_a) {
    accumulator.channel_a_codes.update(code);
  }
  else {
    accumulator.channel_b_codes.update(code);
  }

  // Step 2: Track saturation metadata so summaries can report warnings.
  if (phoenix_benchmark_is_adc_code_saturated(code)) {
    if (for_channel_a) {
      ++accumulator.channel_a_saturation_count;
    }
    else {
      ++accumulator.channel_b_saturation_count;
    }
  }
}

static void populate_accumulators_from_sweeps(const LightReadingsSweepCollection& collection,
                                              PhoenixBenchmarkStateAccumulator*   accumulators) {
  // Step 1: Fold each sweep sample into the drain, LED1, and LED2 accumulators.
  for (uint32_t index = 0u; index < collection.sweep_count; ++index) {
    const LightReadingsSweepSample& sample = collection.sweeps[index];

    accumulate_channel_measurement(accumulators[k_phoenix_benchmark_channel_map_drain_state_index], true,
                                   sample.drain_blue_code);
    accumulate_channel_measurement(accumulators[k_phoenix_benchmark_channel_map_drain_state_index], false,
                                   sample.drain_green_code);

    accumulate_channel_measurement(accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 1u], true,
                                   sample.blue_code);
    // Step 2: Reuse the drain value for the non-routed channel so statistics still capture baseline behaviour.
    accumulate_channel_measurement(accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 1u], false,
                                   sample.drain_green_code);

    // Step 3: Mirror the approach for the LED2 path, keeping channel A anchored to the drain baseline.
    accumulate_channel_measurement(accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 2u], true,
                                   sample.drain_blue_code);
    accumulate_channel_measurement(accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 2u], false,
                                   sample.green_code);
  }
}

static void reset_accumulators(PhoenixBenchmarkStateAccumulator* accumulators) {
  for (std::size_t index = 0; index < k_accumulator_count; ++index) {
    // Step 1: Clear each accumulator so a new sweep starts from a blank slate.
    accumulators[index] = PhoenixBenchmarkStateAccumulator{};
  }
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

  // Step 4: Clone the device light readings configuration so dwell overrides can be applied per run.
  LightReadingsConfig light_config    = g_device_light_readings_config;
  light_config.blue_channel.dwell_us  = options.dwell_us;
  light_config.green_channel.dwell_us = options.dwell_us;

  // Step 5: Bring the light readings helper online using the requested dwell schedule.
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

  // Step 7: Apply the requested digipot wiper override before sampling.
  if (!apply_wiper_code(options.wiper_code)) {
    emit_line(callbacks, "# channel_map,error=ad524x_failure");
    shutdown_light_readings();
    return {false, PHOENIX_BENCHMARK_ERR_HARDWARE_FAILURE, k_error_hardware_failure, false};
  }

  // Step 8: Execute the requested sweep count through the light readings helper.
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

  // Step 9: Translate the captured samples into per-state statistics.
  populate_accumulators_from_sweeps(sweep_collection, accumulators);

  // Step 10: Determine whether any saturation was detected during the sweep.
  bool run_has_warnings = light_readings_last_sweep_detected_saturation();
  if (!run_has_warnings) {
    for (std::size_t index = 0u; index < k_accumulator_count; ++index) {
      const PhoenixBenchmarkStateAccumulator& accumulator = accumulators[index];
      if ((accumulator.channel_a_saturation_count > 0u) || (accumulator.channel_b_saturation_count > 0u)) {
        run_has_warnings = true;
        break;
      }
    }
  }

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
