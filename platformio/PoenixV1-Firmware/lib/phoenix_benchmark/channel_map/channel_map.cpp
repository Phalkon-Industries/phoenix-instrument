#include "channel_map.hpp"

#include "ad524x.hpp"
#include "adc_hal.hpp"
#include "command_parser.hpp"
#include "led_router.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

static constexpr uint32_t k_adc_timeout_us     = 1000000u;
static constexpr uint32_t k_spi_clock_hz       = 500000UL;
static constexpr uint8_t  k_digipot_channels[] = {0u, 1u};

#ifdef AD5242_I2C_ADDRESS
static_assert(AD5242_I2C_ADDRESS == 0x2Cu, "AD5242 address changed; update channel_map constants");
#endif
static constexpr uint8_t k_ad524x_address = 0x2Cu;

#ifdef PIN_ADC_CS
static_assert(PIN_ADC_CS == 13, "ADC CS pin changed; update channel_map constants");
#endif
static constexpr int k_pin_adc_cs = 13;

#ifdef PIN_ENABLE_POWER
static_assert(PIN_ENABLE_POWER == 12, "Power enable pin changed; update channel_map constants");
#endif
static constexpr int k_pin_enable_power = 12;

#ifdef TS5A3359_IN1
static_assert(TS5A3359_IN1 == 17, "Switch IN1 pin changed; update channel_map constants");
#endif
static constexpr int k_switch_in1_pin = 17;

#ifdef TS5A3359_IN2
static_assert(TS5A3359_IN2 == 18, "Switch IN2 pin changed; update channel_map constants");
#endif
static constexpr int         k_switch_in2_pin        = 18;
static constexpr std::size_t k_digipot_channel_count = sizeof(k_digipot_channels) / sizeof(k_digipot_channels[0]);
static constexpr std::size_t k_max_sample_attempts   = 3u;

static constexpr PhoenixBenchmarkChannelMapStateDescriptor
    k_state_descriptors[k_phoenix_benchmark_channel_map_state_descriptor_count] = {
        {"Drain", PhoenixBenchmarkChannel::kUnknown, true, true},
        {"LED1", PhoenixBenchmarkChannel::kChannelA, true, false},
        {"LED2", PhoenixBenchmarkChannel::kChannelB, true, false},
};

struct PhoenixBenchmarkChannelMapStateRequest {
  LedRouterState router_state;
  std::size_t    accumulator_index;
};

static constexpr PhoenixBenchmarkChannelMapStateRequest k_state_sequence[] = {
    {LedRouterState::LED_ROUTER_STATE_DRAIN, 0u},
    {LedRouterState::LED_ROUTER_STATE_LED1, 1u},
    {LedRouterState::LED_ROUTER_STATE_LED2, 2u},
};

static_assert(k_phoenix_benchmark_channel_map_state_descriptor_count ==
                  (sizeof(k_state_sequence) / sizeof(k_state_sequence[0])),
              "State sequence and descriptor tables must remain aligned");

static constexpr std::size_t k_accumulator_count = k_phoenix_benchmark_channel_map_state_descriptor_count;

static constexpr const char* k_error_invalid_options  = "invalid options";
static constexpr const char* k_error_hardware_failure = "hardware failure";
static constexpr const char* k_error_adc_saturation   = "adc saturation";

static PhoenixBenchmarkChannelMapDefaults g_defaults                  = {};
static bool                               g_power_enabled             = false;
static const char*                        g_last_sample_error         = nullptr;
static bool                               g_force_saturation_for_test = false;
static constexpr int32_t  k_positive_full_scale_test_code = k_phoenix_benchmark_adc_positive_full_scale_code;
static constexpr int32_t  k_negative_full_scale_test_code = k_phoenix_benchmark_adc_negative_full_scale_code;
static const AdcHalConfig k_adc_config                    = {
                       .chip_select_pin = k_pin_adc_cs,
                       .spi_clock_hz    = k_spi_clock_hz,
                       .default_gain    = AdcHalGain::ADC_HAL_GAIN_1,
};

static void enable_power_domains(void) {
  if (g_power_enabled) {
    return;
  }
  // Step 1: Drive the shared power rail so downstream peripherals receive bias power.
  pinMode(k_pin_enable_power, OUTPUT);
  digitalWrite(k_pin_enable_power, HIGH);
  g_power_enabled = true;
}

static void configure_led_idle(void) {
  static bool configured = false;
  if (configured) {
    return;
  }
  // Step 1: Force the status LEDs to a benign idle so measurement states start predictable.
#ifdef LED_RED
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, LOW);
#endif
#ifdef LED_BLUE
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);
#endif
  configured = true;
}

