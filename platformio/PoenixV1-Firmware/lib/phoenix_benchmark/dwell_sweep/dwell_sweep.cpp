#include "dwell_sweep.hpp"

#include "../channel_map/channel_map.hpp"
#include "../channel_map/channel_map_support.hpp"
#include "../core/phoenix_benchmark_core.hpp"
#include <Arduino.h>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

constexpr PhoenixBenchmarkDwellSweepDefaults k_default_dwell_sweep_defaults = {
    .sweeps_per_dwell = 4u,
    .start_dwell_us   = 100u,
    .end_dwell_us     = 400u,
    .dwell_step_us    = 100u,
};

constexpr uint32_t k_dwell_us_limit          = 5000000u;
constexpr uint32_t k_max_sweeps_per_dwell    = 1000u;
constexpr uint64_t k_max_total_dwell_product = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
constexpr double   k_minimum_alignment_delta = 5.0;

constexpr const char k_error_invalid_arguments[]       = "invalid arguments";
constexpr const char k_error_invalid_command[]         = "invalid command";
constexpr const char k_error_invalid_value[]           = "invalid value";
constexpr const char k_error_invalid_options[]         = "invalid options";
constexpr const char k_error_hardware_initialisation[] = "hardware initialisation failed";
constexpr const char k_error_channel_map_failed[]      = "channel_map failed";

PhoenixBenchmarkDwellSweepDefaults g_dwell_defaults = k_default_dwell_sweep_defaults;

PhoenixBenchmarkChannelMapExecutionStatus (*g_channel_map_runner)(
    const PhoenixBenchmarkChannelMapOptions&, PhoenixBenchmarkStateAccumulator*,
    const PhoenixBenchmarkChannelMapOutputCallbacks&) = phoenix_benchmark_channel_map_run;

bool (*g_hardware_ready_checker)(void) = phoenix_benchmark_channel_map_ensure_hardware_ready;
uint32_t (*g_micros_provider)(void)    = ::micros;

uint32_t compute_elapsed_time(uint32_t start, uint32_t end) {
  if (end >= start) {
    return end - start;
  }
  return (0xFFFFFFFFu - start) + end + 1u;
}

uint32_t compute_step_count(uint32_t start, uint32_t end, uint32_t step) {
  if (step == 0u) {
    return 0u;
  }
  if (start > end) {
    return 0u;
  }
  const uint64_t range       = static_cast<uint64_t>(end) - static_cast<uint64_t>(start);
  const uint64_t increments  = range / static_cast<uint64_t>(step);
  const uint64_t total_steps = increments + 1u;
  if (total_steps > std::numeric_limits<uint32_t>::max()) {
    return 0u;
  }
  return static_cast<uint32_t>(total_steps);
}

bool parse_unsigned_value(const char* text, uint32_t* value_out, char** end_out) {
  if ((text == nullptr) || (value_out == nullptr)) {
    return false;
  }

  while ((*text != '\0') && std::isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }

  errno                = 0;
  char*         end    = nullptr;
  unsigned long parsed = std::strtoul(text, &end, 10);
  if ((end == text) || (errno == ERANGE) ||
      (parsed > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max()))) {
    return false;
  }

  *value_out = static_cast<uint32_t>(parsed);
  if (end_out != nullptr) {
    *end_out = end;
  }
  return true;
}

bool find_parameters_block(const char* json, const char** block_start, const char** block_end) {
  if ((json == nullptr) || (block_start == nullptr) || (block_end == nullptr)) {
    return false;
  }
  const char* token = std::strstr(json, "\"parameters\"");
  if (token == nullptr) {
    *block_start = nullptr;
    *block_end   = nullptr;
    return true;
  }
  const char* open = std::strchr(token, '{');
  if (open == nullptr) {
    return false;
  }
  int         depth  = 1;
  const char* cursor = open + 1;
  while ((*cursor != '\0') && (depth > 0)) {
    if (*cursor == '{') {
      ++depth;
    }
    else if (*cursor == '}') {
      --depth;
      if (depth == 0) {
        *block_start = open + 1;
        *block_end   = cursor;
        return true;
      }
    }
    ++cursor;
  }
  return false;
}

