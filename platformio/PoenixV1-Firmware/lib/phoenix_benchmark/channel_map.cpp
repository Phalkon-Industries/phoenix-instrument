#include "channel_map.hpp"

#include "ad524x.hpp"
#include "adc_hal.hpp"
#include "command_parser.hpp"
#include "led_router.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace phoenix_benchmark {

namespace {

constexpr uint32_t k_adc_timeout_us     = 1000000u;
constexpr uint32_t k_spi_clock_hz       = 500000UL;
constexpr uint8_t  k_digipot_channels[] = {0u, 1u};

#ifdef AD5242_I2C_ADDRESS
static_assert(AD5242_I2C_ADDRESS == 0x2Cu, "AD5242 address changed; update channel_map constants");
#endif
constexpr uint8_t k_ad524x_address = 0x2Cu;

#ifdef PIN_ADC_CS
static_assert(PIN_ADC_CS == 13, "ADC CS pin changed; update channel_map constants");
#endif
constexpr int k_pin_adc_cs = 13;

#ifdef PIN_ENABLE_POWER
static_assert(PIN_ENABLE_POWER == 12, "Power enable pin changed; update channel_map constants");
#endif
constexpr int k_pin_enable_power = 12;

#ifdef TS5A3359_IN1
static_assert(TS5A3359_IN1 == 17, "Switch IN1 pin changed; update channel_map constants");
#endif
constexpr int k_switch_in1_pin = 17;

#ifdef TS5A3359_IN2
static_assert(TS5A3359_IN2 == 18, "Switch IN2 pin changed; update channel_map constants");
#endif
constexpr int    k_switch_in2_pin        = 18;
constexpr size_t k_digipot_channel_count = sizeof(k_digipot_channels) / sizeof(k_digipot_channels[0]);
constexpr size_t k_max_sample_attempts   = 3u;

constexpr ChannelMapStateDescriptor k_state_descriptors[channel_map::k_state_descriptor_count] = {
    {"Drain", phoenix_benchmark_support::BenchmarkChannel::kUnknown, true, true},
    {"LED1", phoenix_benchmark_support::BenchmarkChannel::kChannelA, true, false},
    {"LED2", phoenix_benchmark_support::BenchmarkChannel::kChannelB, true, false},
};

struct StateRequest {
  LedRouterState router_state;
  size_t         accumulator_index;
};

constexpr StateRequest k_state_sequence[] = {
    {LedRouterState::LED_ROUTER_STATE_DRAIN, 0u},
    {LedRouterState::LED_ROUTER_STATE_LED1, 1u},
    {LedRouterState::LED_ROUTER_STATE_LED2, 2u},
};

static_assert(channel_map::k_state_descriptor_count == (sizeof(k_state_sequence) / sizeof(k_state_sequence[0])),
              "State sequence and descriptor tables must remain aligned");

constexpr size_t k_accumulator_count = channel_map::k_state_descriptor_count;

constexpr const char* k_error_invalid_options  = "invalid options";
constexpr const char* k_error_hardware_failure = "hardware failure";
constexpr const char* k_error_adc_saturation   = "adc saturation";

ChannelMapDefaults g_defaults          = {};
bool               g_power_enabled     = false;
const char*        g_last_sample_error = nullptr;

const AdcHalConfig k_adc_config = {
    .chip_select_pin = k_pin_adc_cs,
    .spi_clock_hz    = k_spi_clock_hz,
    .default_gain    = AdcHalGain::ADC_HAL_GAIN_1,
};

void enable_power_domains(void) {
  if (g_power_enabled) {
    return;
  }
  pinMode(k_pin_enable_power, OUTPUT);
  digitalWrite(k_pin_enable_power, HIGH);
  g_power_enabled = true;
}

void configure_led_idle(void) {
  static bool configured = false;
  if (configured) {
    return;
  }
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

bool ensure_led_router_initialised(void) {
  static bool initialised = false;
  if (initialised) {
    return true;
  }

  const LedRouterConfig config = {
      .switch_in1_pin = k_switch_in1_pin,
      .switch_in2_pin = k_switch_in2_pin,
  };

  const int return_code = led_router_initialize(&config);
  if (return_code != LED_ROUTER_OK) {
    return false;
  }

  initialised = true;
  return true;
}

bool ensure_digipot_initialised(void) {
  if (!ad524x_is_initialized()) {
    Wire.begin();
    const int init_code = ad524x_initialize(k_ad524x_address, &Wire);
    if (init_code != AD524X_OK) {
      return false;
    }
  }
  return true;
}

bool ensure_adc_initialised(void) {
  static bool initialised = false;
  if (initialised) {
    return true;
  }

  int return_code = adc_hal_initialize(&k_adc_config);
  if (return_code != ADC_HAL_OK) {
    return false;
  }

  return_code = adc_hal_apply_default_configuration();
  if (return_code != ADC_HAL_OK) {
    return false;
  }

  initialised = true;
  return true;
}

void emit_line(const OutputCallbacks& callbacks, const char* message) {
  if (callbacks.print_line != nullptr && message != nullptr) {
    callbacks.print_line(message);
  }
}

bool apply_wiper_code(uint8_t wiper_code) {
  for (size_t index = 0; index < k_digipot_channel_count; ++index) {
    const int return_code = ad524x_set_wiper(k_digipot_channels[index], wiper_code);
    if (return_code != AD524X_OK) {
      return false;
    }
  }
  return true;
}

bool select_led_state(LedRouterState state) {
  const int return_code = led_router_set_state(state);
  return return_code == LED_ROUTER_OK;
}

bool read_adc_channel(AdcHalChannel channel, int32_t* out_code) {
  const int return_code = adc_hal_read_single_ended(channel, k_adc_timeout_us, out_code);
  return return_code == ADC_HAL_OK;
}

bool sample_state(const StateRequest& request, uint32_t dwell_us,
                  phoenix_benchmark_support::StateAccumulator* accumulators) {
  g_last_sample_error = nullptr;

  if (!select_led_state(request.router_state)) {
    g_last_sample_error = "led router state change failed";
    return false;
  }

  if (dwell_us > 0u) {
    delayMicroseconds(dwell_us);
  }

  for (size_t attempt = 0u; attempt < k_max_sample_attempts; ++attempt) {
    int32_t channel_a_code = 0;
    if (!read_adc_channel(AdcHalChannel::ADC_HAL_CHANNEL_4, &channel_a_code)) {
      g_last_sample_error = "adc read failed (channel A)";
      return false;
    }

    int32_t channel_b_code = 0;
    if (!read_adc_channel(AdcHalChannel::ADC_HAL_CHANNEL_5, &channel_b_code)) {
      g_last_sample_error = "adc read failed (channel B)";
      return false;
    }

    const bool a_saturated = phoenix_benchmark_support::is_adc_code_saturated(channel_a_code);
    const bool b_saturated = phoenix_benchmark_support::is_adc_code_saturated(channel_b_code);
    if (a_saturated || b_saturated) {
      g_last_sample_error = k_error_adc_saturation;
      if ((attempt + 1u) < k_max_sample_attempts) {
        delayMicroseconds(50u);
        continue;
      }
    }

    phoenix_benchmark_support::StateAccumulator& accumulator = accumulators[request.accumulator_index];
    accumulator.channel_a_codes.update(channel_a_code);
    accumulator.channel_b_codes.update(channel_b_code);
    return true;
  }

  return false;
}

void reset_accumulators(phoenix_benchmark_support::StateAccumulator* accumulators) {
  for (size_t index = 0; index < k_accumulator_count; ++index) {
    accumulators[index] = phoenix_benchmark_support::StateAccumulator{};
  }
}

}  // namespace

void ChannelMapOptions::apply_defaults(const ChannelMapDefaults& defaults) {
  if (!has_sweep_override) {
    sweep_count = defaults.sweep_count;
  }
  if (!has_dwell_override) {
    dwell_us = defaults.dwell_us;
  }
  if (!has_wiper_override) {
    wiper_code = defaults.wiper_code;
  }
}

bool ChannelMapOptions::validate(char* error_buffer, size_t buffer_length) const {
  const char* message = nullptr;
  if (sweep_count == 0u) {
    message = "sweep_count must be greater than zero";
  }
  else if (dwell_us > 5000000u) {
    // Guard extreme dwell values that would stall the benchmark beyond reasonable limits.
    message = "dwell_us exceeds limit";
  }

  if (message != nullptr && error_buffer != nullptr && buffer_length > 0u) {
    strncpy(error_buffer, message, buffer_length - 1u);
    error_buffer[buffer_length - 1u] = '\0';
  }
  return message == nullptr;
}

namespace channel_map {

void initialise(const ChannelMapDefaults& defaults) {
  g_defaults = defaults;
}

ExecutionStatus run(const ChannelMapOptions& input_options, phoenix_benchmark_support::StateAccumulator* accumulators,
                    const OutputCallbacks& callbacks) {
  if (accumulators == nullptr) {
    emit_line(callbacks, "# channel_map,error=null_accumulator");
    return ExecutionStatus{false, PHOENIX_BENCHMARK_ERR_INVALID_ARGUMENT, k_error_invalid_options};
  }

  ChannelMapOptions options                = input_options;
  char              validation_message[64] = {};
  if (!options.validate(validation_message, sizeof(validation_message))) {
    emit_line(callbacks, "# channel_map,error=invalid_options");
    return ExecutionStatus{false, PHOENIX_BENCHMARK_ERR_INVALID_ARGUMENT, k_error_invalid_options};
  }

  enable_power_domains();
  configure_led_idle();

  if (!ensure_digipot_initialised() || !ensure_adc_initialised() || !ensure_led_router_initialised()) {
    emit_line(callbacks, "# channel_map,error=hardware_initialisation_failed");
    return ExecutionStatus{false, PHOENIX_BENCHMARK_ERR_UNIMPLEMENTED, k_error_hardware_failure};
  }

  (void) select_led_state(LedRouterState::LED_ROUTER_STATE_DRAIN);

  reset_accumulators(accumulators);

  if (!apply_wiper_code(options.wiper_code)) {
    emit_line(callbacks, "# channel_map,error=ad524x_failure");
    return ExecutionStatus{false, PHOENIX_BENCHMARK_ERR_UNIMPLEMENTED, k_error_hardware_failure};
  }

  const bool include_drain_state = g_defaults.include_drain_state;

  for (uint32_t sweep_index = 0u; sweep_index < options.sweep_count; ++sweep_index) {
    for (size_t state_index = 0u; state_index < (sizeof(k_state_sequence) / sizeof(k_state_sequence[0]));
         ++state_index) {
      const StateRequest& request = k_state_sequence[state_index];
      if (!include_drain_state && request.router_state == LedRouterState::LED_ROUTER_STATE_DRAIN) {
        continue;
      }

      if (!sample_state(request, options.dwell_us, accumulators)) {
        emit_line(callbacks, "# channel_map,error=sampling_failed");
        (void) select_led_state(LedRouterState::LED_ROUTER_STATE_DRAIN);
        const char* message = g_last_sample_error != nullptr ? g_last_sample_error : k_error_adc_saturation;
        return ExecutionStatus{false, PHOENIX_BENCHMARK_ERR_UNIMPLEMENTED, message};
      }
    }
  }

  (void) select_led_state(LedRouterState::LED_ROUTER_STATE_DRAIN);
  return ExecutionStatus{true, PHOENIX_BENCHMARK_OK, nullptr};
}

ParseResult parse_command(const char* line) {
  ChannelMapOptions options = {};
  options.apply_defaults(g_defaults);

  const parser::ParseOutcome outcome = parser::parse_command(line, "channel_map");
  if (!outcome.success) {
    return ParseResult{false, options, outcome.error_message};
  }

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

  options.apply_defaults(g_defaults);
  return ParseResult{true, options, nullptr};
}

void reset_state(void) {
  g_defaults          = ChannelMapDefaults{};
  g_power_enabled     = false;
  g_last_sample_error = nullptr;
}

const ChannelMapStateDescriptor* state_descriptors(void) {
  return k_state_descriptors;
}

}  // namespace channel_map

}  // namespace phoenix_benchmark
