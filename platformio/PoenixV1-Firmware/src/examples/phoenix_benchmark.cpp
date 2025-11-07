#include "adc_hal.hpp"
#include "adc_speed/adc_speed.hpp"
#include "adc_speed/adc_speed_command_parser.hpp"
#include "adc_speed/adc_speed_formatter.hpp"
#include "channel_map/channel_map.hpp"
#include "cold_sweep/cold_sweep.hpp"
#include "cold_sweep/cold_sweep_formatter.hpp"
#include "device_setup.hpp"
#include "drift_capture/drift_capture.hpp"
#include "dwell_sweep/dwell_sweep.hpp"
#include "dwell_sweep/dwell_sweep_formatter.hpp"
#include "osr_sweep/osr_sweep.hpp"
#include "osr_sweep/osr_sweep_formatter.hpp"
#include "pot_sweep/pot_sweep.hpp"
#include "pot_sweep/pot_sweep_formatter.hpp"
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

const PhoenixBenchmarkDriftCaptureDefaults k_drift_capture_defaults = {
    .start_time_us = 0u,
    .end_time_us   = 100000u,
    .step_delay_us = 0u,
    .osr           = 4096u,
    .wiper_code    = k_channel_map_defaults.wiper_code,
};

const PhoenixBenchmarkAdcSpeedDefaults k_adc_speed_defaults = {
    .duration_ms     = 1000u,
    .enable_blocking = true,
    .enable_irq      = true,
};

const PhoenixBenchmarkOsrSweepDefaults k_osr_sweep_defaults = {
    .sweep_count = 100u,
    .dwell_us    = 100u,
    .wiper_code  = 0x00u,
};

const PhoenixBenchmarkPotSweepDefaults k_pot_sweep_defaults = {
    .sweeps_per_wiper = 5u,
    .dwell_us         = 100u,
};

const PhoenixBenchmarkDwellSweepDefaults k_dwell_sweep_defaults = {
    .sweeps_per_dwell = 4u,
    .start_dwell_us   = 100u,
    .end_dwell_us     = 400u,
    .dwell_step_us    = 100u,
};

PhoenixBenchmarkStateAccumulator     g_state_accumulators[k_phoenix_benchmark_channel_map_state_descriptor_count];
PhoenixBenchmarkOsrSweepRowMetrics   g_osr_sweep_rows[k_phoenix_benchmark_osr_value_count];
PhoenixBenchmarkPotSweepRowMetrics   g_pot_sweep_rows[k_phoenix_benchmark_pot_sweep_max_wiper_count];
PhoenixBenchmarkDwellSweepRowMetrics g_dwell_sweep_rows[k_phoenix_benchmark_dwell_sweep_max_step_count];

void reset_osr_sweep_rows(void) {
  for (std::size_t index = 0u; index < k_phoenix_benchmark_osr_value_count; ++index) {
    g_osr_sweep_rows[index] = PhoenixBenchmarkOsrSweepRowMetrics{};
  }
}

void reset_pot_sweep_rows(void) {
  for (std::size_t index = 0u; index < k_phoenix_benchmark_pot_sweep_max_wiper_count; ++index) {
    g_pot_sweep_rows[index] = PhoenixBenchmarkPotSweepRowMetrics{};
  }
}

void reset_dwell_sweep_rows(void) {
  // Step 1: Clear every recorded dwell row so new sweeps start with clean buffers.
  for (std::size_t index = 0u; index < k_phoenix_benchmark_dwell_sweep_max_step_count; ++index) {
    g_dwell_sweep_rows[index] = PhoenixBenchmarkDwellSweepRowMetrics{};
  }
}

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