bool parse_optional_field(const char* block_start, const char* block_end, const char* key, uint32_t* value_out,
                          bool* present_out) {
  if ((block_start == nullptr) || (block_end == nullptr) || (key == nullptr) || (value_out == nullptr)) {
    return false;
  }

  const std::size_t key_length = std::strlen(key);
  const char*       match      = std::strstr(block_start, key);
  if ((match == nullptr) || (match >= block_end)) {
    if (present_out != nullptr) {
      *present_out = false;
    }
    return true;
  }

  if (present_out != nullptr) {
    *present_out = false;
  }

  const char* after_key = match + key_length;
  if (after_key >= block_end) {
    return false;
  }

  const char* colon = std::strchr(after_key, ':');
  if ((colon == nullptr) || (colon >= block_end)) {
    return false;
  }
  ++colon;

  char*    value_end = nullptr;
  uint32_t parsed    = 0u;
  if (!parse_unsigned_value(colon, &parsed, &value_end)) {
    return false;
  }
  if ((value_end != nullptr) && (value_end > block_end)) {
    return false;
  }

  *value_out = parsed;
  if (present_out != nullptr) {
    *present_out = true;
  }
  return true;
}

struct ChannelMapTemplateResult {
  PhoenixBenchmarkChannelMapOptions options;
  bool                              success;
};

ChannelMapTemplateResult load_channel_map_template(void) {
  const PhoenixBenchmarkChannelMapParseResult parse_result =
      phoenix_benchmark_channel_map_parse_command("{\"command\":\"channel_map\"}");
  if (parse_result.success) {
    PhoenixBenchmarkChannelMapOptions options = parse_result.options;
    options.has_sweep_override                = false;
    options.has_dwell_override                = false;
    options.has_wiper_override                = false;
    return {options, true};
  }

  PhoenixBenchmarkChannelMapOptions fallback = {};
  fallback.sweep_count                       = 1u;
  fallback.has_sweep_override                = true;
  fallback.dwell_us                          = 100u;
  fallback.has_dwell_override                = true;
  fallback.wiper_code                        = 0x00u;
  fallback.has_wiper_override                = true;
  return {fallback, false};
}

}  // namespace

void PhoenixBenchmarkDwellSweepOptions::apply_defaults(const PhoenixBenchmarkDwellSweepDefaults& defaults) {
  if (!has_sweeps_override) {
    sweeps_per_dwell = defaults.sweeps_per_dwell;
  }
  if (!has_start_override) {
    start_dwell_us = defaults.start_dwell_us;
  }
  if (!has_end_override) {
    end_dwell_us = defaults.end_dwell_us;
  }
  if (!has_step_override) {
    dwell_step_us = defaults.dwell_step_us;
  }
}

bool PhoenixBenchmarkDwellSweepOptions::validate(const char** error_message) const {
  const char* message = nullptr;

  if ((sweeps_per_dwell == 0u) || (sweeps_per_dwell > k_max_sweeps_per_dwell)) {
    message = "sweeps_per_dwell out of range";
  }
  else if (start_dwell_us > k_dwell_us_limit) {
    message = "start_dwell_us exceeds limit";
  }
  else if (end_dwell_us > k_dwell_us_limit) {
    message = "end_dwell_us exceeds limit";
  }
  else if (start_dwell_us > end_dwell_us) {
    message = "start_dwell_us exceeds end_dwell_us";
  }
  else if (dwell_step_us == 0u) {
    message = "dwell_step_us must be non-zero";
  }
  else {
    const uint32_t step_count = compute_step_count(start_dwell_us, end_dwell_us, dwell_step_us);
    if ((step_count == 0u) || (step_count > k_phoenix_benchmark_dwell_sweep_max_step_count)) {
      message = "dwell_step_us produces too many steps";
    }
    else {
      const uint64_t dwell_product = static_cast<uint64_t>(sweeps_per_dwell) * static_cast<uint64_t>(end_dwell_us);
      if (dwell_product > k_max_total_dwell_product) {
        message = "sweep duration exceeds timer capacity";
      }
    }
  }

  if (error_message != nullptr) {
    *error_message = message;
  }
  return message == nullptr;
}

