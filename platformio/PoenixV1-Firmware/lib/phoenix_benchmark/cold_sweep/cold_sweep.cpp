#include "cold_sweep.hpp"

#include "../channel_map/channel_map.hpp"
#include "phoenix_guard.hpp"
#include <Arduino.h>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <device_setup.hpp>

namespace {
int (*g_light_readings_runner)(uint32_t, LightReadingsSweepCollection*)                  = nullptr;
int (*g_stats_calculator)(const LightReadingsSweepCollection*, LightReadingsSweepStats*) = nullptr;
bool (*g_saturation_checker)(void)                                                       = nullptr;
uint32_t (*g_timestamp_provider)(void)                                                   = nullptr;
bool (*g_hardware_ready_checker)(void) = phoenix_benchmark_channel_map_ensure_hardware_ready;

constexpr const char* k_error_invalid_arguments              = "invalid_arguments";
constexpr const char* k_error_invalid_command                = "invalid_command";
constexpr const char* k_error_invalid_parameters             = "invalid_parameters";
constexpr const char* k_error_hardware_initialisation_failed = "hardware_initialisation_failed";
constexpr const char* k_error_light_initialisation_failed    = "light_initialisation_failed";
constexpr const char* k_error_light_shutdown_failed          = "light_shutdown_failed";

const char* skip_whitespace(const char* cursor) {
  if (cursor == nullptr) {
    return nullptr;
  }
  while ((*cursor != '\0') && std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
    ++cursor;
  }
  return cursor;
}

bool consume_literal(const char** cursor, const char* literal) {
  if ((cursor == nullptr) || (literal == nullptr)) {
    return false;
  }
  const std::size_t length = std::strlen(literal);
  if (std::strncmp(*cursor, literal, length) != 0) {
    return false;
  }
  *cursor += length;
  return true;
}

bool parse_string_token(const char** cursor, char* buffer, std::size_t length) {
  if ((cursor == nullptr) || (buffer == nullptr) || (length == 0u)) {
    return false;
  }
  if (**cursor != '"') {
    return false;
  }
  ++(*cursor);
  const char* start = *cursor;
  while ((**cursor != '\0') && (**cursor != '"')) {
    ++(*cursor);
  }
  if (**cursor != '"') {
    return false;
  }
  const std::size_t token_length = static_cast<std::size_t>(*cursor - start);
  if ((token_length + 1u) > length) {
    return false;
  }
  std::memcpy(buffer, start, token_length);
  buffer[token_length] = '\0';
  ++(*cursor);
  return true;
}

bool parse_empty_object(const char** cursor) {
  if ((cursor == nullptr) || (**cursor != '{')) {
    return false;
  }
  ++(*cursor);
  *cursor = skip_whitespace(*cursor);
  if (*cursor == nullptr) {
    return false;
  }
  if (**cursor != '}') {
    return false;
  }
  ++(*cursor);
  return true;
}

int run_light_readings(uint32_t sweep_count, LightReadingsSweepCollection* collection) {
  if (g_light_readings_runner != nullptr) {
    return g_light_readings_runner(sweep_count, collection);
  }
  return light_readings_sweep_n(sweep_count, collection);
}

int compute_light_reading_stats(const LightReadingsSweepCollection* collection, LightReadingsSweepStats* stats_out) {
  if (g_stats_calculator != nullptr) {
    return g_stats_calculator(collection, stats_out);
  }
  return light_readings_compute_sweep_stats(collection, stats_out);
}

bool did_sweep_detect_saturation(void) {
  if (g_saturation_checker != nullptr) {
    return g_saturation_checker();
  }
  return light_readings_last_sweep_detected_saturation();
}

uint32_t capture_timestamp_us(void) {
  if (g_timestamp_provider != nullptr) {
    return g_timestamp_provider();
  }
  return micros();
}
}  // namespace

void phoenix_benchmark_cold_sweep_reset_state(void) {
  phoenix_benchmark_cold_sweep_clear_test_hooks();
}

