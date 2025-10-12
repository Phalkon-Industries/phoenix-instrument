#include "adc_hal.hpp"
#include "channel_map.hpp"
#include "phoenix_benchmark_support.hpp"
#include "phoenix_summary_formatter.hpp"
#include <Arduino.h>

namespace {

using phoenix_benchmark::ChannelMapDefaults;
using phoenix_benchmark::ChannelMapOptions;
using phoenix_benchmark::ChannelMapStateDescriptor;
using phoenix_benchmark::ExecutionStatus;
using phoenix_benchmark::OutputCallbacks;
namespace channel_map = phoenix_benchmark::channel_map;

using phoenix_benchmark_support::BenchmarkChannel;
using phoenix_benchmark_support::determine_dominant_channel;
using phoenix_benchmark_support::format_channel_alignment_label;
using phoenix_benchmark_support::format_summary_header;
using phoenix_benchmark_support::format_summary_row;
using phoenix_benchmark_support::StateAccumulator;

constexpr double        k_channel_min_drain_delta = 5.0;
constexpr unsigned long k_serial_baud_rate        = 115200UL;
constexpr size_t        k_command_buffer_bytes    = 160u;

constexpr AdcHalChannel k_channel_a = AdcHalChannel::ADC_HAL_CHANNEL_4;
constexpr AdcHalChannel k_channel_b = AdcHalChannel::ADC_HAL_CHANNEL_5;

const ChannelMapDefaults k_channel_map_defaults = {
    .sweep_count         = 3000u,
    .dwell_us            = 100u,
    .wiper_code          = 0x00u,
    .include_drain_state = true,
};

StateAccumulator g_state_accumulators[channel_map::k_state_descriptor_count];

void reset_accumulators(void) {
  for (size_t index = 0; index < channel_map::k_state_descriptor_count; ++index) {
    g_state_accumulators[index] = StateAccumulator{};
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

void print_run_header(const ChannelMapOptions& options) {
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

  char line_buffer[phoenix_benchmark_support::k_summary_table_buffer_bytes] = {};
  if (!format_summary_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# summary_table_format_failed"));
    return;
  }
  Serial.println(line_buffer);

  const ChannelMapStateDescriptor* descriptors       = channel_map::state_descriptors();
  const size_t                     descriptor_count  = channel_map::k_state_descriptor_count;
  const StateAccumulator&          drain_accumulator = g_state_accumulators[channel_map::k_drain_state_index];

  bool printed_state_row = false;
  for (size_t index = 0; index < descriptor_count; ++index) {
    const ChannelMapStateDescriptor& descriptor = descriptors[index];
    if (!descriptor.include_in_summary) {
      continue;
    }
    if (!k_channel_map_defaults.include_drain_state && descriptor.is_reference_state) {
      continue;
    }

    const StateAccumulator& accumulator = g_state_accumulators[index];
    if (!accumulator.channel_a_codes.has_samples()) {
      continue;
    }

    char                   alignment_label[phoenix_benchmark_support::k_summary_map_width + 1u] = {};
    const BenchmarkChannel observed_channel =
        determine_dominant_channel(drain_accumulator, accumulator, k_channel_min_drain_delta);
    const bool aligned = format_channel_alignment_label(descriptor.expected_channel, observed_channel, alignment_label,
                                                        sizeof(alignment_label));

    const phoenix_benchmark_support::SummaryRowValues row_values = {
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
        .has_channel_metrics = true,
    };

    if (!format_summary_row(row_values, line_buffer, sizeof(line_buffer))) {
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

bool execute_channel_map_command(const ChannelMapOptions& options) {
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

  const OutputCallbacks callbacks = {serial_print_line, nullptr};
  const ExecutionStatus status    = channel_map::run(options, g_state_accumulators, callbacks);

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

  const phoenix_benchmark::ParseResult parse_result = channel_map::parse_command(line);
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

  channel_map::reset_state();
  channel_map::initialise(k_channel_map_defaults);

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
