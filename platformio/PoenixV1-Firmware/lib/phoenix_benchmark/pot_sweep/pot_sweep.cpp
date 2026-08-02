#include "pot_sweep.hpp"

#include "../../digipot_hal/digipot_hal.hpp"
#include "../../light_readings/light_readings.hpp"
#include "../../mcp356x/mcp356x.hpp"
#include "../channel_map/channel_map.hpp"
#include "../core/phoenix_benchmark_core.hpp"
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <device_setup.hpp>
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
constexpr const char* k_error_light_runtime     = "light readings runtime update failed";
constexpr const char* k_error_light_sweep       = "sampling failed";
constexpr const char* k_error_light_stats       = "statistics computation failed";
constexpr const char* k_error_light_shutdown    = "light readings shutdown failed";
constexpr const char* k_error_adc_defaults      = "adc default configuration failed";

typedef bool (*HardwareReadyChecker)(void);
typedef int (*AdcConfigurator)(void);

PhoenixBenchmarkPotSweepDefaults g_pot_sweep_defaults     = k_default_pot_sweep_defaults;
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

  // Step 5: Ensure hardware is initialised before launching light readings sweeps.
  if (!g_hardware_ready_checker()) {
    return {false, false, k_error_hardware_init, 0u, false, 0u, false, 0u};
  }

  if ((g_adc_default_config != nullptr) && (g_adc_default_config() != MCP356X_OK)) {
    return {false, false, k_error_adc_defaults, 0u, false, 0u, false, 0u};
  }

  LightReadingsConfig light_config = g_device_light_readings_config;
  if (light_readings_initialize(&light_config) != LIGHT_READINGS_OK) {
    return {false, false, k_error_hardware_init, 0u, false, 0u, false, 0u};
  }

  bool light_readings_initialised = true;
  auto shutdown_light_readings    = [&]() {
    if (!light_readings_initialised) {
      return LIGHT_READINGS_OK;
    }
    const int shutdown_result = light_readings_shutdown();
    if (shutdown_result != LIGHT_READINGS_OK) {
      return shutdown_result;
    }
    light_readings_initialised = false;
    return LIGHT_READINGS_OK;
  };

  LightReadingsRuntimeSettings dwell_settings = {
      .apply_dwell_override = true,
      .dwell_us             = options.dwell_us,
      .apply_wiper_override = false,
      .wiper_code           = 0u,
  };

  if (light_readings_modify_settings(&dwell_settings) != LIGHT_READINGS_OK) {
    (void) shutdown_light_readings();
    return {false, false, k_error_light_runtime, 0u, false, 0u, false, 0u};
  }

  LightReadingsSweepCollection sweep_collection = {
      .sweep_count = 0u,
      .sweeps      = g_light_readings_sweep_storage,
  };

  uint32_t rows_generated = 0u;
  bool     has_warnings   = false;

  bool     blue_recommendation_valid = false;
  uint16_t blue_recommended_wiper    = 0u;
  int32_t  blue_best_code            = std::numeric_limits<int32_t>::min();

  bool     green_recommendation_valid = false;
  uint16_t green_recommended_wiper    = 0u;
  int32_t  green_best_code            = std::numeric_limits<int32_t>::min();

  for (uint32_t wiper = 0u; wiper < k_phoenix_benchmark_pot_sweep_max_wiper_count; ++wiper) {
    const uint16_t wiper_code = static_cast<uint16_t>(wiper);

    // Step 7a: Set the blue wiper (MCP41U83T supports full 10-bit range).
    int wiper_result = digipot_blue_set_wiper(wiper_code);
    if (wiper_result != DIGIPOT_HAL_OK) {
      (void) shutdown_light_readings();
      return {false,
              has_warnings,
              k_error_light_runtime,
              rows_generated,
              blue_recommendation_valid,
              blue_recommended_wiper,
              green_recommendation_valid,
              green_recommended_wiper};
    }

    // Step 7b: Set the green wiper (AD5242 8-bit), clamping to its max.
    const uint16_t green_code = (wiper_code <= DIGIPOT_GREEN_MAX_WIPER) ? wiper_code : DIGIPOT_GREEN_MAX_WIPER;
    wiper_result              = digipot_green_set_wiper(green_code);
    if (wiper_result != DIGIPOT_HAL_OK) {
      (void) shutdown_light_readings();
      return {false,
              has_warnings,
              k_error_light_runtime,
              rows_generated,
              blue_recommendation_valid,
              blue_recommended_wiper,
              green_recommendation_valid,
              green_recommended_wiper};
    }

    const int sweep_result = light_readings_sweep_n(options.sweeps_per_wiper, &sweep_collection);
    if (sweep_result != LIGHT_READINGS_OK) {
      (void) shutdown_light_readings();
      return {false,
              has_warnings,
              k_error_light_sweep,
              rows_generated,
              blue_recommendation_valid,
              blue_recommended_wiper,
              green_recommendation_valid,
              green_recommended_wiper};
    }

    LightReadingsSweepStats sweep_stats = {};
    if (light_readings_compute_sweep_stats(&sweep_collection, &sweep_stats) != LIGHT_READINGS_OK) {
      (void) shutdown_light_readings();
      return {false,
              has_warnings,
              k_error_light_stats,
              rows_generated,
              blue_recommendation_valid,
              blue_recommended_wiper,
              green_recommendation_valid,
              green_recommended_wiper};
    }

    PhoenixBenchmarkPotSweepRowMetrics& row = rows[rows_generated];
    row                                     = PhoenixBenchmarkPotSweepRowMetrics{};
    row.wiper_code                          = wiper_code;

    row.blue_max_code  = sweep_stats.blue.has_samples ? sweep_stats.blue.max_value : 0;
    row.green_max_code = sweep_stats.green.has_samples ? sweep_stats.green.max_value : 0;

    const int32_t blue_abs_code  = (row.blue_max_code < 0) ? -row.blue_max_code : row.blue_max_code;
    const int32_t green_abs_code = (row.green_max_code < 0) ? -row.green_max_code : row.green_max_code;

    bool blue_sample_saturated  = false;
    bool green_sample_saturated = false;
    bool sweep_saw_saturation   = false;
    if ((sweep_collection.sweeps != nullptr) && (sweep_collection.sweep_count > 0u)) {
      for (uint32_t sample_index = 0u; sample_index < sweep_collection.sweep_count; ++sample_index) {
        const LightReadingsSweepSample& sample = sweep_collection.sweeps[sample_index];

        if (phoenix_benchmark_is_adc_code_saturated(sample.drain_blue_code) ||
            phoenix_benchmark_is_adc_code_saturated(sample.drain_green_code)) {
          sweep_saw_saturation = true;
        }
        if (phoenix_benchmark_is_adc_code_saturated(sample.blue_code)) {
          blue_sample_saturated = true;
          sweep_saw_saturation  = true;
        }
        if (phoenix_benchmark_is_adc_code_saturated(sample.green_code)) {
          green_sample_saturated = true;
          sweep_saw_saturation   = true;
        }
      }
    }

    const bool adc_reported_saturation = light_readings_last_sweep_detected_saturation();

    row.blue_saturated  = (blue_abs_code >= k_pot_sweep_saturation_threshold) || blue_sample_saturated;
    row.green_saturated = (green_abs_code >= k_pot_sweep_saturation_threshold) || green_sample_saturated;

    if (row.blue_saturated || row.green_saturated || sweep_saw_saturation || adc_reported_saturation) {
      has_warnings = true;
    }

    if (!row.blue_saturated) {
      if (!blue_recommendation_valid || (blue_abs_code > blue_best_code)) {
        blue_recommendation_valid = true;
        blue_recommended_wiper    = wiper_code;
        blue_best_code            = blue_abs_code;
      }
    }

    if (!row.green_saturated) {
      if (!green_recommendation_valid || (green_abs_code > green_best_code)) {
        green_recommendation_valid = true;
        green_recommended_wiper    = wiper_code;
        green_best_code            = green_abs_code;
      }
    }

    rows_generated += 1u;
  }

  if (!blue_recommendation_valid) {
    blue_recommendation_valid = true;
    blue_recommended_wiper    = 0x00u;
  }

  if (!green_recommendation_valid) {
    green_recommendation_valid = true;
    green_recommended_wiper    = 0x00u;
  }

  const int shutdown_result = shutdown_light_readings();
  if (shutdown_result != LIGHT_READINGS_OK) {
    has_warnings = true;
  }

  return {true,
          has_warnings || (shutdown_result != LIGHT_READINGS_OK),
          (shutdown_result == LIGHT_READINGS_OK) ? nullptr : k_error_light_shutdown,
          rows_generated,
          blue_recommendation_valid,
          blue_recommended_wiper,
          green_recommendation_valid,
          green_recommended_wiper};
}

int32_t phoenix_benchmark_pot_sweep_saturation_threshold(void) {
  return k_pot_sweep_saturation_threshold;
}

void phoenix_benchmark_pot_sweep_set_hardware_ready_checker_for_test(bool (*checker)(void)) {
  g_hardware_ready_checker = (checker != nullptr) ? checker : phoenix_benchmark_channel_map_ensure_hardware_ready;
}

void phoenix_benchmark_pot_sweep_set_adc_default_configurator_for_test(int (*configurator)(void)) {
  g_adc_default_config = (configurator != nullptr) ? configurator : mcp356x_apply_default_config;
}

void phoenix_benchmark_pot_sweep_clear_test_hooks(void) {
  g_hardware_ready_checker = phoenix_benchmark_channel_map_ensure_hardware_ready;
  g_adc_default_config     = mcp356x_apply_default_config;
}
