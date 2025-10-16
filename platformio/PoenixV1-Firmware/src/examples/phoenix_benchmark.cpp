#include "adc_hal.hpp"
#include "adc_speed/adc_speed.hpp"
#include "adc_speed/adc_speed_command_parser.hpp"
#include "adc_speed/adc_speed_formatter.hpp"
#include "channel_map/channel_map.hpp"
#include <Arduino.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// This sketch forwards all benchmark orchestration to the phoenix benchmark
// library in lib/phoenix_benchmark/channel_map so other firmware
// modules can reuse the same C-style API.

namespace {

constexpr double        k_channel_min_drain_delta        = 5.0;
constexpr unsigned long k_serial_baud_rate               = 115200UL;
constexpr size_t        k_command_buffer_bytes           = 160u;
constexpr size_t        k_adc_speed_command_buffer_bytes = 160u;
constexpr char          k_whitespace_tokens[]            = " \t\r\n";

constexpr AdcHalChannel k_channel_a = AdcHalChannel::ADC_HAL_CHANNEL_4;
constexpr AdcHalChannel k_channel_b = AdcHalChannel::ADC_HAL_CHANNEL_5;

const PhoenixBenchmarkChannelMapDefaults k_channel_map_defaults = {
    .sweep_count = 100u,
    .dwell_us    = 100u,
    .wiper_code  = 0x00u,
};

const PhoenixBenchmarkAdcSpeedDefaults k_adc_speed_defaults = {
    .duration_ms     = 1000u,
    .enable_blocking = true,
    .enable_irq      = true,
};

PhoenixBenchmarkStateAccumulator g_state_accumulators[k_phoenix_benchmark_channel_map_state_descriptor_count];

void reset_accumulators(void) {
  // Step 1: Clear each accumulator so new runs start without residual data.
  for (size_t index = 0; index < k_phoenix_benchmark_channel_map_state_descriptor_count; ++index) {
    g_state_accumulators[index] = PhoenixBenchmarkStateAccumulator{};
  }
}

void serial_print_line(const char* line) {
  // Step 1: Forward log lines to Serial when provided by the channel map driver.
  if (line != nullptr) {
    Serial.println(line);
  }
}

void print_ready_banner(void) {
  // Step 1: Announce that the benchmark firmware is ready for commands.
  Serial.println(F("# phoenix benchmark ready"));
  // Step 2: Emit the simple ready token expected by host tooling.
  Serial.println(F("# ready"));
}

void wait_for_serial(void) {
  // Step 1: Poll until the USB serial interface becomes available.
  while (!Serial) {
    delay(50);
  }
}

void print_run_header(const PhoenixBenchmarkChannelMapOptions& options) {
  // Step 1: Describe the run parameters so logs capture the executed scenario.
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

  // Step 2: Record channel map detection thresholds for downstream analysis.
  Serial.print(F("# channel_map_config,minimum_drain_delta="));
  Serial.println(k_channel_min_drain_delta, 3);
}

void print_summary_table(void) {
  // Step 1: Separate the summary from prior logs and print the section banner.
  Serial.println();
  Serial.println(F("# summary_table"));

  // Step 2: Format and output the table header.
  char line_buffer[k_phoenix_benchmark_channel_map_summary_table_buffer_bytes] = {};
  if (!phoenix_benchmark_channel_map_format_summary_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# summary_table_format_failed"));
    return;
  }
  Serial.println(line_buffer);

  // Step 3: Fetch descriptor metadata and drain reference metrics for comparison.
  const PhoenixBenchmarkChannelMapStateDescriptor* descriptors = phoenix_benchmark_channel_map_state_descriptors();
  const size_t                            descriptor_count     = k_phoenix_benchmark_channel_map_state_descriptor_count;
  const PhoenixBenchmarkStateAccumulator& drain_accumulator =
      g_state_accumulators[k_phoenix_benchmark_channel_map_drain_state_index];

  // Step 4: Iterate through each descriptor, printing rows for states with data.
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

    // Step 4a: Determine observed alignment relative to the drain reference.
    char                          alignment_label[k_phoenix_benchmark_channel_map_summary_map_width + 1u] = {};
    const PhoenixBenchmarkChannel observed_channel = phoenix_benchmark_channel_map_determine_dominant_channel(
        drain_accumulator, accumulator, k_channel_min_drain_delta);
    const bool aligned = phoenix_benchmark_channel_map_format_alignment_label(
        descriptor.expected_channel, observed_channel, alignment_label, sizeof(alignment_label));

    // Step 4b: Build any warning text that should accompany the state row.
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

    // Step 4c: Populate the row structure and render it with the formatter helper.
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

  // Step 5: Provide a placeholder message when no rows were emitted.
  if (!printed_state_row) {
    Serial.println(F("(no state samples captured)"));
  }

  Serial.println();
}

const char* skip_leading_whitespace(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }
  while ((*text != '\0') && std::isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  return text;
}

bool extract_command_identifier(const char* line, char* buffer, std::size_t buffer_length) {
  if ((line == nullptr) || (buffer == nullptr) || (buffer_length == 0u)) {
    return false;
  }

  const char* command_token = std::strstr(line, "\"command\"");
  if (command_token == nullptr) {
    return false;
  }

  const char* colon = std::strchr(command_token, ':');
  if (colon == nullptr) {
    return false;
  }

  const char* cursor = colon + 1;
  while ((*cursor != '\0') && std::isspace(static_cast<unsigned char>(*cursor))) {
    ++cursor;
  }

  if (*cursor != '"') {
    return false;
  }
  ++cursor;

  const char* value_start = cursor;
  while ((*cursor != '\0') && (*cursor != '"')) {
    ++cursor;
  }
  if (*cursor != '"') {
    return false;
  }

  const std::size_t token_length = static_cast<std::size_t>(cursor - value_start);
  if ((token_length == 0u) || (token_length >= buffer_length)) {
    return false;
  }

  std::memcpy(buffer, value_start, token_length);
  buffer[token_length] = '\0';
  return true;
}

bool parse_boolean_flag(const char* value, bool* out_flag) {
  if ((value == nullptr) || (out_flag == nullptr)) {
    return false;
  }

  if ((std::strcmp(value, "true") == 0) || (std::strcmp(value, "1") == 0)) {
    *out_flag = true;
    return true;
  }

  if ((std::strcmp(value, "false") == 0) || (std::strcmp(value, "0") == 0)) {
    *out_flag = false;
    return true;
  }

  return false;
}

bool parse_adc_speed_plain_command(const char* line, PhoenixBenchmarkAdcSpeedOptions* options,
                                   const char** error_message) {
  if ((line == nullptr) || (options == nullptr)) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_adc_speed_error_invalid_command;
    }
    return false;
  }

  char        buffer[k_adc_speed_command_buffer_bytes] = {};
  std::size_t length                                   = 0u;
  while ((length < (sizeof(buffer) - 1u)) && (line[length] != '\0')) {
    ++length;
  }
  if (line[length] != '\0') {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_adc_speed_error_invalid_command;
    }
    return false;
  }

  std::memcpy(buffer, line, length);
  buffer[length] = '\0';

  char* token = std::strtok(buffer, k_whitespace_tokens);
  if ((token == nullptr) || (std::strcmp(token, "adc_speed") != 0)) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_adc_speed_error_invalid_command;
    }
    return false;
  }

  PhoenixBenchmarkAdcSpeedOptions parsed_options = phoenix_benchmark_adc_speed_defaults();

  while (true) {
    token = std::strtok(nullptr, k_whitespace_tokens);
    if (token == nullptr) {
      break;
    }

    char* equals = std::strchr(token, '=');
    if (equals == nullptr) {
      if (error_message != nullptr) {
        *error_message = k_phoenix_benchmark_adc_speed_error_invalid_command;
      }
      return false;
    }

    *equals           = '\0';
    const char* key   = token;
    const char* value = equals + 1;
    if ((key == nullptr) || (value == nullptr) || (key[0] == '\0') || (value[0] == '\0')) {
      if (error_message != nullptr) {
        *error_message = k_phoenix_benchmark_adc_speed_error_invalid_value;
      }
      return false;
    }

    if (std::strcmp(key, "duration_ms") == 0) {
      char*               parse_end = nullptr;
      const unsigned long parsed    = std::strtoul(value, &parse_end, 10);
      const bool invalid = (parse_end == value) || (*parse_end != '\0') || (parsed == 0UL) || (parsed > 0xFFFFFFFFUL);
      if (invalid) {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_adc_speed_error_invalid_value;
        }
        return false;
      }
      parsed_options.duration_ms           = static_cast<uint32_t>(parsed);
      parsed_options.has_duration_override = true;
      continue;
    }

    if ((std::strcmp(key, "enable_blocking") == 0) || (std::strcmp(key, "blocking") == 0)) {
      bool flag_value = true;
      if (!parse_boolean_flag(value, &flag_value)) {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_adc_speed_error_invalid_value;
        }
        return false;
      }
      parsed_options.enable_blocking       = flag_value;
      parsed_options.has_blocking_override = true;
      continue;
    }

    if ((std::strcmp(key, "enable_irq") == 0) || (std::strcmp(key, "irq") == 0)) {
      bool flag_value = true;
      if (!parse_boolean_flag(value, &flag_value)) {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_adc_speed_error_invalid_value;
        }
        return false;
      }
      parsed_options.enable_irq       = flag_value;
      parsed_options.has_irq_override = true;
      continue;
    }

    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_adc_speed_error_invalid_command;
    }
    return false;
  }

  const char* validation_error = nullptr;
  if (!phoenix_benchmark_adc_speed_validate_options(parsed_options, &validation_error)) {
    if (error_message != nullptr) {
      *error_message =
          (validation_error != nullptr) ? validation_error : k_phoenix_benchmark_adc_speed_error_invalid_value;
    }
    return false;
  }

  *options = parsed_options;
  if (error_message != nullptr) {
    *error_message = nullptr;
  }
  return true;
}

