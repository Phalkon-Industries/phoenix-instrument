#include "ad524x.hpp"
#include "adc_hal.hpp"
#include "led_router.hpp"
#include "main.hpp"
#include "phoenix_benchmark_support.hpp"
#include "phoenix_summary_formatter.hpp"
#include <Arduino.h>
#include <Wire.h>

namespace {

using phoenix_benchmark_support::RunningStats;
using phoenix_benchmark_support::SampleResult;
using phoenix_benchmark_support::StateAccumulator;

struct StateRequest {
  const char*    label;
  LedRouterState router_state;
  size_t         accumulator_index;
};

struct BenchmarkConfig {
  AdcHalConfig  adc_config;
  AdcHalGain    adc_gain;
  uint32_t      adc_timeout_us;
  uint8_t       digipot_wiper_code;
  uint8_t       digipot_midscale_code;
  uint32_t      led_dwell_us;
  uint32_t      sweep_count;
  uint32_t      running_stats_interval;
  uint32_t      human_readable_interval_s;
  bool          include_drain_state;
  bool          print_csv_header;
  bool          enable_csv_stream;
  AdcHalChannel channel_a;
  AdcHalChannel channel_b;
};

constexpr uint32_t k_spi_clock_hz          = 500000UL;
constexpr uint8_t  k_digipot_channels[]    = {0u, 1u};
constexpr size_t   k_digipot_channel_count = sizeof(k_digipot_channels) / sizeof(k_digipot_channels[0]);

constexpr StateRequest k_state_sequence[] = {
    {"Drain", LedRouterState::LED_ROUTER_STATE_DRAIN, 0u},
    {"LED1", LedRouterState::LED_ROUTER_STATE_LED1, 1u},
    {"LED2", LedRouterState::LED_ROUTER_STATE_LED2, 2u},
};
constexpr size_t k_state_count = sizeof(k_state_sequence) / sizeof(k_state_sequence[0]);

constexpr BenchmarkConfig k_benchmark_config = {
    .adc_config =
        {
            .chip_select_pin = PIN_ADC_CS,
            .spi_clock_hz    = k_spi_clock_hz,
            .default_gain    = AdcHalGain::ADC_HAL_GAIN_1,
        },
    .adc_gain                  = AdcHalGain::ADC_HAL_GAIN_1,
    .adc_timeout_us            = 1000000u,
    .digipot_wiper_code        = 0x00u,
    .digipot_midscale_code     = 0x80u,
    .led_dwell_us              = 100u,
    .sweep_count               = 3000u,
    .running_stats_interval    = 1000u,
    .human_readable_interval_s = 0u,  // Zero disables periodic snapshots so the operator only sees the final table.
    .include_drain_state       = true,
    .print_csv_header          = true,
    .enable_csv_stream         = false,
    .channel_a                 = AdcHalChannel::ADC_HAL_CHANNEL_4,
    .channel_b                 = AdcHalChannel::ADC_HAL_CHANNEL_5,
};

StateAccumulator       g_state_accumulators[k_state_count];
RunningStats<uint32_t> g_cycle_duration_stats;
volatile bool          g_benchmark_failed = false;

void wait_for_serial(void) {
  const unsigned long start_ms = millis();
  while (!Serial && (millis() - start_ms) < 2000UL) {
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
    return_code = ad524x_set_wiper(k_digipot_channels[i], k_benchmark_config.digipot_wiper_code);
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
  const int init_code = adc_hal_initialize(&k_benchmark_config.adc_config);
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
  const int return_code = adc_hal_read_single_ended(channel, k_benchmark_config.adc_timeout_us, out_code);
  if (return_code != ADC_HAL_OK) {
    Serial.print(F("# adc_hal_read_single_ended failed: "));
    Serial.println(return_code);
    return false;
  }
  return true;
}

void print_run_header(void) {
  Serial.println(F("# phoenix benchmark starting"));
  Serial.print(F("# config,sweep_count="));
  Serial.print(k_benchmark_config.sweep_count);
  Serial.print(F(",led_dwell_us="));
  Serial.print(k_benchmark_config.led_dwell_us);
  Serial.print(F(",running_stats_interval="));
  Serial.print(k_benchmark_config.running_stats_interval);
  Serial.print(F(",human_interval_s="));
  Serial.print(k_benchmark_config.human_readable_interval_s);
  Serial.print(F(",wiper_code=0x"));
  if (k_benchmark_config.digipot_wiper_code < 0x10u) {
    Serial.print('0');
  }
  Serial.print(k_benchmark_config.digipot_wiper_code, HEX);
  Serial.print(F(",channels="));
  Serial.print(static_cast<uint8_t>(k_benchmark_config.channel_a));
  Serial.print('/');
  Serial.println(static_cast<uint8_t>(k_benchmark_config.channel_b));

  Serial.print(F("# output_mode,csv_stream="));
  Serial.println(k_benchmark_config.enable_csv_stream ? F("enabled") : F("disabled"));

  if (k_benchmark_config.enable_csv_stream && k_benchmark_config.print_csv_header) {
    Serial.println(
        F("state,sweep_index,state_sample_index,state_timestamp_us,channel_a_code,channel_b_code,state_duration_us"));
  }
}

void print_sample_csv(const StateRequest& request, uint32_t sweep_index, uint32_t state_sample_index,
                      const SampleResult& sample) {
  Serial.print(request.label);
  Serial.print(',');
  Serial.print(sweep_index);
  Serial.print(',');
  Serial.print(state_sample_index);
  Serial.print(',');
  Serial.print(sample.timestamp_us);
  Serial.print(',');
  Serial.print(sample.channel_a_code);
  Serial.print(',');
  Serial.print(sample.channel_b_code);
  Serial.print(',');
  Serial.println(sample.elapsed_us);
}

void print_running_stats(const StateRequest& request, const StateAccumulator& accumulator) {
  Serial.print(F("# running_stats,state="));
  Serial.print(request.label);
  Serial.print(F(",samples="));
  Serial.print(accumulator.channel_a_codes.count);
  Serial.print(F(",channel_a_mean="));
  Serial.print(accumulator.channel_a_codes.mean, 6);
  Serial.print(F(",channel_a_std="));
  Serial.print(accumulator.channel_a_codes.standard_deviation(), 6);
  Serial.print(F(",channel_b_mean="));
  Serial.print(accumulator.channel_b_codes.mean, 6);
  Serial.print(F(",channel_b_std="));
  Serial.print(accumulator.channel_b_codes.standard_deviation(), 6);
  Serial.print(F(",state_duration_us_mean="));
  Serial.print(accumulator.state_duration_us.mean, 3);
  Serial.print(F(",state_duration_us_std="));
  Serial.println(accumulator.state_duration_us.standard_deviation(), 3);
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
    if (!k_benchmark_config.include_drain_state && request.router_state == LedRouterState::LED_ROUTER_STATE_DRAIN) {
      continue;
    }

    const StateAccumulator& accumulator = g_state_accumulators[accumulator_index];
    if (!accumulator.channel_a_codes.has_samples()) {
      continue;
    }

    const phoenix_benchmark_support::SummaryRowValues row_values = {
        .label               = request.label,
        .sample_count        = accumulator.channel_a_codes.count,
        .mean_channel_a      = accumulator.channel_a_codes.mean,
        .std_channel_a       = accumulator.channel_a_codes.standard_deviation(),
        .mean_channel_b      = accumulator.channel_b_codes.mean,
        .std_channel_b       = accumulator.channel_b_codes.standard_deviation(),
        .step_mean_us        = accumulator.state_duration_us.mean,
        .step_std_us         = accumulator.state_duration_us.standard_deviation(),
        .step_range_us       = accumulator.state_duration_us.range(),
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
        .mean_channel_b      = 0.0,
        .std_channel_b       = 0.0,
        .step_mean_us        = g_cycle_duration_stats.mean,
        .step_std_us         = g_cycle_duration_stats.standard_deviation(),
        .step_range_us       = g_cycle_duration_stats.range(),
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

bool sample_once(const StateRequest& request, SampleResult* out_result) {
  const unsigned long state_start = micros();

  if (!select_led_state(request.router_state)) {
    return false;
  }

  if (k_benchmark_config.led_dwell_us > 0u) {
    // Allow the selected LED path to settle before capturing ADC samples.
    delayMicroseconds(k_benchmark_config.led_dwell_us);
  }

  int32_t channel_a_code = 0;
  if (!read_adc_channel(k_benchmark_config.channel_a, &channel_a_code)) {
    return false;
  }

  int32_t channel_b_code = 0;
  if (!read_adc_channel(k_benchmark_config.channel_b, &channel_b_code)) {
    return false;
  }

  const unsigned long state_end = micros();

  out_result->channel_a_code = channel_a_code;
  out_result->channel_b_code = channel_b_code;
  out_result->elapsed_us     = static_cast<uint32_t>(state_end - state_start);
  out_result->timestamp_us   = static_cast<uint32_t>(state_start);
  return true;
}

void park_hardware(void) {
  for (size_t i = 0; i < k_digipot_channel_count; ++i) {
    (void) ad524x_set_wiper(k_digipot_channels[i], k_benchmark_config.digipot_midscale_code);
  }

  (void) adc_hal_enter_standby();

  const int router_return_code = led_router_shutdown();
  if (router_return_code != LED_ROUTER_OK) {
    Serial.print(F("# led_router_shutdown failed: "));
    Serial.println(router_return_code);
  }
}

void run_benchmark(void) {
  print_run_header();

  for (size_t sweep_index = 0; sweep_index < k_benchmark_config.sweep_count; ++sweep_index) {
    const unsigned long sweep_start = micros();
    for (size_t state_idx = 0; state_idx < k_state_count; ++state_idx) {
      const StateRequest& request = k_state_sequence[state_idx];
      if (!k_benchmark_config.include_drain_state && request.router_state == LedRouterState::LED_ROUTER_STATE_DRAIN) {
        continue;
      }

      SampleResult sample = {};
      if (!sample_once(request, &sample)) {
        g_benchmark_failed = true;
        return;
      }

      StateAccumulator& accumulator = g_state_accumulators[request.accumulator_index];
      accumulator.channel_a_codes.update(sample.channel_a_code);
      accumulator.channel_b_codes.update(sample.channel_b_code);
      accumulator.state_duration_us.update(sample.elapsed_us);

      const uint32_t state_sample_index = accumulator.channel_a_codes.count;
      if (k_benchmark_config.enable_csv_stream) {
        print_sample_csv(request, static_cast<uint32_t>(sweep_index), state_sample_index, sample);

        if (k_benchmark_config.running_stats_interval > 0u &&
            (state_sample_index % k_benchmark_config.running_stats_interval) == 0u) {
          print_running_stats(request, accumulator);
        }
      }
    }

    const unsigned long sweep_end  = micros();
    const uint32_t      cycle_time = static_cast<uint32_t>(sweep_end - sweep_start);
    g_cycle_duration_stats.update(cycle_time);
  }

  print_summary_table();
  Serial.println(F("# benchmark_complete"));
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
  Serial.begin(115200);
  wait_for_serial();

  if (!initialise_benchmark()) {
    Serial.println(F("# benchmark_initialisation_failed"));
    return;
  }

  run_benchmark();
  park_hardware();
}

void loop() {
}