uint32_t osr_enum_to_value(mcp356x_osr osr_value) {
  switch (osr_value) {
    case mcp356x_osr::osr_32:
      return 32u;
    case mcp356x_osr::osr_64:
      return 64u;
    case mcp356x_osr::osr_128:
      return 128u;
    case mcp356x_osr::osr_256:
      return 256u;
    case mcp356x_osr::osr_512:
      return 512u;
    case mcp356x_osr::osr_1024:
      return 1024u;
    case mcp356x_osr::osr_2048:
      return 2048u;
    case mcp356x_osr::osr_4096:
      return 4096u;
    case mcp356x_osr::osr_8192:
      return 8192u;
    case mcp356x_osr::osr_16384:
      return 16384u;
    case mcp356x_osr::osr_20480:
      return 20480u;
    case mcp356x_osr::osr_24576:
      return 24576u;
    case mcp356x_osr::osr_40960:
      return 40960u;
    case mcp356x_osr::osr_49152:
      return 49152u;
    case mcp356x_osr::osr_81920:
      return 81920u;
    case mcp356x_osr::osr_98304:
      return 98304u;
    default:
      return 0u;
  }
}

void format_osr_label(mcp356x_osr osr_value, char* buffer, std::size_t length) {
  if ((buffer == nullptr) || (length == 0u)) {
    return;
  }
  const uint32_t numeric_value = osr_enum_to_value(osr_value);
  std::snprintf(buffer, length, "OSR%lu", static_cast<unsigned long>(numeric_value));
}

bool print_osr_sweep_summary(const PhoenixBenchmarkOsrSweepRowMetrics* rows, std::size_t row_count) {
  Serial.println();
  Serial.println(F("# summary_table"));

  char line_buffer[k_phoenix_benchmark_osr_sweep_summary_buffer_bytes] = {};
  if (!phoenix_benchmark_osr_sweep_format_summary_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# summary_table_format_failed"));
    return false;
  }
  Serial.println(line_buffer);

  for (std::size_t index = 0u; index < row_count; ++index) {
    const PhoenixBenchmarkOsrSweepRowMetrics& row = rows[index];

    PhoenixBenchmarkOsrSweepSummaryRowValues summary_values   = {};
    char                                     label_buffer[12] = {};
    format_osr_label(row.osr_value, label_buffer, sizeof(label_buffer));
    summary_values.label        = label_buffer;
    summary_values.sample_count = row.drain.channel_a_codes.count;
    summary_values.drain_mean   = row.drain.channel_a_codes.mean;
    summary_values.drain_std    = row.drain.channel_a_codes.standard_deviation();
    summary_values.drain_min    = static_cast<double>(row.drain.channel_a_codes.min_value);
    summary_values.drain_max    = static_cast<double>(row.drain.channel_a_codes.max_value);

    summary_values.led1_mean = row.led1.channel_a_codes.mean;
    summary_values.led1_std  = row.led1.channel_a_codes.standard_deviation();
    summary_values.led1_min  = static_cast<double>(row.led1.channel_a_codes.min_value);
    summary_values.led1_max  = static_cast<double>(row.led1.channel_a_codes.max_value);

    summary_values.led2_mean = row.led2.channel_b_codes.mean;
    summary_values.led2_std  = row.led2.channel_b_codes.standard_deviation();
    summary_values.led2_min  = static_cast<double>(row.led2.channel_b_codes.min_value);
    summary_values.led2_max  = static_cast<double>(row.led2.channel_b_codes.max_value);

    summary_values.sweep_duration_us = row.elapsed_microseconds;
    summary_values.has_metrics = row.drain.channel_a_codes.has_samples() && row.led1.channel_a_codes.has_samples() &&
                                 row.led2.channel_b_codes.has_samples();

    if (!phoenix_benchmark_osr_sweep_format_summary_row(summary_values, line_buffer, sizeof(line_buffer))) {
      Serial.println(F("# summary_table_row_format_failed"));
      continue;
    }
    Serial.println(line_buffer);
  }

  Serial.println();
  return true;
}

uint32_t compute_dwell_step_count(const PhoenixBenchmarkDwellSweepOptions& options) {
  // Step 1: Reject invalid schedules that request a zero microsecond increment.
  if (options.dwell_step_us == 0u) {
    return 0u;
  }
  // Step 2: Guard against ranges that invert the start and end dwell.
  if (options.start_dwell_us > options.end_dwell_us) {
    return 0u;
  }
  // Step 3: Compute the inclusive dwell count using 64-bit math to avoid overflow.
  const uint64_t range = static_cast<uint64_t>(options.end_dwell_us) - static_cast<uint64_t>(options.start_dwell_us);
  const uint64_t increments = range / static_cast<uint64_t>(options.dwell_step_us);
  return static_cast<uint32_t>(increments + 1u);
}

