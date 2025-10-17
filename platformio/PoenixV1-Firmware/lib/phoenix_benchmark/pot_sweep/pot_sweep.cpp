#include "pot_sweep.hpp"

#include "../../mcp356x/mcp356x.hpp"
#include "../channel_map/channel_map.hpp"
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

constexpr PhoenixBenchmarkPotSweepDefaults k_default_pot_sweep_defaults = {
    .sweeps_per_wiper = 5u,
    .dwell_us         = 100u,
};

// 90% of the 24-bit full-scale ADC code (0.9 * 8,388,607) defines the saturation boundary.
constexpr int32_t k_pot_sweep_saturation_threshold = 7549746;

constexpr const char* k_error_invalid_arguments = "invalid arguments";
constexpr const char* k_error_invalid_command   = "invalid command";
constexpr const char* k_error_invalid_value     = "invalid value";
constexpr const char* k_error_invalid_options   = "invalid options";
constexpr const char* k_error_hardware_init     = "hardware initialisation failed";
constexpr const char* k_error_channel_map_run   = "channel_map failed";
constexpr const char* k_error_adc_defaults      = "adc default configuration failed";

typedef PhoenixBenchmarkChannelMapExecutionStatus (*ChannelMapRunner)(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks);

typedef bool (*HardwareReadyChecker)(void);
typedef int (*AdcConfigurator)(void);

PhoenixBenchmarkPotSweepDefaults g_pot_sweep_defaults     = k_default_pot_sweep_defaults;
ChannelMapRunner                 g_channel_map_runner     = phoenix_benchmark_channel_map_run;
HardwareReadyChecker             g_hardware_ready_checker = phoenix_benchmark_channel_map_ensure_hardware_ready;
AdcConfigurator                  g_adc_default_config     = mcp356x_apply_default_config;

const char* skip_whitespace(const char* cursor) {
  if (cursor == nullptr) {
    return nullptr;
  }
  while ((*cursor != '\0') && std::isspace(static_cast<unsigned char>(*cursor))) {
    ++cursor;
  }
  return cursor;
}

bool parse_unsigned_value(const char* cursor, int base, uint32_t* value_out, const char** end_out) {
  if ((cursor == nullptr) || (value_out == nullptr)) {
    return false;
  }

  if (*cursor == '-') {
    return false;
  }

  errno                       = 0;
  char*               end_ptr = nullptr;
  const unsigned long parsed  = std::strtoul(cursor, &end_ptr, base);
  if (end_ptr == cursor) {
    return false;
  }
  if ((errno == ERANGE) || (parsed > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max()))) {
    return false;
  }

  *value_out = static_cast<uint32_t>(parsed);
  if (end_out != nullptr) {
    *end_out = end_ptr;
  }
  return true;
}

struct ChannelMapTemplateResult {
  PhoenixBenchmarkChannelMapOptions options;
  bool                              success;
};

ChannelMapTemplateResult load_channel_map_template(void) {
  // Step 1: Attempt to clone the live channel-map defaults so we mirror current settings.
  const PhoenixBenchmarkChannelMapParseResult parse_result = phoenix_benchmark_channel_map_parse_command(
      "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":1,\"wiper_code\":0}}");
  if (parse_result.success) {
    PhoenixBenchmarkChannelMapOptions baseline = parse_result.options;
    baseline.has_sweep_override                = false;
    baseline.has_wiper_override                = false;
    return {baseline, true};
  }

  // Step 2: Fall back to a safe template when parsing fails so pot sweep remains operational.
  PhoenixBenchmarkChannelMapOptions fallback = {};
  fallback.sweep_count                       = 1u;
  fallback.has_sweep_override                = true;
  fallback.dwell_us           = 100u;  // 100 us dwell chosen to mirror channel_map defaults (docs/style-guide).
  fallback.has_dwell_override = true;
  fallback.wiper_code         = 0x00u;
  fallback.has_wiper_override = true;
  return {fallback, false};
}

}  // namespace

void PhoenixBenchmarkPotSweepOptions::apply_defaults(const PhoenixBenchmarkPotSweepDefaults& defaults) {
  // Step 1: Adopt the default sweep count when the caller left it unset.
  if (!has_sweeps_override) {
    sweeps_per_wiper = defaults.sweeps_per_wiper;
  }

  // Step 2: Mirror the default dwell time unless the caller provided an override.
  if (!has_dwell_override) {
    dwell_us = defaults.dwell_us;
  }
}

bool PhoenixBenchmarkPotSweepOptions::validate(const char** error_message) const {
  const char* message = nullptr;

  // Step 1: Guard against zero sweeps or excessive dwell times.
  if (sweeps_per_wiper == 0u) {
    message = "sweeps_per_wiper must be greater than zero";
  }
  else if (dwell_us > 5000000u) {
    message = "dwell_us exceeds limit";
  }

  if ((message != nullptr) && (error_message != nullptr)) {
    *error_message = message;
  }
  return message == nullptr;
}

