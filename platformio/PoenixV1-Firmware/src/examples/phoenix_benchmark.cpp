#include "adc_hal.hpp"
#include "channel_map/channel_map.hpp"
#include <Arduino.h>
#include <cstdio>

// This sketch forwards all benchmark orchestration to the phoenix benchmark
// library in lib/phoenix_benchmark/channel_map so other firmware
// modules can reuse the same C-style API.

namespace {

constexpr double        k_channel_min_drain_delta = 5.0;
constexpr unsigned long k_serial_baud_rate        = 115200UL;
constexpr size_t        k_command_buffer_bytes    = 160u;

constexpr AdcHalChannel k_channel_a = AdcHalChannel::ADC_HAL_CHANNEL_4;
constexpr AdcHalChannel k_channel_b = AdcHalChannel::ADC_HAL_CHANNEL_5;

const PhoenixBenchmarkChannelMapDefaults k_channel_map_defaults = {
    .sweep_count = 100u,
    .dwell_us    = 100u,
    .wiper_code  = 0x00u,
};

PhoenixBenchmarkStateAccumulator g_state_accumulators[k_phoenix_benchmark_channel_map_state_descriptor_count];

void reset_accumulators(void) {
  for (size_t index = 0; index < k_phoenix_benchmark_channel_map_state_descriptor_count; ++index) {
    g_state_accumulators[index] = PhoenixBenchmarkStateAccumulator{};
  }
}

void serial_print_line(const char* line) {
  if (line != nullptr) {
    Serial.println(line);
  }
}

void print_ready_banner(void) {
  Serial.println(F("# phoenix benchmark ready"));
  Serial.println(F("# ready"));
}

void wait_for_serial(void) {
  while (!Serial) {
    delay(50);
  }
}

void print_run_header(const PhoenixBenchmarkChannelMapOptions& options) {
  Serial.println(F("# phoenix benchmark starting"));
  Serial.print(F("# config,sweep_count="));
  Serial.print(options.sweep_count);
  Serial.print(F(",led_dwell_us="));
  Serial.print(options.dwell_us);
  Serial.print(F(",wiper_code=0x"));
  if (options.wiper_code < 0x10u) {
    Serial.print('0');
  }
  Serial.print(options.wiper_code, HEX);
  Serial.print(F(",channels="));
  Serial.print(static_cast<uint8_t>(k_channel_a));
  Serial.print('/');
  Serial.println(static_cast<uint8_t>(k_channel_b));

  Serial.print(F("# channel_map_config,minimum_drain_delta="));
  Serial.println(k_channel_min_drain_delta, 3);
}

void print_summary_table(void) {
  Serial.println();
  Serial.println(F("# summary_table"));

  char line_buffer[k_phoenix_benchmark_channel_map_summary_table_buffer_bytes] = {};
  if (!phoenix_benchmark_channel_map_format_summary_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# summary_table_format_failed"));
    return;
  }
  Serial.println(line_buffer);

  const PhoenixBenchmarkChannelMapStateDescriptor* descriptors = phoenix_benchmark_channel_map_state_descriptors();
  const size_t                            descriptor_count     = k_phoenix_benchmark_channel_map_state_descriptor_count;
  const PhoenixBenchmarkStateAccumulator& drain_accumulator =
      g_state_accumulators[k_phoenix_benchmark_channel_map_drain_state_index];

  bool printed_state_row = false;
  for (size_t index = 0; index < descriptor_count; ++index) {
    const PhoenixBenchmarkChannelMapStateDescriptor& descriptor = descriptors[index];
    if (!descriptor.include_in_summary) {
      continue;
    }
    const PhoenixBenchmarkStateAccumulator& accumulator = g_state_accumulators[index];
    const bool                              has_samples = accumulator.channel_a_codes.has_samples();
    const bool                              channel_a_saturated =
        (accumulator.channel_a_saturation_count > 0u) ||
        (has_samples && (phoenix_benchmark_is_adc_code_saturated(accumulator.channel_a_codes.max_value) ||
                         phoenix_benchmark_is_adc_code_saturated(accumulator.channel_a_codes.min_value)));
    const bool channel_b_saturated =
        (accumulator.channel_b_saturation_count > 0u) ||
        (has_samples && (phoenix_benchmark_is_adc_code_saturated(accumulator.channel_b_codes.max_value) ||
                         phoenix_benchmark_is_adc_code_saturated(accumulator.channel_b_codes.min_value)));
    const bool has_saturation = channel_a_saturated || channel_b_saturated;
    if (!has_samples && !has_saturation) {
      continue;
    }

    char                          alignment_label[k_phoenix_benchmark_channel_map_summary_map_width + 1u] = {};
    const PhoenixBenchmarkChannel observed_channel = phoenix_benchmark_channel_map_determine_dominant_channel(
        drain_accumulator, accumulator, k_channel_min_drain_delta);
    const bool aligned = phoenix_benchmark_channel_map_format_alignment_label(
        descriptor.expected_channel, observed_channel, alignment_label, sizeof(alignment_label));

    char        warning_label[k_phoenix_benchmark_channel_map_summary_warning_width + 1u] = {};
    const char* warning_text                                                              = nullptr;
    if (has_saturation) {
      if (channel_a_saturated && channel_b_saturated) {
        std::snprintf(warning_label, sizeof(warning_label), "SAT A=%lu,B=%lu",
                      static_cast<unsigned long>(accumulator.channel_a_saturation_count),
                      static_cast<unsigned long>(accumulator.channel_b_saturation_count));
      }
      else if (channel_a_saturated) {
        std::snprintf(warning_label, sizeof(warning_label), "SAT A=%lu",
                      static_cast<unsigned long>(accumulator.channel_a_saturation_count));
      }
      else {
        std::snprintf(warning_label, sizeof(warning_label), "SAT B=%lu",
                      static_cast<unsigned long>(accumulator.channel_b_saturation_count));
      }
      warning_text = warning_label;
    }

    const PhoenixBenchmarkChannelMapSummaryRowValues row_values = {
        .label               = descriptor.label,
        .sample_count        = accumulator.channel_a_codes.count,
        .mean_channel_a      = accumulator.channel_a_codes.mean,
        .std_channel_a       = accumulator.channel_a_codes.standard_deviation(),
        .min_channel_a       = static_cast<double>(accumulator.channel_a_codes.min_value),
        .max_channel_a       = static_cast<double>(accumulator.channel_a_codes.max_value),
        .mean_channel_b      = accumulator.channel_b_codes.mean,
        .std_channel_b       = accumulator.channel_b_codes.standard_deviation(),
        .min_channel_b       = static_cast<double>(accumulator.channel_b_codes.min_value),
        .max_channel_b       = static_cast<double>(accumulator.channel_b_codes.max_value),
        .channel_alignment   = aligned ? alignment_label : nullptr,
        .warning_label       = warning_text,
        .has_channel_metrics = has_samples,
    };

    if (!phoenix_benchmark_channel_map_format_summary_row(row_values, line_buffer, sizeof(line_buffer))) {
      Serial.println(F("# summary_table_row_format_failed"));
      continue;
    }

    Serial.println(line_buffer);
    printed_state_row = true;
  }

  if (!printed_state_row) {
    Serial.println(F("(no state samples captured)"));
  }

  Serial.println();
}

bool execute_channel_map_command(const PhoenixBenchmarkChannelMapOptions& options) {
  Serial.print(F("# running,scenario=channel_map,sweeps="));
  Serial.print(options.sweep_count);
  Serial.print(F(",dwell_us="));
  Serial.print(options.dwell_us);
  Serial.print(F(",wiper_code=0x"));
  if (options.wiper_code < 0x10u) {
    Serial.print('0');
  }
  Serial.println(options.wiper_code, HEX);

  reset_accumulators();
  print_run_header(options);

  const PhoenixBenchmarkChannelMapOutputCallbacks callbacks = {serial_print_line, nullptr};
  const PhoenixBenchmarkChannelMapExecutionStatus status =
      phoenix_benchmark_channel_map_run(options, g_state_accumulators, callbacks);

  if (!status.success) {
    Serial.print(F("# error,channel_map_failed"));
    if (status.message != nullptr) {
      Serial.print(F(",reason="));
      Serial.print(status.message);
    }
    Serial.println();
    return false;
  }

  print_summary_table();
  if (status.has_warnings) {
    Serial.println(F("# channel_map_warnings,reason=adc_saturation"));
  }
  Serial.println(F("# benchmark_complete"));
  return true;
}

void handle_command_line(const char* line) {
  if (line == nullptr) {
    Serial.println(F("# error,null_command"));
    print_ready_banner();
    return;
  }

  if (line[0] == '\0') {
    print_ready_banner();
    return;
  }

  const PhoenixBenchmarkChannelMapParseResult parse_result = phoenix_benchmark_channel_map_parse_command(line);
  if (!parse_result.success) {
    Serial.print(F("# error,channel_map_parse_failed"));
    if (parse_result.error_message != nullptr) {
      Serial.print(F(",reason="));
      Serial.print(parse_result.error_message);
    }
    Serial.println();
    Serial.println(F("# ready"));
    return;
  }

  if (!execute_channel_map_command(parse_result.options)) {
    Serial.println(F("# ready"));
    return;
  }

  Serial.println(F("# ready"));
}

}  // namespace

void setup() {
  Serial.begin(k_serial_baud_rate);
  wait_for_serial();

  phoenix_benchmark_channel_map_reset_state();
  phoenix_benchmark_channel_map_initialise(k_channel_map_defaults);

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