bool print_dwell_sweep_summary(const PhoenixBenchmarkDwellSweepRowMetrics* rows, uint32_t row_count) {
  // Step 1: Emit the summary banner and render the header so users see available columns.
  Serial.println();
  Serial.println(F("# summary_table"));

  char line_buffer[k_phoenix_benchmark_dwell_sweep_summary_buffer_bytes] = {};
  if (!phoenix_benchmark_dwell_sweep_format_summary_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# summary_table_format_failed"));
    return false;
  }
  Serial.println(line_buffer);

  // Step 2: Iterate each dwell row and log the formatted metrics.
  for (uint32_t index = 0u; index < row_count; ++index) {
    const PhoenixBenchmarkDwellSweepRowMetrics& row = rows[index];

    const bool has_metrics = row.drain.channel_a_codes.has_samples() && row.led1.channel_a_codes.has_samples() &&
                             row.led2.channel_b_codes.has_samples();

    PhoenixBenchmarkDwellSweepSummaryRowValues summary_values = {
        .dwell_us         = row.dwell_us,
        .sweeps_completed = row.sweeps_completed,
        .drain_mean       = row.drain.channel_a_codes.mean,
        .drain_std        = row.drain.channel_a_codes.standard_deviation(),
        .led1_mean        = row.led1.channel_a_codes.mean,
        .led1_std         = row.led1.channel_a_codes.standard_deviation(),
        .led2_mean        = row.led2.channel_b_codes.mean,
        .led2_std         = row.led2.channel_b_codes.standard_deviation(),
        .duration_us      = row.elapsed_microseconds,
        .warning_mask     = row.warning_mask,
        .has_metrics      = has_metrics,
    };

    if (!phoenix_benchmark_dwell_sweep_format_summary_row(summary_values, line_buffer, sizeof(line_buffer))) {
      Serial.println(F("# summary_table_row_format_failed"));
      continue;
    }
    Serial.println(line_buffer);
  }

  Serial.println();
  return true;
}

void print_dwell_warning_reasons(uint8_t mask) {
  // Step 1: Emit a printable warning token for each flagged condition.
  bool emitted_reason = false;
  if ((mask & k_phoenix_benchmark_dwell_sweep_warning_saturation) != 0u) {
    Serial.print(F("saturation"));
    emitted_reason = true;
  }
  if ((mask & k_phoenix_benchmark_dwell_sweep_warning_adc_error) != 0u) {
    if (emitted_reason) {
      Serial.print('|');
    }
    Serial.print(F("adc_error"));
    emitted_reason = true;
  }
  if ((mask & k_phoenix_benchmark_dwell_sweep_warning_alignment) != 0u) {
    if (emitted_reason) {
      Serial.print('|');
    }
    Serial.print(F("alignment"));
    emitted_reason = true;
  }
  if (!emitted_reason) {
    Serial.print(F("template_fallback"));
  }
}

