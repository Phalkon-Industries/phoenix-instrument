#include "osr_sweep.hpp"

#include "../../mcp356x/mcp356x.hpp"
#include "../channel_map/channel_map.hpp"
#include "../channel_map/command_parser.hpp"
#include "../core/phoenix_benchmark_core.hpp"
#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

constexpr PhoenixBenchmarkOsrSweepDefaults k_default_osr_sweep_defaults = {
    .sweep_count = 100u,
    .dwell_us    = 100u,
    .wiper_code  = 0x00u,
};

constexpr const char* k_error_invalid_arguments = "invalid arguments";
constexpr const char* k_error_invalid_options   = "invalid options";
constexpr const char* k_error_set_osr_failed    = "osr configuration failed";
constexpr const char* k_error_channel_map_run   = "channel_map failed";

typedef PhoenixBenchmarkChannelMapExecutionStatus (*ChannelMapRunner)(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks);

typedef int (*OsrSetter)(mcp356x_osr value);

typedef uint32_t (*MicrosProvider)(void);

PhoenixBenchmarkOsrSweepDefaults g_osr_defaults       = k_default_osr_sweep_defaults;
ChannelMapRunner                 g_channel_map_runner = phoenix_benchmark_channel_map_run;
OsrSetter                        g_osr_setter         = mcp356x_set_osr;
MicrosProvider                   g_micros_provider    = ::micros;

constexpr mcp356x_osr k_osr_values[k_phoenix_benchmark_osr_value_count] = {
    mcp356x_osr::osr_32,    mcp356x_osr::osr_64,    mcp356x_osr::osr_128,   mcp356x_osr::osr_256,
    mcp356x_osr::osr_512,   mcp356x_osr::osr_1024,  mcp356x_osr::osr_2048,  mcp356x_osr::osr_4096,
    mcp356x_osr::osr_8192,  mcp356x_osr::osr_16384, mcp356x_osr::osr_20480, mcp356x_osr::osr_24576,
    mcp356x_osr::osr_40960, mcp356x_osr::osr_49152, mcp356x_osr::osr_81920, mcp356x_osr::osr_98304,
};

uint32_t compute_elapsed_time(uint32_t start, uint32_t end) {
  if (end >= start) {
    return end - start;
  }
  return (0xFFFFFFFFu - start) + end + 1u;
}

}  // namespace

void PhoenixBenchmarkOsrSweepOptions::apply_defaults(const PhoenixBenchmarkOsrSweepDefaults& defaults) {
  if (!has_sweep_override) {
    sweep_count = defaults.sweep_count;
  }
  if (!has_dwell_override) {
    dwell_us = defaults.dwell_us;
  }
  if (!has_wiper_override) {
    wiper_code = defaults.wiper_code;
  }
}

bool PhoenixBenchmarkOsrSweepOptions::validate(const char** error_message) const {
  const char* message = nullptr;
  if (sweep_count == 0u) {
    message = "sweep_count must be greater than zero";
  }
  else if (dwell_us > 5000000u) {
    message = "dwell_us exceeds limit";
  }

  if ((message != nullptr) && (error_message != nullptr)) {
    *error_message = message;
  }
  return message == nullptr;
}

void phoenix_benchmark_osr_sweep_initialise(const PhoenixBenchmarkOsrSweepDefaults& defaults) {
  g_osr_defaults = defaults;
}

void phoenix_benchmark_osr_sweep_reset_state(void) {
  g_osr_defaults = k_default_osr_sweep_defaults;
#if defined(UNIT_TEST)
  phoenix_benchmark_osr_sweep_clear_test_hooks();
#endif
}

PhoenixBenchmarkOsrSweepParseResult phoenix_benchmark_osr_sweep_parse_command(const char* line) {
  PhoenixBenchmarkOsrSweepOptions options = {};
  if (line == nullptr) {
    return {false, options, k_error_invalid_arguments};
  }

  options.apply_defaults(g_osr_defaults);

  PhoenixBenchmarkChannelMapParseOutcome outcome = phoenix_benchmark_channel_map_parse_command_line(line, "osr_sweep");
  if (!outcome.success) {
    return {false, options, outcome.error_message};
  }

  if (outcome.arguments.has_sweep_override) {
    options.sweep_count        = outcome.arguments.sweep_count;
    options.has_sweep_override = true;
  }
  if (outcome.arguments.has_dwell_override) {
    options.dwell_us           = outcome.arguments.dwell_us;
    options.has_dwell_override = true;
  }
  if (outcome.arguments.has_wiper_override) {
    options.wiper_code         = outcome.arguments.wiper_code;
    options.has_wiper_override = true;
  }

  const char* validation_message = nullptr;
  if (!options.validate(&validation_message)) {
    return {false, options, (validation_message != nullptr) ? validation_message : k_error_invalid_options};
  }

  return {true, options, nullptr};
}