static bool ensure_led_router_initialised(void) {
  static bool initialised = false;
  if (initialised) {
    return true;
  }

  // Step 1: Provide the hardware pin map so the router knows how to steer channels.
  const LedRouterConfig config = {
      .switch_in1_pin = k_switch_in1_pin,
      .switch_in2_pin = k_switch_in2_pin,
  };

  // Step 2: Initialize the router driver before we attempt to move any switches.
  const int return_code = led_router_initialize(&config);
  if (return_code != LED_ROUTER_OK) {
    return false;
  }

  initialised = true;
  return true;
}

static bool ensure_digipot_initialised(void) {
  if (!ad524x_is_initialized()) {
    // Step 1: Start the I2C bus so the digi-pot can receive commands.
    Wire.begin();
    // Step 2: Initialize the digi-pot at the expected address before setting wipers.
    const int init_code = ad524x_initialize(k_ad524x_address, &Wire);
    if (init_code != AD524X_OK) {
      return false;
    }
  }
  return true;
}

static bool ensure_adc_initialised(void) {
  static bool initialised = false;
  if (initialised) {
    return true;
  }

  // Step 1: Bring up the ADC HAL so we can schedule conversions.
  int return_code = adc_hal_initialize(&k_adc_config);
  if (return_code != ADC_HAL_OK) {
    return false;
  }

  // Step 2: Apply the default register configuration expected by downstream math.
  return_code = adc_hal_apply_default_configuration();
  if (return_code != ADC_HAL_OK) {
    return false;
  }

  initialised = true;
  return true;
}

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

static bool select_led_state(LedRouterState state) {
  // Step 1: Command the router to the requested LED path before sampling.
  const int return_code = led_router_set_state(state);
  return return_code == LED_ROUTER_OK;
}

static bool read_adc_channel(AdcHalChannel channel, int32_t* out_code) {
  // Step 1: Request a single-ended conversion and capture the resulting code.
  const int return_code = adc_hal_read_single_ended(channel, k_adc_timeout_us, out_code);
  // Step 2: Report success only when the HAL confirms the read completed.
  return return_code == ADC_HAL_OK;
}