bool determine_command_identifier(const char* line, char* buffer, std::size_t buffer_length, bool* parsing_json_out) {
  if ((line == nullptr) || (buffer == nullptr) || (buffer_length == 0u)) {
    return false;
  }

  const char* trimmed = skip_leading_whitespace(line);
  if ((trimmed == nullptr) || (*trimmed == '\0')) {
    return false;
  }

  const bool parsing_json = (*trimmed == '{');
  if (parsing_json_out != nullptr) {
    *parsing_json_out = parsing_json;
  }

  if (parsing_json) {
    return extract_command_identifier(trimmed, buffer, buffer_length);
  }

  std::size_t index = 0u;
  while ((trimmed[index] != '\0') && !std::isspace(static_cast<unsigned char>(trimmed[index]))) {
    if (index >= (buffer_length - 1u)) {
      return false;
    }
    buffer[index] = trimmed[index];
    ++index;
  }

  if (index == 0u) {
    return false;
  }

  buffer[index] = '\0';
  return true;
}

void print_adc_speed_summary(const PhoenixBenchmarkAdcSpeedExecutionStatus& status) {
  Serial.println();
  Serial.println(F("# summary_table"));

  char line_buffer[k_phoenix_benchmark_adc_speed_summary_buffer_bytes] = {};
  if (!phoenix_benchmark_adc_speed_format_summary_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# summary_table_format_failed"));
    return;
  }
  Serial.println(line_buffer);

  PhoenixBenchmarkAdcSpeedSummaryRowValues blocking_row = {
      .mode_label         = "Blocking",
      .samples_per_second = status.blocking_samples_per_second,
      .loop_microseconds  = status.blocking_loop_microseconds,
      .error_count        = status.blocking_error_count,
      .notes              = status.blocking_note,
      .has_metrics        = status.blocking_executed,
  };
  if (!status.blocking_executed) {
    blocking_row.has_metrics = false;
    blocking_row.notes       = "disabled";
  }

  if (phoenix_benchmark_adc_speed_format_summary_row(blocking_row, line_buffer, sizeof(line_buffer))) {
    Serial.println(line_buffer);
  }
  else {
    Serial.println(F("# summary_table_row_format_failed"));
  }

  PhoenixBenchmarkAdcSpeedSummaryRowValues irq_row = {
      .mode_label         = "IRQ",
      .samples_per_second = status.irq_samples_per_second,
      .loop_microseconds  = status.irq_loop_microseconds,
      .error_count        = status.irq_error_count,
      .notes              = status.irq_note,
      .has_metrics        = status.irq_executed,
  };
  if (!status.irq_executed) {
    irq_row.has_metrics = false;
    irq_row.notes       = "disabled";
  }

  if (phoenix_benchmark_adc_speed_format_summary_row(irq_row, line_buffer, sizeof(line_buffer))) {
    Serial.println(line_buffer);
  }
  else {
    Serial.println(F("# summary_table_row_format_failed"));
  }

  Serial.println();
}