void phoenix_benchmark_dwell_sweep_initialise(const PhoenixBenchmarkDwellSweepDefaults& defaults) {
  g_dwell_defaults = defaults;
}

void phoenix_benchmark_dwell_sweep_reset_state(void) {
  g_dwell_defaults = k_default_dwell_sweep_defaults;
  phoenix_benchmark_dwell_sweep_clear_test_hooks();
}

PhoenixBenchmarkDwellSweepParseResult phoenix_benchmark_dwell_sweep_parse_command(const char* line) {
  PhoenixBenchmarkDwellSweepOptions options = {};
  options.apply_defaults(g_dwell_defaults);

  if (line == nullptr) {
    return {false, options, k_error_invalid_arguments};
  }

  // Step 1: Trim leading whitespace to detect bare commands.
  const char* cursor = line;
  while ((*cursor != '\0') && std::isspace(static_cast<unsigned char>(*cursor))) {
    ++cursor;
  }

  PhoenixBenchmarkDwellSweepOptions parsed = {};
  parsed.apply_defaults(g_dwell_defaults);

  if (*cursor == '\0') {
    return {false, options, k_error_invalid_command};
  }

  if (*cursor != '{') {
    if (std::strncmp(cursor, "dwell_sweep", 11u) == 0) {
      const char* after_token = cursor + 11u;
      while ((*after_token != '\0') && std::isspace(static_cast<unsigned char>(*after_token))) {
        ++after_token;
      }
      if (*after_token != '\0') {
        return {false, options, k_error_invalid_command};
      }

      parsed.has_sweeps_override = false;
      parsed.has_start_override  = false;
      parsed.has_end_override    = false;
      parsed.has_step_override   = false;

      const char* validation_message = nullptr;
      if (!parsed.validate(&validation_message)) {
        return {false, parsed, (validation_message != nullptr) ? validation_message : k_error_invalid_options};
      }
      return {true, parsed, nullptr};
    }
    return {false, options, k_error_invalid_command};
  }

  // Step 2: Ensure the command token targets dwell_sweep.
  const char* command_token = std::strstr(cursor, "\"command\"");
  if (command_token == nullptr) {
    return {false, options, k_error_invalid_command};
  }
  const char* command_value = std::strchr(command_token, ':');
  if (command_value == nullptr) {
    return {false, options, k_error_invalid_command};
  }
  ++command_value;
  while ((*command_value != '\0') && std::isspace(static_cast<unsigned char>(*command_value))) {
    ++command_value;
  }
  if (*command_value != '"') {
    return {false, options, k_error_invalid_command};
  }
  ++command_value;
  const char* command_end = std::strchr(command_value, '"');
  if ((command_end == nullptr) ||
      (std::strncmp(command_value, "dwell_sweep", static_cast<std::size_t>(command_end - command_value)) != 0)) {
    return {false, options, k_error_invalid_command};
  }

  // Step 3: Parse optional parameters when present.
  const char* parameters_start = nullptr;
  const char* parameters_end   = nullptr;
  if (!find_parameters_block(cursor, &parameters_start, &parameters_end)) {
    return {false, options, k_error_invalid_command};
  }

  if ((parameters_start != nullptr) && (parameters_end != nullptr)) {
    uint32_t parsed_value = 0u;

    bool field_present = false;
    if (!parse_optional_field(parameters_start, parameters_end, "\"sweeps_per_dwell\"", &parsed_value,
                              &field_present)) {
      return {false, options, k_error_invalid_value};
    }
    if (field_present) {
      parsed.sweeps_per_dwell    = parsed_value;
      parsed.has_sweeps_override = true;
    }

    if (!parse_optional_field(parameters_start, parameters_end, "\"start_dwell_us\"", &parsed_value, &field_present)) {
      return {false, options, k_error_invalid_value};
    }
    if (field_present) {
      parsed.start_dwell_us     = parsed_value;
      parsed.has_start_override = true;
    }

    if (!parse_optional_field(parameters_start, parameters_end, "\"end_dwell_us\"", &parsed_value, &field_present)) {
      return {false, options, k_error_invalid_value};
    }
    if (field_present) {
      parsed.end_dwell_us     = parsed_value;
      parsed.has_end_override = true;
    }

    if (!parse_optional_field(parameters_start, parameters_end, "\"dwell_step_us\"", &parsed_value, &field_present)) {
      return {false, options, k_error_invalid_value};
    }
    if (field_present) {
      parsed.dwell_step_us     = parsed_value;
      parsed.has_step_override = true;
    }
  }

  const char* validation_message = nullptr;
  if (!parsed.validate(&validation_message)) {
    return {false, parsed, (validation_message != nullptr) ? validation_message : k_error_invalid_options};
  }

  return {true, parsed, nullptr};
}

