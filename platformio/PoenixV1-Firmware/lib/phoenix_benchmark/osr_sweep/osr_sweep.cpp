#include "osr_sweep.hpp"

#include "../../light_readings/light_readings.hpp"
#include "../../mcp356x/mcp356x.hpp"
#include "../channel_map/channel_map.hpp"
#include "../channel_map/command_parser.hpp"
#include "../core/phoenix_benchmark_core.hpp"
#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <device_setup.hpp>
#include <limits>

namespace {

constexpr PhoenixBenchmarkOsrSweepDefaults k_default_osr_sweep_defaults = {
    .sweep_count = 10u,
    .dwell_us    = 100u,
    .wiper_code  = 0x00u,
};

constexpr const char* k_error_invalid_arguments    = "invalid arguments";
constexpr const char* k_error_invalid_options      = "invalid options";
constexpr const char* k_error_set_osr_failed       = "osr configuration failed";
constexpr const char* k_error_light_initialisation = "hardware initialisation failed";
constexpr const char* k_error_light_runtime_update = "light readings runtime update failed";
constexpr const char* k_error_light_sweep          = "sampling failed";
constexpr const char* k_error_light_stats          = "statistics computation failed";
constexpr const char* k_error_restore_osr_failed   = "osr restore failed";
constexpr const char* k_error_invalid_row_capacity = "invalid arguments";
constexpr const char* k_error_light_shutdown       = "light readings shutdown failed";

typedef int (*OsrSetter)(mcp356x_osr value);

typedef uint32_t (*MicrosProvider)(void);

typedef bool (*HardwareReadyChecker)(void);

PhoenixBenchmarkOsrSweepDefaults g_osr_defaults    = k_default_osr_sweep_defaults;
OsrSetter                        g_osr_setter      = mcp356x_set_osr;
MicrosProvider                   g_micros_provider = ::micros;
HardwareReadyChecker             g_hardware_ready  = phoenix_benchmark_channel_map_ensure_hardware_ready;

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
  auto finalize_status = [](bool attempted_osr_updates, PhoenixBenchmarkOsrSweepExecutionStatus status) {
    if (attempted_osr_updates && (g_osr_setter != nullptr)) {
      const int restore_result = g_osr_setter(mcp356x_osr::osr_4096);
      if (restore_result != MCP356X_OK) {
        status.has_warnings = true;
        if (!status.success && (status.message == nullptr)) {
          status.message = k_error_restore_osr_failed;
        }
      }
    }
    return status;
  };

  if ((rows == nullptr) || (row_capacity < k_phoenix_benchmark_osr_value_count)) {
    return finalize_status(false, {false, false, k_error_invalid_row_capacity, 0u});
  }

  PhoenixBenchmarkOsrSweepOptions options = input_options;
  options.apply_defaults(g_osr_defaults);

  const char* validation_message = nullptr;
  if (!options.validate(&validation_message)) {
    return finalize_status(
        false, {false, false, (validation_message != nullptr) ? validation_message : k_error_invalid_options, 0u});
  }

  if (!g_hardware_ready()) {
    return finalize_status(false, {false, false, k_error_light_initialisation, 0u});
  }