bool execute_dwell_sweep_command(const PhoenixBenchmarkDwellSweepOptions& options) {
  const uint32_t expected_steps = compute_dwell_step_count(options);

  // Step 1: Announce the run configuration so host logs capture the dwell schedule.
  Serial.print(F("# running,scenario=dwell_sweep,sweeps_per_dwell="));
  Serial.print(options.sweeps_per_dwell);
  Serial.print(F(",start_us="));
  Serial.print(options.start_dwell_us);
  Serial.print(F(",end_us="));
  Serial.print(options.end_dwell_us);
  Serial.print(F(",step_us="));
  Serial.print(options.dwell_step_us);
  Serial.print(F(",steps="));
  Serial.println(expected_steps);

  // Step 2: Clear previous metrics and invoke the dwell sweep runner.
  reset_dwell_sweep_rows();
  const PhoenixBenchmarkDwellSweepExecutionStatus status =
      phoenix_benchmark_dwell_sweep_run(options, g_dwell_sweep_rows, k_phoenix_benchmark_dwell_sweep_max_step_count);

  // Step 3: Handle execution failures by reporting the error context.
  if (!status.success) {
    Serial.print(F("# error,dwell_sweep_failed"));
    if (status.message != nullptr) {
      Serial.print(F(",reason="));
      Serial.print(status.message);
    }
    Serial.println();
    return false;
  }

  // Step 4: Present the summary table so operators can review dwell performance.
  if (!print_dwell_sweep_summary(g_dwell_sweep_rows, status.rows_generated)) {
    return false;
  }

  // Step 5: Aggregate warning codes to surface any dwell-level issues.
  uint8_t aggregated_warnings = 0u;
  for (uint32_t index = 0u; index < status.rows_generated; ++index) {
    aggregated_warnings |= g_dwell_sweep_rows[index].warning_mask;
  }

  // Step 6: Emit a consolidated warning line when the sweep reported anomalies.
  if (status.has_warnings) {
    Serial.print(F("# dwell_sweep_warnings,reason="));
    if (aggregated_warnings != 0u) {
      print_dwell_warning_reasons(aggregated_warnings);
    }
    else {
      Serial.print(F("template_fallback"));
    }
    Serial.println();
  }

  // Step 7: Mark the scenario complete so host tooling resumes prompt handling.
  Serial.println(F("# benchmark_complete"));
  return true;
}

bool print_pot_sweep_summary(const PhoenixBenchmarkPotSweepRowMetrics* rows, std::size_t row_count) {
  // Step 1: Emit the summary banner and render the header so users see available columns.
  Serial.println();
  Serial.println(F("# summary_table"));

  char line_buffer[k_phoenix_benchmark_pot_sweep_summary_buffer_bytes] = {};
  if (!phoenix_benchmark_pot_sweep_format_summary_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# summary_table_format_failed"));
    return false;
  }
  Serial.println(line_buffer);

  // Step 2: Iterate each recorded wiper row and print the formatted metrics.
  for (std::size_t index = 0u; index < row_count; ++index) {
    const PhoenixBenchmarkPotSweepRowMetrics& row = rows[index];

    PhoenixBenchmarkPotSweepSummaryRowValues summary_values = {
        .wiper_code     = row.wiper_code,
        .led1_max_code  = row.led1_max_code,
        .led2_max_code  = row.led2_max_code,
        .led1_saturated = row.led1_saturated,
        .led2_saturated = row.led2_saturated,
    };

    if (!phoenix_benchmark_pot_sweep_format_summary_row(summary_values, line_buffer, sizeof(line_buffer))) {
      Serial.println(F("# summary_table_row_format_failed"));
      continue;
    }
    Serial.println(line_buffer);
  }

  Serial.println();
  return true;
}

bool execute_pot_sweep_command(const PhoenixBenchmarkPotSweepOptions& options) {
  // Step 1: Announce the run parameters for log correlation.
  Serial.print(F("# running,scenario=pot_sweep,sweeps_per_wiper="));
  Serial.print(options.sweeps_per_wiper);
  Serial.print(F(",dwell_us="));
  Serial.print(options.dwell_us);
  Serial.print(F(",wiper_count="));
  Serial.println(static_cast<unsigned long>(k_phoenix_benchmark_pot_sweep_max_wiper_count));

  // Step 2: Clear prior metrics and invoke the pot sweep driver.
  reset_pot_sweep_rows();
  const PhoenixBenchmarkPotSweepExecutionStatus status =
      phoenix_benchmark_pot_sweep_run(options, g_pot_sweep_rows, k_phoenix_benchmark_pot_sweep_max_wiper_count);

  if (!status.success) {
    Serial.print(F("# error,pot_sweep_failed"));
    if (status.message != nullptr) {
      Serial.print(F(",reason="));
      Serial.print(status.message);
    }
    Serial.println();
    return false;
  }

  // Step 3: Present the summary table so operators can review LED headroom per wiper.
  if (!print_pot_sweep_summary(g_pot_sweep_rows, status.rows_generated)) {
    return false;
  }

  // Step 4: Report recommended wiper selections for each LED when available.
  if (status.led1_recommendation_valid) {
    Serial.print(F("# pot_sweep_recommendation,led=led1,wiper=0x"));
    if (status.led1_recommended_wiper < 0x10u) {
      Serial.print('0');
    }
    Serial.println(status.led1_recommended_wiper, HEX);
  }

  if (status.led2_recommendation_valid) {
    Serial.print(F("# pot_sweep_recommendation,led=led2,wiper=0x"));
    if (status.led2_recommended_wiper < 0x10u) {
      Serial.print('0');
    }
    Serial.println(status.led2_recommended_wiper, HEX);
  }

  // Step 5: Surface warning context when saturation or channel-map issues occurred.
  if (status.has_warnings) {
    bool saturation_detected = false;
    for (std::size_t index = 0u; index < status.rows_generated; ++index) {
      if (g_pot_sweep_rows[index].led1_saturated || g_pot_sweep_rows[index].led2_saturated) {
        saturation_detected = true;
        break;
      }
    }

    Serial.print(F("# pot_sweep_warnings,reason="));
    Serial.println(saturation_detected ? F("saturation") : F("channel_map_warning"));
  }

  Serial.println(F("# benchmark_complete"));
  return true;
}