PhoenixBenchmarkColdSweepParseResult phoenix_benchmark_cold_sweep_parse_command(const char* line) {
  PhoenixBenchmarkColdSweepOptions options = {
      .sweep_count        = 0u,
      .has_sweep_override = false,
      .dwell_override_us  = 0u,
      .has_dwell_override = false,
  };

  if (line == nullptr) {
    return {false, options, k_error_invalid_arguments};
  }

  const char* cursor = skip_whitespace(line);
  if (cursor == nullptr) {
    return {false, options, k_error_invalid_command};
  }

  // Step 1: Accept the bare command token for compatibility with manual shells.
  if (*cursor != '{') {
    if (!consume_literal(&cursor, "cold_sweep")) {
      return {false, options, k_error_invalid_command};
    }
    cursor = skip_whitespace(cursor);
    if ((cursor != nullptr) && (*cursor == '\0')) {
      return {true, options, nullptr};
    }
    return {false, options, k_error_invalid_command};
  }

  // Step 2: Parse the minimal JSON envelope shared with other benchmarks.
  ++cursor;
  bool saw_command    = false;
  bool saw_parameters = false;

  while (true) {
    cursor = skip_whitespace(cursor);
    if (cursor == nullptr) {
      return {false, options, k_error_invalid_command};
    }
    if (*cursor == '}') {
      ++cursor;
      break;
    }

    char key_buffer[32] = {};
    if (!parse_string_token(&cursor, key_buffer, sizeof(key_buffer))) {
      return {false, options, k_error_invalid_command};
    }

    cursor = skip_whitespace(cursor);
    if (cursor == nullptr || (*cursor != ':')) {
      return {false, options, k_error_invalid_command};
    }
    ++cursor;
    cursor = skip_whitespace(cursor);
    if (cursor == nullptr) {
      return {false, options, k_error_invalid_command};
    }

    if (std::strcmp(key_buffer, "command") == 0) {
      // Step 2a: Confirm the payload targets the cold_sweep benchmark.
      char value_buffer[32] = {};
      if (!parse_string_token(&cursor, value_buffer, sizeof(value_buffer))) {
        return {false, options, k_error_invalid_command};
      }
      if (std::strcmp(value_buffer, "cold_sweep") != 0) {
        return {false, options, k_error_invalid_command};
      }
      saw_command = true;
    }
    else if ((std::strcmp(key_buffer, "parameters") == 0) || (std::strcmp(key_buffer, "arguments") == 0)) {
      // Step 2b: Allow an empty parameter object to match other benchmark schemas.
      if (!parse_empty_object(&cursor)) {
        return {false, options, k_error_invalid_parameters};
      }
      saw_parameters = true;
    }
    else {
      return {false, options, k_error_invalid_command};
    }

    cursor = skip_whitespace(cursor);
    if (cursor == nullptr) {
      return {false, options, k_error_invalid_command};
    }
    if (*cursor == ',') {
      ++cursor;
      continue;
    }
  }

  cursor = skip_whitespace(cursor);
  if ((cursor != nullptr) && (*cursor != '\0')) {
    return {false, options, k_error_invalid_command};
  }

  if (!saw_command) {
    return {false, options, k_error_invalid_command};
  }

  (void) saw_parameters;
  return {true, options, nullptr};
}

