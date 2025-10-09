#include "ad524x.hpp"
#include "adc_hal.hpp"
#include "led_router.hpp"
#include "main.hpp"
#include "phoenix_benchmark_support.hpp"
#include "phoenix_summary_formatter.hpp"
#include <Arduino.h>
#include <Wire.h>

namespace {

using phoenix_benchmark_support::BenchmarkChannel;
using phoenix_benchmark_support::ChannelMapRequest;
using phoenix_benchmark_support::determine_dominant_channel;
using phoenix_benchmark_support::format_channel_alignment_label;
using phoenix_benchmark_support::parse_channel_map_command;
using phoenix_benchmark_support::RunningStats;
using phoenix_benchmark_support::SampleResult;
using phoenix_benchmark_support::StateAccumulator;

struct StateRequest {
  const char*      label;
  LedRouterState   router_state;
  size_t           accumulator_index;
  BenchmarkChannel expected_channel;
};

struct ChannelMapRunConfig {
  uint32_t sweep_count;
  uint32_t led_dwell_us;
};

constexpr uint32_t k_spi_clock_hz          = 500000UL;
constexpr uint8_t  k_digipot_channels[]    = {0u, 1u};
constexpr size_t   k_digipot_channel_count = sizeof(k_digipot_channels) / sizeof(k_digipot_channels[0]);

constexpr StateRequest k_state_sequence[] = {
    {"Drain", LedRouterState::LED_ROUTER_STATE_DRAIN, 0u, BenchmarkChannel::kUnknown},
    {"LED1", LedRouterState::LED_ROUTER_STATE_LED1, 1u, BenchmarkChannel::kChannelA},
    {"LED2", LedRouterState::LED_ROUTER_STATE_LED2, 2u, BenchmarkChannel::kChannelB},
};
constexpr size_t k_state_count = sizeof(k_state_sequence) / sizeof(k_state_sequence[0]);

constexpr AdcHalConfig k_adc_config = {
    .chip_select_pin = PIN_ADC_CS,
    .spi_clock_hz    = k_spi_clock_hz,
    .default_gain    = AdcHalGain::ADC_HAL_GAIN_1,
};

constexpr uint32_t      k_adc_timeout_us          = 1000000u;
constexpr uint8_t       k_digipot_wiper_code      = 0x00u;
constexpr uint32_t      k_default_led_dwell_us    = 100u;
constexpr uint32_t      k_default_sweep_count     = 3000u;
constexpr bool          k_include_drain_state     = true;
constexpr AdcHalChannel k_channel_a               = AdcHalChannel::ADC_HAL_CHANNEL_4;
constexpr AdcHalChannel k_channel_b               = AdcHalChannel::ADC_HAL_CHANNEL_5;
constexpr double        k_channel_dominance_ratio = 1.2;
constexpr double        k_channel_min_range       = 5.0;
constexpr unsigned long k_serial_baud_rate        = 115200UL;
constexpr size_t        k_command_buffer_bytes    = 160u;

StateAccumulator       g_state_accumulators[k_state_count];
RunningStats<uint32_t> g_cycle_duration_stats;
volatile bool          g_benchmark_failed = false;

void reset_accumulators(void) {
  for (size_t index = 0; index < k_state_count; ++index) {
    g_state_accumulators[index] = StateAccumulator{};
  }
  g_cycle_duration_stats = RunningStats<uint32_t>{};
}

void print_ready_banner(void) {
  Serial.println(F("# phoenix benchmark ready"));
  Serial.println(F("# ready"));
}

void wait_for_serial(void) {
  while (!Serial) {
    // Give the USB host time to enumerate and open the CDC channel.
    delay(50);
  }
}

void enable_power_domains(void) {
  pinMode(PIN_ENABLE_POWER, OUTPUT);
  digitalWrite(PIN_ENABLE_POWER, HIGH);
}

void configure_led_paths_off(void) {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_BLUE, LOW);
}

bool configure_led_router(void) {
  const LedRouterConfig config = {
      TS5A3359_IN1,
      TS5A3359_IN2,
  };

  const int return_code = led_router_initialize(&config);
  if (return_code != LED_ROUTER_OK) {
    Serial.print(F("# led_router_initialize failed: "));
    Serial.println(return_code);
    return false;
  }

  return true;
}