uint8_t compute_cold_sweep_saturation_mask(const LightReadingsSweepCollection& sweeps) {
  // Step 1: Skip evaluation when no sweeps completed.
  if ((sweeps.sweeps == nullptr) || (sweeps.sweep_count == 0u)) {
    return 0u;
  }

  // Step 2: Aggregate saturation state across each channel.
  uint8_t saturation_mask = 0u;
  for (uint32_t index = 0u; index < sweeps.sweep_count; ++index) {
    const LightReadingsSweepSample& sample = sweeps.sweeps[index];
    if (phoenix_benchmark_is_adc_code_saturated(sample.drain_blue_code)) {
      saturation_mask |= k_phoenix_benchmark_cold_sweep_saturation_drain_blue;
    }
    if (phoenix_benchmark_is_adc_code_saturated(sample.drain_green_code)) {
      saturation_mask |= k_phoenix_benchmark_cold_sweep_saturation_drain_green;
    }
    if (phoenix_benchmark_is_adc_code_saturated(sample.blue_code)) {
      saturation_mask |= k_phoenix_benchmark_cold_sweep_saturation_blue;
    }
    if (phoenix_benchmark_is_adc_code_saturated(sample.green_code)) {
      saturation_mask |= k_phoenix_benchmark_cold_sweep_saturation_green;
    }
  }

  return saturation_mask;
}

bool print_cold_sweep_summary(const LightReadingsSweepStats& stats, uint8_t saturation_mask) {
  // Step 1: Separate the summary from prior logs and emit the banner.
  Serial.println();
  Serial.println(F("# summary_table"));

  char line_buffer[k_phoenix_benchmark_cold_sweep_summary_buffer_bytes] = {};
  if (!phoenix_benchmark_cold_sweep_format_summary_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# summary_table_format_failed"));
    return false;
  }
  Serial.println(line_buffer);

  // Step 2: Print the per-channel metrics so host tooling can digest the results.
  const struct {
    const char*                          label;
    const LightReadingsStatisticSummary* summary;
    uint8_t                              saturation_bit;
  } rows[] = {
      {"drain_blue", &stats.drain_blue, k_phoenix_benchmark_cold_sweep_saturation_drain_blue},
      {"drain_green", &stats.drain_green, k_phoenix_benchmark_cold_sweep_saturation_drain_green},
      {"blue", &stats.blue, k_phoenix_benchmark_cold_sweep_saturation_blue},
      {"green", &stats.green, k_phoenix_benchmark_cold_sweep_saturation_green},
  };

  for (const auto& row : rows) {
    const LightReadingsStatisticSummary&            summary = *row.summary;
    const PhoenixBenchmarkColdSweepSummaryRowValues values  = {
         .label              = row.label,
         .sample_count       = summary.sample_count,
         .mean               = summary.mean,
         .standard_deviation = summary.standard_deviation,
         .min_code           = summary.min_value,
         .max_code           = summary.max_value,
         .has_samples        = summary.has_samples,
         .saturated          = (saturation_mask & row.saturation_bit) != 0u,
    };

    if (!phoenix_benchmark_cold_sweep_format_summary_row(values, line_buffer, sizeof(line_buffer))) {
      Serial.println(F("# summary_table_row_format_failed"));
      continue;
    }

    Serial.println(line_buffer);
  }

  Serial.println();
  return true;
}