void phoenix_benchmark_pot_sweep_initialise(const PhoenixBenchmarkPotSweepDefaults& defaults) {
  g_pot_sweep_defaults = defaults;
}

void phoenix_benchmark_pot_sweep_reset_state(void) {
  g_pot_sweep_defaults = k_default_pot_sweep_defaults;
#if defined(UNIT_TEST)
  phoenix_benchmark_pot_sweep_clear_test_hooks();
#endif
}

PhoenixBenchmarkPotSweepParseResult phoenix_benchmark_pot_sweep_parse_command(const char* line) {
  PhoenixBenchmarkPotSweepOptions options = {};
  if (line == nullptr) {
    return {false, options, k_error_invalid_arguments};
  }

  // Step 1: Seed the option structure with defaults so optional fields inherit baseline values.
  options.apply_defaults(g_pot_sweep_defaults);

  const char* cursor = skip_whitespace(line);
  if (cursor == nullptr) {
    return {false, options, k_error_invalid_command};
  }

  // Step 2: Accept bare command tokens or validate the JSON object envelope.
  if (*cursor != '{') {
    // Accept the bare command token so defaults remain active.
    if (std::strstr(line, "pot_sweep") != nullptr) {
      const char* validation_message = nullptr;
      if (!options.validate(&validation_message)) {
        return {false, options, (validation_message != nullptr) ? validation_message : k_error_invalid_options};
      }
      return {true, options, nullptr};
    }
    return {false, options, k_error_invalid_command};
  }

  ++cursor;

  bool saw_command = false;

  // Step 3: Walk each key/value pair and collect command plus parameter overrides.
  while ((*cursor != '\0')) {
    cursor = skip_whitespace(cursor);
    if ((cursor == nullptr) || (*cursor == '\0')) {
      break;
    }

    if (*cursor == '}') {
      ++cursor;
      break;
    }

    if (*cursor != '"') {
      return {false, options, k_error_invalid_command};
    }
    ++cursor;

    const char* key_start = cursor;
    while ((*cursor != '\0') && (*cursor != '"')) {
      ++cursor;
    }
    if (*cursor != '"') {
      return {false, options, k_error_invalid_command};
    }

    const std::size_t key_length = static_cast<std::size_t>(cursor - key_start);
    if ((key_length == 0u) || (key_length >= 32u)) {
      return {false, options, k_error_invalid_command};
    }

    char key_buffer[32] = {};
    std::memcpy(key_buffer, key_start, key_length);
    key_buffer[key_length] = '\0';

    ++cursor;
    cursor = skip_whitespace(cursor);
    if (*cursor != ':') {
      return {false, options, k_error_invalid_command};
    }
    ++cursor;
    cursor = skip_whitespace(cursor);

    if (std::strcmp(key_buffer, "command") == 0) {
      // Step 3a: Capture the command token and confirm the payload targets pot_sweep.
      if (*cursor != '"') {
        return {false, options, k_error_invalid_command};
      }
      ++cursor;
      const char* value_start = cursor;
      while ((*cursor != '\0') && (*cursor != '"')) {
        ++cursor;
      }
      if (*cursor != '"') {
        return {false, options, k_error_invalid_command};
      }
      const std::size_t value_length = static_cast<std::size_t>(cursor - value_start);
      if ((value_length != std::strlen("pot_sweep")) || (std::strncmp(value_start, "pot_sweep", value_length) != 0)) {
        return {false, options, k_error_invalid_command};
      }
      saw_command = true;
      ++cursor;
    }
    else if ((std::strcmp(key_buffer, "parameters") == 0) || (std::strcmp(key_buffer, "arguments") == 0)) {
      // Step 3b: Iterate parameter overrides and apply them to the option structure.
      if (*cursor != '{') {
        return {false, options, k_error_invalid_command};
      }
      ++cursor;
      while (true) {
        cursor = skip_whitespace(cursor);
        if (cursor == nullptr) {
          return {false, options, k_error_invalid_command};
        }
        if (*cursor == '}') {
          ++cursor;
          break;
        }
        if (*cursor != '"') {
          return {false, options, k_error_invalid_command};
        }
        ++cursor;
        const char* param_key_start = cursor;
        while ((*cursor != '\0') && (*cursor != '"')) {
          ++cursor;
        }
        if (*cursor != '"') {
          return {false, options, k_error_invalid_command};
        }
        const std::size_t param_key_length = static_cast<std::size_t>(cursor - param_key_start);
        if ((param_key_length == 0u) || (param_key_length >= 32u)) {
          return {false, options, k_error_invalid_command};
        }

        char param_key_buffer[32] = {};
        std::memcpy(param_key_buffer, param_key_start, param_key_length);
        param_key_buffer[param_key_length] = '\0';

        ++cursor;
        cursor = skip_whitespace(cursor);
        if (*cursor != ':') {
          return {false, options, k_error_invalid_command};
        }
        ++cursor;
        cursor = skip_whitespace(cursor);

        if (std::strcmp(param_key_buffer, "sweeps") == 0) {
          // Step 3b-i: Override the sweep count when specified by the host.
          uint32_t sweeps = 0u;
          if (!parse_unsigned_value(cursor, 10, &sweeps, &cursor) || (sweeps == 0u)) {
            return {false, options, k_error_invalid_value};
          }
          options.sweeps_per_wiper    = sweeps;
          options.has_sweeps_override = true;
        }
        else if (std::strcmp(param_key_buffer, "dwell_us") == 0) {
          // Step 3b-ii: Override the dwell interval when specified by the host.
          uint32_t dwell = 0u;
          if (!parse_unsigned_value(cursor, 10, &dwell, &cursor)) {
            return {false, options, k_error_invalid_value};
          }
          options.dwell_us           = dwell;
          options.has_dwell_override = true;
        }
        else {
          // Step 3b-iii: Reject unrecognised parameters to highlight protocol drift.
          return {false, options, k_error_invalid_command};
        }

        cursor = skip_whitespace(cursor);
        if (*cursor == ',') {
          ++cursor;
          continue;
        }
        if (*cursor == '}') {
          ++cursor;
          break;
        }
        return {false, options, k_error_invalid_command};
      }
    }
    else {
      return {false, options, k_error_invalid_command};
    }

    cursor = skip_whitespace(cursor);
    if (*cursor == ',') {
      ++cursor;
      continue;
    }
    if (*cursor == '}') {
      ++cursor;
      break;
    }
  }

  if (!saw_command) {
    return {false, options, k_error_invalid_command};
  }

  // Step 4: Validate the populated options before reporting success to the caller.
  const char* validation_message = nullptr;
  if (!options.validate(&validation_message)) {
    return {false, options, (validation_message != nullptr) ? validation_message : k_error_invalid_options};
  }

  return {true, options, nullptr};
}