bool initialise_ad524x(void) {
  Wire.begin();
  int return_code = ad524x_initialize(AD5242_I2C_ADDRESS, &Wire);
  if (return_code != AD524X_OK) {
    Serial.print(F("# ad524x_initialize failed: "));
    Serial.println(return_code);
    return false;
  }

  for (size_t i = 0; i < k_digipot_channel_count; ++i) {
    return_code = ad524x_set_wiper(k_digipot_channels[i], k_digipot_wiper_code);
    if (return_code != AD524X_OK) {
      Serial.print(F("# ad524x_set_wiper failed on channel "));
      Serial.print(k_digipot_channels[i]);
      Serial.print(F(": "));
      Serial.println(return_code);
      return false;
    }
  }
  return true;
}

bool initialise_adc(void) {
  const int init_code = adc_hal_initialize(&k_adc_config);
  if (init_code != ADC_HAL_OK) {
    Serial.print(F("# adc_hal_initialize failed: "));
    Serial.println(init_code);
    return false;
  }

  const int config_code = adc_hal_apply_default_configuration();
  if (config_code != ADC_HAL_OK) {
    Serial.print(F("# adc_hal_apply_default_configuration failed: "));
    Serial.println(config_code);
    return false;
  }

  return true;
}

bool select_led_state(LedRouterState state) {
  const int return_code = led_router_set_state(state);
  if (return_code != LED_ROUTER_OK) {
    Serial.print(F("# led_router_set_state failed: "));
    Serial.println(return_code);
    return false;
  }
  return true;
}

bool read_adc_channel(AdcHalChannel channel, int32_t* out_code) {
  const int return_code = adc_hal_read_single_ended(channel, k_adc_timeout_us, out_code);
  if (return_code != ADC_HAL_OK) {
    Serial.print(F("# adc_hal_read_single_ended failed: "));
    Serial.println(return_code);
    return false;
  }
  return true;
}

void print_run_header(const ChannelMapRunConfig& run_config) {
  Serial.println(F("# phoenix benchmark starting"));
  Serial.print(F("# config,sweep_count="));
  Serial.print(run_config.sweep_count);
  Serial.print(F(",led_dwell_us="));
  Serial.print(run_config.led_dwell_us);
  Serial.print(F(",wiper_code=0x"));
  if (k_digipot_wiper_code < 0x10u) {
    Serial.print('0');
  }
  Serial.print(k_digipot_wiper_code, HEX);
  Serial.print(F(",channels="));
  Serial.print(static_cast<uint8_t>(k_channel_a));
  Serial.print('/');
  Serial.println(static_cast<uint8_t>(k_channel_b));

  Serial.print(F("# channel_map_config,dominance_ratio="));
  Serial.print(k_channel_dominance_ratio, 3);
  Serial.print(F(",minimum_range="));
  Serial.println(k_channel_min_range, 3);
}