PhoenixBenchmarkColdSweepExecutionStatus phoenix_benchmark_cold_sweep_run(
    const PhoenixBenchmarkColdSweepOptions& options, LightReadingsSweepCollection* sweeps_out,
    LightReadingsSweepStats* stats_out) {
  PhoenixBenchmarkColdSweepExecutionStatus status = {};
  status.success                                  = false;
  status.has_warnings                             = false;
  status.warning_mask                             = 0u;
  status.message                                  = nullptr;
  status.captured_sweeps                          = 0u;
  status.timestamp_us                             = 0u;

  // Step 1: Validate output storage and derive the requested sweep count.
  if ((sweeps_out == nullptr) || (stats_out == nullptr)) {
    status.message = "invalid_arguments";
    return status;
  }

  *stats_out = {};

  const uint32_t requested_sweeps = options.has_sweep_override ? options.sweep_count : LIGHT_READINGS_MAX_SWEEP_COUNT;
  if ((requested_sweeps == 0u) || (requested_sweeps > LIGHT_READINGS_MAX_SWEEP_COUNT)) {
    status.message = "invalid_sweep_count";
    return status;
  }

  if ((sweeps_out->sweeps == nullptr) && (requested_sweeps > 0u)) {
    status.message = "missing_sweep_storage";
    return status;
  }

  sweeps_out->sweep_count = 0u;

  // Step 2: Ensure the hardware stack is ready and initialise the light readings helper for this run.
  if ((g_hardware_ready_checker == nullptr) || !g_hardware_ready_checker()) {
    status.message = k_error_hardware_initialisation_failed;
    return status;
  }

  LightReadingsConfig light_config = g_device_light_readings_config;
  if (options.has_dwell_override) {
    light_config.blue_channel.dwell_us  = options.dwell_override_us;
    light_config.green_channel.dwell_us = options.dwell_override_us;
  }

  bool      light_readings_initialised = false;
  const int initialise_result          = light_readings_initialize(&light_config);
  if (initialise_result != LIGHT_READINGS_OK) {
    status.message = k_error_light_initialisation_failed;
    return status;
  }
  light_readings_initialised = true;

  auto shutdown_light_readings = [&]() {
    if (!light_readings_initialised) {
      return LIGHT_READINGS_OK;
    }
    const int shutdown_result = light_readings_shutdown();
    if (shutdown_result == LIGHT_READINGS_OK) {
      light_readings_initialised = false;
    }
    return shutdown_result;
  };

  // Step 3: Capture the sweep batch, compute statistics, and record saturation status.
  const int sweep_return_code = run_light_readings(requested_sweeps, sweeps_out);
  if (sweep_return_code != LIGHT_READINGS_OK) {
    status.message = "light_readings_sweep_failed";
  }
  else if (compute_light_reading_stats(sweeps_out, stats_out) != LIGHT_READINGS_OK) {
    status.message = "statistics_failed";
  }
  else {
    status.success         = true;
    status.captured_sweeps = sweeps_out->sweep_count;
    status.timestamp_us    = capture_timestamp_us();

    if (did_sweep_detect_saturation()) {
      status.has_warnings = true;
      status.warning_mask |= k_phoenix_benchmark_cold_sweep_warning_saturation;
    }
  }

  const int shutdown_result = shutdown_light_readings();
  if (shutdown_result != LIGHT_READINGS_OK) {
    status.success = false;
    if (status.message == nullptr) {
      status.message = k_error_light_shutdown_failed;
    }
  }

  if (!status.success && (status.timestamp_us == 0u)) {
    status.timestamp_us = capture_timestamp_us();
  }

  return status;
}

void phoenix_benchmark_cold_sweep_set_light_readings_runner_for_test(
    int (*runner)(uint32_t sweep_count, LightReadingsSweepCollection* collection)) {
  g_light_readings_runner = runner;
}

void phoenix_benchmark_cold_sweep_set_stats_calculator_for_test(
    int (*calculator)(const LightReadingsSweepCollection* collection, LightReadingsSweepStats* stats_out)) {
  g_stats_calculator = calculator;
}

void phoenix_benchmark_cold_sweep_set_saturation_checker_for_test(bool (*checker)(void)) {
  g_saturation_checker = checker;
}

void phoenix_benchmark_cold_sweep_set_timestamp_provider_for_test(uint32_t (*provider)(void)) {
  g_timestamp_provider = provider;
}

void phoenix_benchmark_cold_sweep_set_hardware_ready_checker_for_test(bool (*checker)(void)) {
  g_hardware_ready_checker = (checker != nullptr) ? checker : phoenix_benchmark_channel_map_ensure_hardware_ready;
}

void phoenix_benchmark_cold_sweep_clear_test_hooks(void) {
  g_light_readings_runner  = nullptr;
  g_stats_calculator       = nullptr;
  g_saturation_checker     = nullptr;
  g_timestamp_provider     = nullptr;
  g_hardware_ready_checker = phoenix_benchmark_channel_map_ensure_hardware_ready;
}