bool print_cold_sweep_samples(const LightReadingsSweepCollection& sweeps) {
  // Step 1: Emit the sample banner and render the header for downstream parsing.
  Serial.println(F("# cold_sweep_samples"));

  char line_buffer[k_phoenix_benchmark_cold_sweep_sample_buffer_bytes] = {};
  if (!phoenix_benchmark_cold_sweep_format_sample_header(line_buffer, sizeof(line_buffer))) {
    Serial.println(F("# cold_sweep_sample_header_format_failed"));
    return false;
  }
  Serial.println(line_buffer);

  // Step 2: Handle the edge case where the sweep captured no samples.
  if ((sweeps.sweeps == nullptr) || (sweeps.sweep_count == 0u)) {
    Serial.println(F("# cold_sweep_samples_empty"));
    Serial.println();
    return true;
  }

  // Step 3: Emit each sweep with per-channel codes and saturation metadata.
  for (uint32_t index = 0u; index < sweeps.sweep_count; ++index) {
    const LightReadingsSweepSample& sample = sweeps.sweeps[index];

    uint8_t saturation_mask = 0u;
    if (phoenix_benchmark_is_adc_code_saturated(sample.drain_blue_code)) {
      saturation_mask |= k_phoenix_benchmark_cold_sweep_saturation_drain_blue;
    }
    if (phoenix_benchmark_is_adc_code_saturated(sample.drain_green_code)) {
      saturation_mask |= k_phoenix_benchmark_cold_sweep_saturation_drain_green;
    }
    if (phoenix_benchmark_is_adc_code_saturated(sample.blue_code)) {
      saturation_mask |= k_phoenix_benchmark_cold_sweep_saturation_blue;
    }
    if (phoenix_benchmark_is_adc_code_saturated(sample.green_code)) {
      saturation_mask |= k_phoenix_benchmark_cold_sweep_saturation_green;
    }

    const PhoenixBenchmarkColdSweepSampleRowValues values = {
        .sweep_index      = index,
        .drain_blue_code  = sample.drain_blue_code,
        .drain_green_code = sample.drain_green_code,
        .blue_code        = sample.blue_code,
        .green_code       = sample.green_code,
        .saturation_mask  = saturation_mask,
    };

    if (!phoenix_benchmark_cold_sweep_format_sample_row(values, line_buffer, sizeof(line_buffer))) {
      Serial.print(F("# cold_sweep_sample_row_format_failed,index="));
      Serial.println(index);
      continue;
    }

    Serial.println(line_buffer);
  }

  Serial.println();
  return true;
}

void print_cold_sweep_warning_reasons(uint8_t mask) {
  // Step 1: Surface warning tokens for host logs.
  bool emitted_reason = false;
  if ((mask & k_phoenix_benchmark_cold_sweep_warning_saturation) != 0u) {
    Serial.print(F("saturation"));
    emitted_reason = true;
  }

  if (!emitted_reason) {
    Serial.print(F("template_fallback"));
  }
}