bool execute_channel_map_command(const PhoenixBenchmarkChannelMapOptions& options) {
  // Step 1: Log the invocation so host tooling can correlate outputs.
  Serial.print(F("# running,scenario=channel_map,sweeps="));
  Serial.print(options.sweep_count);
  Serial.print(F(",dwell_us="));
  Serial.print(options.dwell_us);
  Serial.print(F(",wiper_code=0x"));
  if (options.wiper_code < 0x10u) {
    Serial.print('0');
  }
  Serial.println(options.wiper_code, HEX);

  // Step 2: Reset accumulators and announce the run configuration.
  reset_accumulators();
  print_run_header(options);

  // Step 3: Invoke the channel map driver and capture its execution status.
  const PhoenixBenchmarkChannelMapOutputCallbacks callbacks = {serial_print_line, nullptr};
  const PhoenixBenchmarkChannelMapExecutionStatus status =
      phoenix_benchmark_channel_map_run(options, g_state_accumulators, callbacks);

  // Step 4: Handle failures by reporting the error and aborting.
  if (!status.success) {
    Serial.print(F("# error,channel_map_failed"));
    if (status.message != nullptr) {
      Serial.print(F(",reason="));
      Serial.print(status.message);
    }
    Serial.println();
    return false;
  }

  // Step 5: Emit the summary table and call out any warnings discovered.
  print_summary_table();
  if (status.has_warnings) {
    Serial.println(F("# channel_map_warnings,reason=adc_saturation"));
  }
  Serial.println(F("# benchmark_complete"));
  return true;
}