  LightReadingsConfig light_config = g_device_light_readings_config;
  if (light_readings_initialize(&light_config) != LIGHT_READINGS_OK) {
    return finalize_status(false, {false, false, k_error_light_initialisation, 0u});
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

  LightReadingsRuntimeSettings runtime_settings = {
      .apply_dwell_override = true,
      .dwell_us             = options.dwell_us,
      .apply_wiper_override = true,
      .wiper_code           = options.wiper_code,
  };

  if (light_readings_modify_settings(&runtime_settings) != LIGHT_READINGS_OK) {
    (void) shutdown_light_readings();
    return finalize_status(false, {false, false, k_error_light_runtime_update, 0u});
  }

  LightReadingsSweepCollection sweep_collection = {
      .sweep_count = 0u,
      .sweeps      = g_light_readings_sweep_storage,
  };

  uint32_t rows_generated        = 0u;
  bool     has_warnings          = false;
  bool     attempted_osr_updates = false;
  for (std::size_t index = 0u; index < k_phoenix_benchmark_osr_value_count; ++index) {
    const mcp356x_osr current_osr = k_osr_values[index];

    const int set_result = g_osr_setter(current_osr);
    if (set_result != MCP356X_OK) {
      return finalize_status(attempted_osr_updates, {false, has_warnings, k_error_set_osr_failed, rows_generated});
    }
    attempted_osr_updates = true;

    const uint32_t start_micros = g_micros_provider();

    const int      sweep_result = light_readings_sweep_n(options.sweep_count, &sweep_collection);
    const uint32_t end_micros   = g_micros_provider();

    if (sweep_result != LIGHT_READINGS_OK) {
      (void) shutdown_light_readings();
      return finalize_status(attempted_osr_updates, {false, has_warnings, k_error_light_sweep, rows_generated});
    }

    LightReadingsSweepStats sweep_stats = {};
    if (light_readings_compute_sweep_stats(&sweep_collection, &sweep_stats) != LIGHT_READINGS_OK) {
      (void) shutdown_light_readings();
      return finalize_status(attempted_osr_updates, {false, has_warnings, k_error_light_stats, rows_generated});
    }

    PhoenixBenchmarkOsrSweepRowMetrics& row = rows[rows_generated];
    row                                     = PhoenixBenchmarkOsrSweepRowMetrics{};
    row.osr_value                           = current_osr;
    row.sweep_count                         = sweep_collection.sweep_count;
    row.elapsed_microseconds                = compute_elapsed_time(start_micros, end_micros);

    auto assign_summary = [](const LightReadingsStatisticSummary&   summary,
                             PhoenixBenchmarkRunningStats<int32_t>& destination) {
      destination.count = summary.sample_count;

      if (!summary.has_samples) {
        destination.mean      = 0.0;
        destination.m2        = 0.0;
        destination.min_value = std::numeric_limits<int32_t>::max();
        destination.max_value = std::numeric_limits<int32_t>::lowest();
        return;
      }

      destination.mean      = summary.mean;
      destination.m2        = (summary.sample_count > 1u) ? (summary.standard_deviation * summary.standard_deviation *
                                                      static_cast<double>(summary.sample_count - 1u)) :
                                                            0.0;
      destination.min_value = summary.min_value;
      destination.max_value = summary.max_value;
    };

    assign_summary(sweep_stats.drain_blue, row.drain.channel_a_codes);
    assign_summary(sweep_stats.drain_green, row.drain.channel_b_codes);
    assign_summary(sweep_stats.blue, row.blue.channel_a_codes);
    assign_summary(sweep_stats.drain_green, row.blue.channel_b_codes);
    assign_summary(sweep_stats.drain_blue, row.green.channel_a_codes);
    assign_summary(sweep_stats.green, row.green.channel_b_codes);

    row.drain.channel_a_saturation_count = 0u;
    row.drain.channel_b_saturation_count = 0u;
    row.blue.channel_a_saturation_count  = 0u;
    row.blue.channel_b_saturation_count  = 0u;
    row.green.channel_a_saturation_count = 0u;
    row.green.channel_b_saturation_count = 0u;

    bool saturation_detected = false;
    if ((sweep_collection.sweeps != nullptr) && (sweep_collection.sweep_count > 0u)) {
      for (uint32_t sample_index = 0u; sample_index < sweep_collection.sweep_count; ++sample_index) {
        const LightReadingsSweepSample& sample = sweep_collection.sweeps[sample_index];

        if (phoenix_benchmark_is_adc_code_saturated(sample.drain_blue_code)) {
          ++row.drain.channel_a_saturation_count;
          ++row.green.channel_a_saturation_count;
          saturation_detected = true;
        }
        if (phoenix_benchmark_is_adc_code_saturated(sample.drain_green_code)) {
          ++row.drain.channel_b_saturation_count;
          ++row.blue.channel_b_saturation_count;
          saturation_detected = true;
        }
        if (phoenix_benchmark_is_adc_code_saturated(sample.blue_code)) {
          ++row.blue.channel_a_saturation_count;
          saturation_detected = true;
        }
        if (phoenix_benchmark_is_adc_code_saturated(sample.green_code)) {
          ++row.green.channel_b_saturation_count;
          saturation_detected = true;
        }
      }
    }

    const bool adc_reported_saturation = light_readings_last_sweep_detected_saturation();

    if (saturation_detected || adc_reported_saturation) {
      has_warnings = true;
    }

    rows_generated += 1u;
  }

  const int shutdown_result = shutdown_light_readings();
  if (shutdown_result != LIGHT_READINGS_OK) {
    has_warnings = true;
  }

  return finalize_status(attempted_osr_updates,
                         {true, has_warnings || (shutdown_result != LIGHT_READINGS_OK),
                          (shutdown_result == LIGHT_READINGS_OK) ? nullptr : k_error_light_shutdown, rows_generated});
}

void phoenix_benchmark_osr_sweep_set_osr_setter_for_test(int (*setter)(mcp356x_osr value)) {
  g_osr_setter = (setter != nullptr) ? setter : mcp356x_set_osr;
}

void phoenix_benchmark_osr_sweep_set_micros_provider_for_test(uint32_t (*provider)(void)) {
  g_micros_provider = (provider != nullptr) ? provider : ::micros;
}

void phoenix_benchmark_osr_sweep_set_hardware_ready_checker_for_test(bool (*checker)(void)) {
  g_hardware_ready = (checker != nullptr) ? checker : phoenix_benchmark_channel_map_ensure_hardware_ready;
}

void phoenix_benchmark_osr_sweep_clear_test_hooks(void) {
  g_osr_setter      = mcp356x_set_osr;
  g_micros_provider = ::micros;
  g_hardware_ready  = phoenix_benchmark_channel_map_ensure_hardware_ready;
}