bool execute_cold_sweep_command(const PhoenixBenchmarkColdSweepOptions& options) {
  // Step 1: Announce the run configuration chosen for the sweep.
  const uint32_t baseline_blue_dwell_us  = g_device_light_readings_config.blue_channel.dwell_us;
  const uint32_t baseline_green_dwell_us = g_device_light_readings_config.green_channel.dwell_us;
  const bool     override_dwell          = options.has_dwell_override;
  const uint32_t applied_blue_dwell_us   = override_dwell ? options.dwell_override_us : baseline_blue_dwell_us;
  const uint32_t applied_green_dwell_us  = override_dwell ? options.dwell_override_us : baseline_green_dwell_us;
  const uint32_t requested_sweeps = options.has_sweep_override ? options.sweep_count : LIGHT_READINGS_MAX_SWEEP_COUNT;

  Serial.print(F("# running,scenario=cold_sweep,sweeps="));
  Serial.print(requested_sweeps);
  Serial.print(F(",dwell_blue_us="));
  Serial.print(applied_blue_dwell_us);
  Serial.print(F(",dwell_green_us="));
  Serial.println(applied_green_dwell_us);

  // Step 2: Execute the sweep and capture statistics.
  LightReadingsSweepCollection sweep_collection = {
      .sweep_count = 0u,
      .sweeps      = g_light_readings_sweep_storage,
  };
  LightReadingsSweepStats stats = {};

  const PhoenixBenchmarkColdSweepExecutionStatus status =
      phoenix_benchmark_cold_sweep_run(options, &sweep_collection, &stats);

  if (!status.success) {
    Serial.print(F("# error,cold_sweep_failed"));
    if (status.message != nullptr) {
      Serial.print(F(",reason="));
      Serial.print(status.message);
    }
    Serial.println();
    return false;
  }

  Serial.print(F("# cold_sweep_metadata,captured_sweeps="));
  Serial.print(status.captured_sweeps);
  Serial.print(F(",timestamp_us="));
  Serial.println(status.timestamp_us);

  const uint8_t saturation_mask = compute_cold_sweep_saturation_mask(sweep_collection);

  // Step 3: Emit summary statistics and per-sample output for host tooling.
  if (!print_cold_sweep_summary(stats, saturation_mask)) {
    return false;
  }
  if (!print_cold_sweep_samples(sweep_collection)) {
    return false;
  }

  // Step 4: Surface warning reasons when the driver reported anomalies.
  if (status.has_warnings) {
    Serial.print(F("# cold_sweep_warnings,reason="));
    print_cold_sweep_warning_reasons(status.warning_mask);
    Serial.println();
  }

  Serial.println(F("# benchmark_complete"));
  return true;
}

bool execute_osr_sweep_command(const PhoenixBenchmarkOsrSweepOptions& options) {
  Serial.print(F("# running,scenario=osr_sweep,pot="));
  Serial.print(options.wiper_code);
  Serial.print(F(",dwell_us="));
  Serial.print(options.dwell_us);
  Serial.print(F(",sweeps="));
  Serial.println(options.sweep_count);

  reset_osr_sweep_rows();
  const PhoenixBenchmarkOsrSweepExecutionStatus status =
      phoenix_benchmark_osr_sweep_run(options, g_osr_sweep_rows, k_phoenix_benchmark_osr_value_count);

  if (!status.success) {
    Serial.print(F("# error,osr_sweep_failed"));
    if (status.message != nullptr) {
      Serial.print(F(",reason="));
      Serial.print(status.message);
    }
    Serial.println();
    return false;
  }

  if (!print_osr_sweep_summary(g_osr_sweep_rows, status.rows_generated)) {
    return false;
  }

  if (status.has_warnings) {
    Serial.println(F("# osr_sweep_warnings,reason=channel_map_warning"));
  }

  Serial.println(F("# benchmark_complete"));
  return true;
}