bool execute_adc_speed_command(const PhoenixBenchmarkAdcSpeedOptions& options) {
  Serial.print(F("# running,scenario=adc_speed,duration_ms="));
  Serial.print(options.duration_ms);
  Serial.print(F(",modes="));
  bool printed_mode = false;
  if (options.enable_blocking) {
    Serial.print(F("blocking"));
    printed_mode = true;
  }
  if (options.enable_irq) {
    if (printed_mode) {
      Serial.print('|');
    }
    Serial.print(F("irq"));
  }
  if (!printed_mode && !options.enable_irq) {
    Serial.print(F("none"));
  }
  Serial.println();

  const PhoenixBenchmarkAdcSpeedExecutionStatus status = phoenix_benchmark_adc_speed_run(options, nullptr, 0u);

  if (!status.success) {
    Serial.print(F("# error,adc_speed_failed"));
    if (status.message != nullptr) {
      Serial.print(F(",reason="));
      Serial.print(status.message);
    }
    Serial.println();
    return false;
  }

  print_adc_speed_summary(status);
  if (status.has_warnings) {
    Serial.println(F("# adc_speed_warnings,reason=adc_errors"));
  }
  Serial.println(F("# benchmark_complete"));
  return true;
}

void handle_command_line(const char* line) {
  // Step 1: Reject null commands and restore the ready banner.
  if (line == nullptr) {
    Serial.println(F("# error,null_command"));
    print_ready_banner();
    return;
  }

  // Step 2: Treat an empty line as a request to reprint the ready prompt.
  if (line[0] == '\0') {
    print_ready_banner();
    return;
  }

  // Step 3: Determine the command identifier so we can dispatch to the proper handler.
  char command_identifier[32] = {};
  bool parsing_json           = false;
  if (!determine_command_identifier(line, command_identifier, sizeof(command_identifier), &parsing_json)) {
    Serial.println(F("# error,missing_command_field"));
    Serial.println(F("# ready"));
    return;
  }

  bool handled = false;
  if (std::strcmp(command_identifier, "channel_map") == 0) {
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

    handled = execute_channel_map_command(parse_result.options);
  }
  else if (std::strcmp(command_identifier, "adc_speed") == 0) {
    PhoenixBenchmarkAdcSpeedOptions adc_speed_options = {};
    if (parsing_json) {
      const PhoenixBenchmarkAdcSpeedParseResult parse_result = phoenix_benchmark_adc_speed_parse_command(line);
      if (!parse_result.success) {
        Serial.print(F("# error,adc_speed_parse_failed"));
        if (parse_result.error_message != nullptr) {
          Serial.print(F(",reason="));
          Serial.print(parse_result.error_message);
        }
        Serial.println();
        Serial.println(F("# ready"));
        return;
      }
      adc_speed_options = parse_result.options;
    }
    else {
      const char* parse_error = nullptr;
      if (!parse_adc_speed_plain_command(line, &adc_speed_options, &parse_error)) {
        Serial.print(F("# error,adc_speed_parse_failed"));
        if (parse_error != nullptr) {
          Serial.print(F(",reason="));
          Serial.print(parse_error);
        }
        Serial.println();
        Serial.println(F("# ready"));
        return;
      }
    }

    handled = execute_adc_speed_command(adc_speed_options);
  }
  else {
    Serial.print(F("# error,unknown_command"));
    Serial.print(F(",command="));
    Serial.println(command_identifier);
    Serial.println(F("# ready"));
    return;
  }

  if (!handled) {
    Serial.println(F("# ready"));
    return;
  }

  Serial.println(F("# ready"));
}

}  // namespace