PhoenixBenchmarkDwellSweepExecutionStatus phoenix_benchmark_dwell_sweep_run(
    const PhoenixBenchmarkDwellSweepOptions& input_options, PhoenixBenchmarkDwellSweepRowMetrics* rows,
    std::size_t row_capacity) {
  PhoenixBenchmarkDwellSweepExecutionStatus status = {false, false, nullptr, 0u};

  if ((rows == nullptr) || (row_capacity == 0u)) {
    status.message = k_error_invalid_arguments;
    return status;
  }

  PhoenixBenchmarkDwellSweepOptions options = input_options;
  options.apply_defaults(g_dwell_defaults);

  const char* validation_message = nullptr;
  if (!options.validate(&validation_message)) {
    status.message = (validation_message != nullptr) ? validation_message : k_error_invalid_options;
    return status;
  }

  const uint32_t expected_steps =
      compute_step_count(options.start_dwell_us, options.end_dwell_us, options.dwell_step_us);
  if ((expected_steps == 0u) || (row_capacity < expected_steps)) {
    status.message = k_error_invalid_arguments;
    return status;
  }

  if (!g_hardware_ready_checker || !g_hardware_ready_checker()) {
    status.message = k_error_hardware_initialisation;
    return status;
  }

  const ChannelMapTemplateResult    template_result      = load_channel_map_template();
  PhoenixBenchmarkChannelMapOptions channel_map_template = template_result.options;

  PhoenixBenchmarkChannelMapOutputCallbacks callbacks = {nullptr, nullptr};
  status.has_warnings                                 = !template_result.success;

  static PhoenixBenchmarkStateAccumulator accumulators[k_phoenix_benchmark_channel_map_state_descriptor_count];

  uint32_t rows_generated = 0u;
  uint32_t dwell_value    = options.start_dwell_us;

  while (true) {
    if (rows_generated >= expected_steps) {
      break;
    }

    for (std::size_t index = 0u; index < k_phoenix_benchmark_channel_map_state_descriptor_count; ++index) {
      accumulators[index] = PhoenixBenchmarkStateAccumulator{};
    }

    PhoenixBenchmarkChannelMapOptions map_options = channel_map_template;
    map_options.sweep_count                       = options.sweeps_per_dwell;
    map_options.has_sweep_override                = true;
    map_options.dwell_us                          = dwell_value;
    map_options.has_dwell_override                = true;

    const uint32_t                                  start_micros = g_micros_provider();
    const PhoenixBenchmarkChannelMapExecutionStatus run_status =
        g_channel_map_runner(map_options, accumulators, callbacks);
    const uint32_t end_micros = g_micros_provider();

    PhoenixBenchmarkDwellSweepRowMetrics& row = rows[rows_generated];
    row                                       = PhoenixBenchmarkDwellSweepRowMetrics{};
    row.dwell_us                              = dwell_value;
    row.sweeps_requested                      = options.sweeps_per_dwell;
    row.elapsed_microseconds                  = compute_elapsed_time(start_micros, end_micros);
    row.drain                                 = accumulators[k_phoenix_benchmark_channel_map_drain_state_index];
    row.led1                                  = accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 1u];
    row.led2                                  = accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 2u];

    bool row_has_warnings = false;

    if (run_status.has_warnings) {
      row.warning_mask |= k_phoenix_benchmark_dwell_sweep_warning_adc_error;
      row_has_warnings = true;
    }

    if ((row.drain.channel_a_saturation_count > 0u) || (row.drain.channel_b_saturation_count > 0u) ||
        (row.led1.channel_a_saturation_count > 0u) || (row.led1.channel_b_saturation_count > 0u) ||
        (row.led2.channel_a_saturation_count > 0u) || (row.led2.channel_b_saturation_count > 0u)) {
      row.warning_mask |= k_phoenix_benchmark_dwell_sweep_warning_saturation;
      row_has_warnings = true;
    }

    PhoenixBenchmarkChannel       dominant_channel = PhoenixBenchmarkChannel::kUnknown;
    const PhoenixBenchmarkChannel led1_channel =
        phoenix_benchmark_channel_map_determine_dominant_channel(row.drain, row.led1, k_minimum_alignment_delta);
    if (led1_channel == PhoenixBenchmarkChannel::kChannelA) {
      dominant_channel = PhoenixBenchmarkChannel::kChannelA;
    }

    const PhoenixBenchmarkChannel led2_channel =
        phoenix_benchmark_channel_map_determine_dominant_channel(row.drain, row.led2, k_minimum_alignment_delta);
    if ((dominant_channel == PhoenixBenchmarkChannel::kUnknown) &&
        (led2_channel == PhoenixBenchmarkChannel::kChannelB)) {
      dominant_channel = PhoenixBenchmarkChannel::kChannelB;
    }

    row.dominant_channel = dominant_channel;
    if (dominant_channel == PhoenixBenchmarkChannel::kUnknown) {
      row.warning_mask |= k_phoenix_benchmark_dwell_sweep_warning_alignment;
      row_has_warnings = true;
    }

    if (!run_status.success) {
      row.sweeps_completed  = 0u;
      row.error_message     = run_status.message;
      status.success        = false;
      status.message        = (run_status.message != nullptr) ? run_status.message : k_error_channel_map_failed;
      status.has_warnings   = status.has_warnings || row_has_warnings;
      status.rows_generated = rows_generated + 1u;
      return status;
    }

    row.sweeps_completed = options.sweeps_per_dwell;
    row.error_message    = nullptr;
    status.has_warnings  = status.has_warnings || row_has_warnings;

    ++rows_generated;

    if (dwell_value >= options.end_dwell_us) {
      break;
    }

    const uint64_t next_dwell = static_cast<uint64_t>(dwell_value) + static_cast<uint64_t>(options.dwell_step_us);
    if (next_dwell > static_cast<uint64_t>(options.end_dwell_us)) {
      dwell_value = options.end_dwell_us;
    }
    else {
      dwell_value = static_cast<uint32_t>(next_dwell);
    }
  }

  status.success        = true;
  status.rows_generated = rows_generated;
  status.message        = nullptr;
  return status;
}

void phoenix_benchmark_dwell_sweep_set_channel_map_runner_for_test(PhoenixBenchmarkChannelMapExecutionStatus (*runner)(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks)) {
  g_channel_map_runner = (runner != nullptr) ? runner : phoenix_benchmark_channel_map_run;
}

void phoenix_benchmark_dwell_sweep_set_hardware_ready_checker_for_test(bool (*checker)(void)) {
  g_hardware_ready_checker = (checker != nullptr) ? checker : phoenix_benchmark_channel_map_ensure_hardware_ready;
}

void phoenix_benchmark_dwell_sweep_set_micros_provider_for_test(uint32_t (*provider)(void)) {
  g_micros_provider = (provider != nullptr) ? provider : ::micros;
}

void phoenix_benchmark_dwell_sweep_clear_test_hooks(void) {
  g_channel_map_runner     = phoenix_benchmark_channel_map_run;
  g_hardware_ready_checker = phoenix_benchmark_channel_map_ensure_hardware_ready;
  g_micros_provider        = ::micros;
}