bool execute_drift_capture_command(const PhoenixBenchmarkDriftCaptureOptions& options) {
  Serial.print(F("# running,scenario=drift_capture,start_us="));
  Serial.print(options.start_time_us);
  Serial.print(F(",end_us="));
  Serial.print(options.end_time_us);
  Serial.print(F(",step_delay_us="));
  Serial.print(options.step_delay_us);
  Serial.print(F(",osr="));
  Serial.print(options.osr);
  Serial.print(F(",wiper_code=0x"));
  if (options.wiper_code < 0x10u) {
    Serial.print('0');
  }
  Serial.println(options.wiper_code, HEX);

  const PhoenixBenchmarkDriftCaptureOutputCallbacks callbacks = {serial_print_line};
  const PhoenixBenchmarkDriftCaptureExecutionStatus status    = phoenix_benchmark_drift_capture_run(options, callbacks);

  if (!status.success) {
    Serial.print(F("# error,drift_capture_failed"));
    if (status.message != nullptr) {
      Serial.print(F(",reason="));
      Serial.print(status.message);
    }
    Serial.println();
    return false;
  }

  if (status.has_warnings) {
    Serial.print(F("# drift_capture_warnings,mask=0x"));
    Serial.println(status.warning_mask, HEX);
  }

  Serial.println(F("# benchmark_complete"));
  return true;
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
  else if (std::strcmp(command_identifier, "pot_sweep") == 0) {
    const PhoenixBenchmarkPotSweepParseResult parse_result = phoenix_benchmark_pot_sweep_parse_command(line);
    if (!parse_result.success) {
      Serial.print(F("# error,pot_sweep_parse_failed"));
      if (parse_result.error_message != nullptr) {
        Serial.print(F(",reason="));
        Serial.print(parse_result.error_message);
      }
      Serial.println();
      Serial.println(F("# ready"));
      return;
    }

    handled = execute_pot_sweep_command(parse_result.options);
  }
  else if (std::strcmp(command_identifier, "dwell_sweep") == 0) {
    const PhoenixBenchmarkDwellSweepParseResult parse_result = phoenix_benchmark_dwell_sweep_parse_command(line);
    if (!parse_result.success) {
      Serial.print(F("# error,dwell_sweep_parse_failed"));
      if (parse_result.error_message != nullptr) {
        Serial.print(F(",reason="));
        Serial.print(parse_result.error_message);
      }
      Serial.println();
      Serial.println(F("# ready"));
      return;
    }

    handled = execute_dwell_sweep_command(parse_result.options);
  }
  else if (std::strcmp(command_identifier, "osr_sweep") == 0) {
    const PhoenixBenchmarkOsrSweepParseResult parse_result = phoenix_benchmark_osr_sweep_parse_command(line);
    if (!parse_result.success) {
      Serial.print(F("# error,osr_sweep_parse_failed"));
      if (parse_result.error_message != nullptr) {
        Serial.print(F(",reason="));
        Serial.print(parse_result.error_message);
      }
      Serial.println();
      Serial.println(F("# ready"));
      return;
    }

    handled = execute_osr_sweep_command(parse_result.options);
  }
  else if (std::strcmp(command_identifier, "drift_capture") == 0) {
    const PhoenixBenchmarkDriftCaptureParseResult parse_result = phoenix_benchmark_drift_capture_parse_command(line);
    if (!parse_result.success) {
      Serial.print(F("# error,drift_capture_parse_failed"));
      if (parse_result.error_message != nullptr) {
        Serial.print(F(",reason="));
        Serial.print(parse_result.error_message);
      }
      Serial.println();
      Serial.println(F("# ready"));
      return;
    }

    handled = execute_drift_capture_command(parse_result.options);
  }
  else if (std::strcmp(command_identifier, "cold_sweep") == 0) {
    const PhoenixBenchmarkColdSweepParseResult parse_result = phoenix_benchmark_cold_sweep_parse_command(line);
    if (!parse_result.success) {
      Serial.print(F("# error,cold_sweep_parse_failed"));
      if (parse_result.error_message != nullptr) {
        Serial.print(F(",reason="));
        Serial.print(parse_result.error_message);
      }
      Serial.println();
      Serial.println(F("# ready"));
      return;
    }

    handled = execute_cold_sweep_command(parse_result.options);
  }
  else if (std::strcmp(command_identifier, "led_on_drift") == 0) {
    const PhoenixBenchmarkDriftCaptureParseResult parse_result =
        phoenix_benchmark_drift_capture_parse_command("drift_capture");
    if (!parse_result.success) {
      Serial.println(F("# error,drift_capture_defaults_unavailable"));
      Serial.println(F("# ready"));
      return;
    }

    handled = execute_drift_capture_command(parse_result.options);
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
  phoenix_benchmark_osr_sweep_reset_state();
  phoenix_benchmark_osr_sweep_initialise(k_osr_sweep_defaults);
  phoenix_benchmark_dwell_sweep_reset_state();
  phoenix_benchmark_dwell_sweep_initialise(k_dwell_sweep_defaults);
  phoenix_benchmark_pot_sweep_reset_state();
  phoenix_benchmark_pot_sweep_initialise(k_pot_sweep_defaults);
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise(k_drift_capture_defaults);
  phoenix_benchmark_cold_sweep_reset_state();

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