void setup() {
  // Step 1: Initialize serial I/O and wait for the host connection.
  Serial.begin(k_serial_baud_rate);
  wait_for_serial();

  // Step 2: Reset cached driver state and seed baseline defaults.
  phoenix_benchmark_channel_map_reset_state();
  phoenix_benchmark_channel_map_initialise(k_channel_map_defaults);
  phoenix_benchmark_adc_speed_reset_state();
  phoenix_benchmark_adc_speed_initialise(k_adc_speed_defaults);

  // Step 3: Clear previous measurements and present the ready prompt.
  reset_accumulators();
  print_ready_banner();
}

void loop() {
  // Step 1: Accumulate serial bytes until a newline-delimited command is complete.
  static char   command_buffer[k_command_buffer_bytes];
  static size_t buffer_index = 0u;

  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      // Step 1a: Terminate the buffer and hand it to the command handler.
      command_buffer[buffer_index] = '\0';
      handle_command_line(command_buffer);
      buffer_index = 0u;
      continue;
    }

    // Step 1b: Guard against overflow by resetting the buffer when it fills.
    if (buffer_index >= (k_command_buffer_bytes - 1u)) {
      Serial.println(F("# error,command_too_long"));
      buffer_index = 0u;
      continue;
    }

    // Step 1c: Append the character and continue gathering the command.
    command_buffer[buffer_index++] = incoming;
  }
}