void print_summary_table(void) {
  // Present a final fixed-width table so operators can paste the summary into a spreadsheet
  // without manual re-alignment.
  Serial.println();
  Serial.println(F("# summary_table"));

  char line_buffer[phoenix_benchmark_support::k_summary_table_buffer_bytes] = {};  // Matches formatter contract to
                                                                                   // avoid truncating any column.
  if (!phoenix_benchmark_support::format_summary_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# summary_table_format_failed"));
    return;
  }
  Serial.println(line_buffer);

  bool printed_state_row = false;
  for (size_t state_idx = 0; state_idx < k_state_count; ++state_idx) {
    const StateRequest& request           = k_state_sequence[state_idx];
    const size_t        accumulator_index = request.accumulator_index;
    if (!k_include_drain_state && request.router_state == LedRouterState::LED_ROUTER_STATE_DRAIN) {
      continue;
    }

    const StateAccumulator& accumulator = g_state_accumulators[accumulator_index];
    if (!accumulator.channel_a_codes.has_samples()) {
      continue;
    }

    char alignment_label[phoenix_benchmark_support::k_summary_map_width + 1u] = {};
    // Compare channel ranges so we can surface wiring or LED routing mismatches without
    // requiring the operator to interpret raw CSV values. The heuristic only flags a channel
    // when its range meaningfully exceeds the peer, preventing false positives from sensor noise.
    const phoenix_benchmark_support::BenchmarkChannel observed_channel =
        determine_dominant_channel(accumulator, k_channel_dominance_ratio, k_channel_min_range);
    const bool aligned = format_channel_alignment_label(request.expected_channel, observed_channel, alignment_label,
                                                        sizeof(alignment_label));

    const phoenix_benchmark_support::SummaryRowValues row_values = {
        .label               = request.label,
        .sample_count        = accumulator.channel_a_codes.count,
        .mean_channel_a      = accumulator.channel_a_codes.mean,
        .std_channel_a       = accumulator.channel_a_codes.standard_deviation(),
        .min_channel_a       = static_cast<double>(accumulator.channel_a_codes.min_value),
        .max_channel_a       = static_cast<double>(accumulator.channel_a_codes.max_value),
        .mean_channel_b      = accumulator.channel_b_codes.mean,
        .std_channel_b       = accumulator.channel_b_codes.standard_deviation(),
        .min_channel_b       = static_cast<double>(accumulator.channel_b_codes.min_value),
        .max_channel_b       = static_cast<double>(accumulator.channel_b_codes.max_value),
        .step_mean_us        = accumulator.state_duration_us.mean,
        .step_std_us         = accumulator.state_duration_us.standard_deviation(),
        .step_range_us       = accumulator.state_duration_us.range(),
        .channel_alignment   = aligned ? alignment_label : nullptr,
        .has_channel_metrics = true,
    };

    if (!phoenix_benchmark_support::format_summary_row(row_values, line_buffer, sizeof(line_buffer))) {
      Serial.println(F("# summary_table_row_format_failed"));
      continue;
    }

    Serial.println(line_buffer);
    printed_state_row = true;
  }

  if (!printed_state_row) {
    Serial.println(F("(no state samples captured)"));
  }

  if (g_cycle_duration_stats.has_samples()) {
    const phoenix_benchmark_support::SummaryRowValues cycle_row = {
        .label               = "Cycle",
        .sample_count        = g_cycle_duration_stats.count,
        .mean_channel_a      = 0.0,
        .std_channel_a       = 0.0,
        .min_channel_a       = 0.0,
        .max_channel_a       = 0.0,
        .mean_channel_b      = 0.0,
        .std_channel_b       = 0.0,
        .min_channel_b       = 0.0,
        .max_channel_b       = 0.0,
        .step_mean_us        = g_cycle_duration_stats.mean,
        .step_std_us         = g_cycle_duration_stats.standard_deviation(),
        .step_range_us       = g_cycle_duration_stats.range(),
        .channel_alignment   = nullptr,
        .has_channel_metrics = false,
    };

    if (phoenix_benchmark_support::format_summary_row(cycle_row, line_buffer, sizeof(line_buffer))) {
      Serial.println(line_buffer);
    }
    else {
      Serial.println(F("# summary_table_cycle_format_failed"));
    }
  }

  Serial.println();
}

bool sample_once(const StateRequest& request, const ChannelMapRunConfig& run_config, SampleResult* out_result) {
  const unsigned long state_start = micros();

  if (!select_led_state(request.router_state)) {
    return false;
  }

  if (run_config.led_dwell_us > 0u) {
    // Allow the selected LED path to settle before capturing ADC samples.
    delayMicroseconds(run_config.led_dwell_us);
  }

  int32_t channel_a_code = 0;
  if (!read_adc_channel(k_channel_a, &channel_a_code)) {
    return false;
  }

  int32_t channel_b_code = 0;
  if (!read_adc_channel(k_channel_b, &channel_b_code)) {
    return false;
  }

  const unsigned long state_end = micros();

  out_result->channel_a_code = channel_a_code;
  out_result->channel_b_code = channel_b_code;
  out_result->elapsed_us     = static_cast<uint32_t>(state_end - state_start);
  out_result->timestamp_us   = static_cast<uint32_t>(state_start);
  return true;
}