PhoenixBenchmarkPotSweepExecutionStatus phoenix_benchmark_pot_sweep_run(
    const PhoenixBenchmarkPotSweepOptions& input_options, PhoenixBenchmarkPotSweepRowMetrics* rows,
    std::size_t row_capacity) {
  // Step 1: Verify the output buffer so we never dereference invalid storage.
  if ((rows == nullptr) || (row_capacity == 0u)) {
    return {false, false, k_error_invalid_arguments, 0u, false, 0u, false, 0u};
  }

  PhoenixBenchmarkPotSweepOptions options = input_options;
  // Step 2: Apply defaults so partially populated option structures inherit baseline values.
  options.apply_defaults(g_pot_sweep_defaults);

  const char* validation_message = nullptr;
  // Step 3: Validate the merged options before interacting with hardware.
  if (!options.validate(&validation_message)) {
    return {false, false, (validation_message != nullptr) ? validation_message : k_error_invalid_options, 0u, false, 0u,
            false, 0u};
  }

  // Step 4: Confirm the output buffer can store every wiper result.
  if (row_capacity < k_phoenix_benchmark_pot_sweep_max_wiper_count) {
    return {false, false, k_error_invalid_arguments, 0u, false, 0u, false, 0u};
  }

  // Step 5: Ensure hardware is initialised before launching channel-map sweeps.
  if (!g_hardware_ready_checker()) {
    return {false, false, k_error_hardware_init, 0u, false, 0u, false, 0u};
  }

  if ((g_adc_default_config != nullptr) && (g_adc_default_config() != MCP356X_OK)) {
    return {false, false, k_error_adc_defaults, 0u, false, 0u, false, 0u};
  }

  const ChannelMapTemplateResult    template_result      = load_channel_map_template();
  PhoenixBenchmarkChannelMapOptions channel_map_template = template_result.options;

  channel_map_template.dwell_us           = options.dwell_us;
  channel_map_template.has_dwell_override = true;

  PhoenixBenchmarkChannelMapOutputCallbacks callbacks = {nullptr, nullptr};

  uint32_t rows_generated = 0u;
  bool     has_warnings   = !template_result.success;

  bool    led1_recommendation_valid = false;
  uint8_t led1_recommended_wiper    = 0u;
  int32_t led1_best_code            = std::numeric_limits<int32_t>::min();

  bool    led2_recommendation_valid = false;
  uint8_t led2_recommended_wiper    = 0u;
  int32_t led2_best_code            = std::numeric_limits<int32_t>::min();

  for (uint32_t wiper = 0u; wiper <= 0xFFu; ++wiper) {
    const uint8_t wiper_code = static_cast<uint8_t>(wiper & 0xFFu);

    // Step 6: Execute the channel-map runner for the current wiper position.
    PhoenixBenchmarkChannelMapOptions map_options = channel_map_template;
    map_options.sweep_count                       = options.sweeps_per_wiper;
    map_options.has_sweep_override                = true;
    map_options.dwell_us                          = options.dwell_us;
    map_options.has_dwell_override                = true;
    map_options.wiper_code                        = wiper_code;
    map_options.has_wiper_override                = true;

    PhoenixBenchmarkStateAccumulator accumulators[k_phoenix_benchmark_channel_map_state_descriptor_count] = {};

    const PhoenixBenchmarkChannelMapExecutionStatus run_status =
        g_channel_map_runner(map_options, accumulators, callbacks);
    if (!run_status.success) {
      return {false,
              has_warnings,
              k_error_channel_map_run,
              rows_generated,
              led1_recommendation_valid,
              led1_recommended_wiper,
              led2_recommendation_valid,
              led2_recommended_wiper};
    }

    if (run_status.has_warnings) {
      has_warnings = true;
    }

    // Step 7: Capture per-wiper maxima so the formatter can report LED headroom.
    PhoenixBenchmarkPotSweepRowMetrics& row = rows[rows_generated];
    row                                     = PhoenixBenchmarkPotSweepRowMetrics{};
    row.wiper_code                          = wiper_code;

    const PhoenixBenchmarkStateAccumulator& led1_accumulator =
        accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 1u];
    const PhoenixBenchmarkStateAccumulator& led2_accumulator =
        accumulators[k_phoenix_benchmark_channel_map_drain_state_index + 2u];

    if (led1_accumulator.channel_a_codes.count > 0u) {
      row.led1_max_code = led1_accumulator.channel_a_codes.max_value;
    }
    if (led2_accumulator.channel_b_codes.count > 0u) {
      row.led2_max_code = led2_accumulator.channel_b_codes.max_value;
    }

    const int32_t led1_abs_code = std::abs(row.led1_max_code);
    const int32_t led2_abs_code = std::abs(row.led2_max_code);

    row.led1_saturated = (led1_abs_code >= k_pot_sweep_saturation_threshold);
    row.led2_saturated = (led2_abs_code >= k_pot_sweep_saturation_threshold);

    if (row.led1_saturated || row.led2_saturated) {
      has_warnings = true;
    }

    // Step 8: Track the best recommended wiper for each LED while avoiding saturated entries.
    if (!row.led1_saturated) {
      if (!led1_recommendation_valid || (led1_abs_code > led1_best_code)) {
        led1_recommendation_valid = true;
        led1_recommended_wiper    = wiper_code;
        led1_best_code            = led1_abs_code;
      }
    }

    if (!row.led2_saturated) {
      if (!led2_recommendation_valid || (led2_abs_code > led2_best_code)) {
        led2_recommendation_valid = true;
        led2_recommended_wiper    = wiper_code;
        led2_best_code            = led2_abs_code;
      }
    }

    rows_generated += 1u;
  }

  // Step 9: Fall back to the first wiper when saturation prevented recommendation selection.
  if (!led1_recommendation_valid) {
    led1_recommendation_valid = true;
    led1_recommended_wiper    = 0x00u;
  }

  if (!led2_recommendation_valid) {
    led2_recommendation_valid = true;
    led2_recommended_wiper    = 0x00u;
  }

  return {true,
          has_warnings,
          nullptr,
          rows_generated,
          led1_recommendation_valid,
          led1_recommended_wiper,
          led2_recommendation_valid,
          led2_recommended_wiper};
}

int32_t phoenix_benchmark_pot_sweep_saturation_threshold(void) {
  return k_pot_sweep_saturation_threshold;
}

void phoenix_benchmark_pot_sweep_set_channel_map_runner_for_test(PhoenixBenchmarkChannelMapExecutionStatus (*runner)(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks)) {
  g_channel_map_runner = (runner != nullptr) ? runner : phoenix_benchmark_channel_map_run;
}

void phoenix_benchmark_pot_sweep_set_hardware_ready_checker_for_test(bool (*checker)(void)) {
  g_hardware_ready_checker = (checker != nullptr) ? checker : phoenix_benchmark_channel_map_ensure_hardware_ready;
}

void phoenix_benchmark_pot_sweep_set_adc_default_configurator_for_test(int (*configurator)(void)) {
  g_adc_default_config = (configurator != nullptr) ? configurator : mcp356x_apply_default_config;
}

void phoenix_benchmark_pot_sweep_clear_test_hooks(void) {
  g_channel_map_runner     = phoenix_benchmark_channel_map_run;
  g_hardware_ready_checker = phoenix_benchmark_channel_map_ensure_hardware_ready;
  g_adc_default_config     = mcp356x_apply_default_config;
}