PhoenixBenchmarkOsrSweepExecutionStatus phoenix_benchmark_osr_sweep_run(
    const PhoenixBenchmarkOsrSweepOptions& input_options, PhoenixBenchmarkOsrSweepRowMetrics* rows,
    std::size_t row_capacity) {
  if ((rows == nullptr) || (row_capacity < k_phoenix_benchmark_osr_value_count)) {
    return {false, false, k_error_invalid_arguments, 0u};
  }

  PhoenixBenchmarkOsrSweepOptions options = input_options;
  options.apply_defaults(g_osr_defaults);

  const char* validation_message = nullptr;
  if (!options.validate(&validation_message)) {
    return {false, false, (validation_message != nullptr) ? validation_message : k_error_invalid_options, 0u};
  }

  PhoenixBenchmarkChannelMapOutputCallbacks callbacks = {nullptr, nullptr};

  uint32_t rows_generated = 0u;
  bool     has_warnings   = false;
  for (std::size_t index = 0u; index < k_phoenix_benchmark_osr_value_count; ++index) {
    const mcp356x_osr current_osr = k_osr_values[index];

    const int set_result = g_osr_setter(current_osr);
    if (set_result != MCP356X_OK) {
      return {false, has_warnings, k_error_set_osr_failed, rows_generated};
    }

    PhoenixBenchmarkStateAccumulator accumulators[k_phoenix_benchmark_channel_map_state_descriptor_count] = {};

    PhoenixBenchmarkChannelMapOptions map_options = {
        .sweep_count        = options.sweep_count,
        .has_sweep_override = true,
        .dwell_us           = options.dwell_us,
        .has_dwell_override = true,
        .wiper_code         = options.wiper_code,
        .has_wiper_override = true,
    };

    const uint32_t start_micros = g_micros_provider();

    const PhoenixBenchmarkChannelMapExecutionStatus run_status =
        g_channel_map_runner(map_options, accumulators, callbacks);
    const uint32_t end_micros = g_micros_provider();

    if (!run_status.success) {
      return {false, has_warnings, k_error_channel_map_run, rows_generated};
    }

    if (run_status.has_warnings) {
      has_warnings = true;
    }

    PhoenixBenchmarkOsrSweepRowMetrics& row = rows[rows_generated];
    row.osr_value                           = current_osr;
    row.drain                               = accumulators[0];
    row.led1                                = accumulators[1];
    row.led2                                = accumulators[2];
    row.sweep_count                         = options.sweep_count;
    row.elapsed_microseconds                = compute_elapsed_time(start_micros, end_micros);

    rows_generated += 1u;
  }

  return {true, has_warnings, nullptr, rows_generated};
}

void phoenix_benchmark_osr_sweep_set_channel_map_runner_for_test(PhoenixBenchmarkChannelMapExecutionStatus (*runner)(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks)) {
  g_channel_map_runner = (runner != nullptr) ? runner : phoenix_benchmark_channel_map_run;
}

void phoenix_benchmark_osr_sweep_set_osr_setter_for_test(int (*setter)(mcp356x_osr value)) {
  g_osr_setter = (setter != nullptr) ? setter : mcp356x_set_osr;
}

void phoenix_benchmark_osr_sweep_set_micros_provider_for_test(uint32_t (*provider)(void)) {
  g_micros_provider = (provider != nullptr) ? provider : ::micros;
}

void phoenix_benchmark_osr_sweep_clear_test_hooks(void) {
  g_channel_map_runner = phoenix_benchmark_channel_map_run;
  g_osr_setter         = mcp356x_set_osr;
  g_micros_provider    = ::micros;
}