ChannelMapRunConfig make_run_config(const ChannelMapRequest& request) {
  ChannelMapRunConfig config = {
      .sweep_count  = request.sweep_count > 0u ? request.sweep_count : k_default_sweep_count,
      .led_dwell_us = request.has_dwell_override ? request.dwell_us : k_default_led_dwell_us,
  };
  return config;
}

void execute_channel_map_run(const ChannelMapRunConfig& run_config);

bool execute_channel_map_command(const ChannelMapRequest& request) {
  const ChannelMapRunConfig run_config = make_run_config(request);

  Serial.print(F("# running,scenario=channel_map,sweeps="));
  Serial.print(run_config.sweep_count);
  Serial.print(F(",dwell_us="));
  Serial.println(run_config.led_dwell_us);

  reset_accumulators();
  execute_channel_map_run(run_config);
  (void) select_led_state(LedRouterState::LED_ROUTER_STATE_DRAIN);

  if (g_benchmark_failed) {
    Serial.println(F("# error,channel_map_failed"));
    g_benchmark_failed = false;
    return false;
  }

  return true;
}

void execute_channel_map_run(const ChannelMapRunConfig& run_config) {
  g_benchmark_failed = false;
  print_run_header(run_config);

  for (size_t sweep_index = 0; sweep_index < run_config.sweep_count; ++sweep_index) {
    const unsigned long sweep_start = micros();
    for (size_t state_idx = 0; state_idx < k_state_count; ++state_idx) {
      const StateRequest& request = k_state_sequence[state_idx];
      if (!k_include_drain_state && request.router_state == LedRouterState::LED_ROUTER_STATE_DRAIN) {
        continue;
      }

      SampleResult sample = {};
      if (!sample_once(request, run_config, &sample)) {
        g_benchmark_failed = true;
        return;
      }

      StateAccumulator& accumulator = g_state_accumulators[request.accumulator_index];
      accumulator.channel_a_codes.update(sample.channel_a_code);
      accumulator.channel_b_codes.update(sample.channel_b_code);
      accumulator.state_duration_us.update(sample.elapsed_us);
    }

    const unsigned long sweep_end  = micros();
    const uint32_t      cycle_time = static_cast<uint32_t>(sweep_end - sweep_start);
    g_cycle_duration_stats.update(cycle_time);
  }

  print_summary_table();
  Serial.println(F("# benchmark_complete"));
}

void handle_command_line(const char* line) {
  if (line == nullptr) {
    Serial.println(F("# error,null_command"));
    print_ready_banner();
    return;
  }

  if (line[0] == '\0') {
    // Empty commands serve as an out-of-band ready probe for host tools that
    // connect after the firmware is already idle. Respond with a fresh banner
    // so the host can synchronise without a manual reset.
    print_ready_banner();
    return;
  }

  ChannelMapRequest request = {};
  if (!parse_channel_map_command(line, &request)) {
    Serial.println(F("# error,unsupported_command"));
    Serial.println(F("# ready"));
    return;
  }

  if (!execute_channel_map_command(request)) {
    Serial.println(F("# ready"));
    return;
  }

  Serial.println(F("# ready"));
}

bool initialise_benchmark(void) {
  enable_power_domains();
  configure_led_paths_off();

  if (!initialise_ad524x()) {
    return false;
  }
  if (!initialise_adc()) {
    return false;
  }
  if (!configure_led_router()) {
    return false;
  }

  if (!select_led_state(LedRouterState::LED_ROUTER_STATE_DRAIN)) {
    return false;
  }

  return true;
}

}  // namespace

void setup() {
  Serial.begin(k_serial_baud_rate);
  wait_for_serial();

  if (!initialise_benchmark()) {
    Serial.println(F("# benchmark_initialisation_failed"));
    return;
  }

  reset_accumulators();
  print_ready_banner();
}

void loop() {
  static char   command_buffer[k_command_buffer_bytes];
  static size_t buffer_index = 0u;

  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      command_buffer[buffer_index] = '\0';
      handle_command_line(command_buffer);
      buffer_index = 0u;
      continue;
    }

    if (buffer_index >= (k_command_buffer_bytes - 1u)) {
      Serial.println(F("# error,command_too_long"));
      buffer_index = 0u;
      continue;
    }

    command_buffer[buffer_index++] = incoming;
  }
}