static bool sample_state(const PhoenixBenchmarkChannelMapStateRequest& request, uint32_t dwell_us,
                         PhoenixBenchmarkStateAccumulator* accumulators, bool* out_saturation_detected,
                         bool* out_sample_captured) {
  g_last_sample_error = nullptr;

  // Step 1: Reset caller-observed flags so each measurement reports fresh state.
  if (out_saturation_detected != nullptr) {
    *out_saturation_detected = false;
  }
  if (out_sample_captured != nullptr) {
    *out_sample_captured = false;
  }

  // Step 2: Move the router to the requested LED channel before any dwell.
  if (!select_led_state(request.router_state)) {
    g_last_sample_error = "led router state change failed";
    return false;
  }

  // Step 3: Allow the analog path to settle so the ADC sees a stable signal.
  if (dwell_us > 0u) {
    delayMicroseconds(dwell_us);
  }

  // Step 4: Grab the accumulator for this state so we can store statistics.
  PhoenixBenchmarkStateAccumulator& accumulator = accumulators[request.accumulator_index];

  // Step 5: Attempt to capture both ADC channels, retrying on saturation when needed.
  for (std::size_t attempt = 0u; attempt < k_max_sample_attempts; ++attempt) {
    int32_t channel_a_code = 0;
    // Step 5a: Sample channel A and bail if the ADC reports a failure.
    if (!read_adc_channel(AdcHalChannel::ADC_HAL_CHANNEL_4, &channel_a_code)) {
      g_last_sample_error = "adc read failed (channel A)";
      return false;
    }

    int32_t channel_b_code = 0;
    // Step 5b: Sample channel B to capture the complementary diode path.
    if (!read_adc_channel(AdcHalChannel::ADC_HAL_CHANNEL_5, &channel_b_code)) {
      g_last_sample_error = "adc read failed (channel B)";
      return false;
    }

    // Step 5c: Inject synthetic saturation when tests request it.
    if (g_force_saturation_for_test) {
      channel_a_code = k_positive_full_scale_test_code;
      channel_b_code = k_negative_full_scale_test_code;
    }

    const bool a_saturated = phoenix_benchmark_is_adc_code_saturated(channel_a_code);
    const bool b_saturated = phoenix_benchmark_is_adc_code_saturated(channel_b_code);
    if (a_saturated || b_saturated) {
      // Step 5d: Flag saturation, retry if attempts remain, and record the raw codes.
      g_last_sample_error = k_error_adc_saturation;
      if ((attempt + 1u) < k_max_sample_attempts) {
        delayMicroseconds(50u);
        continue;
      }

      accumulator.channel_a_codes.update(channel_a_code);
      accumulator.channel_b_codes.update(channel_b_code);
      // Step 5e: Mark that we captured a sample even though it saturated.
      if (out_sample_captured != nullptr) {
        *out_sample_captured = true;
      }
      if (a_saturated) {
        ++accumulator.channel_a_saturation_count;
      }
      if (b_saturated) {
        ++accumulator.channel_b_saturation_count;
      }
      if (out_saturation_detected != nullptr) {
        *out_saturation_detected = true;
      }
      return true;
    }

    accumulator.channel_a_codes.update(channel_a_code);
    accumulator.channel_b_codes.update(channel_b_code);
    // Step 5f: Record the clean sample and clear saturation flags.
    if (out_sample_captured != nullptr) {
      *out_sample_captured = true;
    }
    if (out_saturation_detected != nullptr) {
      *out_saturation_detected = false;
    }
    return true;
  }

  if (out_saturation_detected != nullptr) {
    *out_saturation_detected = false;
  }
  // Step 6: Report success even if retries exhausted, since the accumulator was updated.
  return true;
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

  // Step 3: Power the shared domains and park indicators in a neutral state.
  enable_power_domains();
  configure_led_idle();

  // Step 4: Bring up every peripheral before we start the sweep.
  if (!ensure_digipot_initialised() || !ensure_adc_initialised() || !ensure_led_router_initialised()) {
    emit_line(callbacks, "# channel_map,error=hardware_initialisation_failed");
    return {false, PHOENIX_BENCHMARK_ERR_UNIMPLEMENTED, k_error_hardware_failure, false};
  }

  // Step 5: Default the router to drain so the array discharges between steps.
  (void) select_led_state(LedRouterState::LED_ROUTER_STATE_DRAIN);

  // Step 6: Reset the per-state statistics before recording measurements.
  reset_accumulators(accumulators);

  // Step 7: Apply the requested intensity so every sweep uses the same starting point.
  if (!apply_wiper_code(options.wiper_code)) {
    emit_line(callbacks, "# channel_map,error=ad524x_failure");
    return {false, PHOENIX_BENCHMARK_ERR_UNIMPLEMENTED, k_error_hardware_failure, false};
  }

  // Step 8: Track whether any saturation occurs so the caller can surface warnings.
  bool run_has_warnings = false;

  // Step 9: Perform the requested number of sweeps across each LED state.
  for (uint32_t sweep_index = 0u; sweep_index < options.sweep_count; ++sweep_index) {
    // Step 9a: Walk the ordered state sequence for this sweep.
    for (std::size_t state_index = 0u; state_index < (sizeof(k_state_sequence) / sizeof(k_state_sequence[0]));
         ++state_index) {
      const PhoenixBenchmarkChannelMapStateRequest& request        = k_state_sequence[state_index];
      bool                                          saw_saturation = false;
      // Step 9b: Sample the current state and capture whether saturation occurred.
      if (!sample_state(request, options.dwell_us, accumulators, &saw_saturation, nullptr)) {
        emit_line(callbacks, "# channel_map,error=sampling_failed");
        (void) select_led_state(LedRouterState::LED_ROUTER_STATE_DRAIN);
        const char* message = (g_last_sample_error != nullptr) ? g_last_sample_error : k_error_hardware_failure;
        return {false, PHOENIX_BENCHMARK_ERR_UNIMPLEMENTED, message, run_has_warnings};
      }

      if (saw_saturation) {
        // Step 9c: Elevate the warning flag so summaries can highlight saturation.
        run_has_warnings = true;
      }
    }
  }

  // Step 10: Park the router in drain when the sweep completes.
  (void) select_led_state(LedRouterState::LED_ROUTER_STATE_DRAIN);
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
  g_defaults                  = PhoenixBenchmarkChannelMapDefaults{};
  g_power_enabled             = false;
  g_last_sample_error         = nullptr;
  g_force_saturation_for_test = false;
}

void phoenix_benchmark_channel_map_set_force_saturation_for_test(bool enabled) {
  // Step 1: Toggle the synthetic saturation flag so tests can manipulate ADC codes.
  g_force_saturation_for_test = enabled;
}
