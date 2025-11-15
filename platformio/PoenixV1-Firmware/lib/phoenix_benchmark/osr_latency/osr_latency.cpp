#include "osr_latency.hpp"

#include "../../adc_hal/adc_hal.hpp"
#include "../channel_map/channel_map.hpp"
#include "osr_latency_command_parser.hpp"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {

bool measure_osr_latency_blocking(mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count,
                                  PhoenixBenchmarkRunningStats<uint32_t>* stats_out);
bool measure_osr_latency_irq(mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count,
                             PhoenixBenchmarkRunningStats<uint32_t>* stats_out);
void install_default_samplers(void);

static constexpr PhoenixBenchmarkOsrLatencyDefaults k_default_defaults = {1u, 8u, true, true};
static PhoenixBenchmarkOsrLatencyDefaults           g_defaults         = k_default_defaults;

static bool (*g_blocking_sampler)(mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count,
                                  PhoenixBenchmarkRunningStats<uint32_t>* stats_out) = measure_osr_latency_blocking;
static bool (*g_irq_sampler)(mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count,
                             PhoenixBenchmarkRunningStats<uint32_t>* stats_out)      = measure_osr_latency_irq;

static constexpr AdcHalChannel k_latency_channel    = AdcHalChannel::ADC_HAL_CHANNEL_4;
static constexpr uint32_t      k_latency_timeout_us = 5000000u;

uint32_t elapsed_microseconds(uint32_t start, uint32_t end) {
  if (end >= start) {
    return end - start;
  }
  return static_cast<uint32_t>((0xFFFFFFFFu - start) + 1u + end);
}

bool ensure_latency_hardware_ready(void) {
  // Step 1: Surface the shared channel-map guard to ensure power rails are active.
  return phoenix_benchmark_channel_map_ensure_hardware_ready();
}

bool capture_single_conversion(mcp356x_sampling_mode mode, uint32_t* elapsed_us) {
  // Step 1: Capture the starting tick so we can compute the conversion latency.
  int32_t        sample_code = 0;
  const uint32_t start_ticks = micros();
  // Step 2: Issue the conversion using the requested sampling mode.
  const int      hal_result = (mode == mcp356x_sampling_mode::blocking) ?
                                  adc_hal_read_single_ended(k_latency_channel, k_latency_timeout_us, &sample_code) :
                                  adc_hal_read_channel_irq(k_latency_channel, k_latency_timeout_us, &sample_code);
  const uint32_t end_ticks  = micros();

  if (hal_result != ADC_HAL_OK) {
    return false;
  }

  if (elapsed_us != nullptr) {
    // Step 3: Convert the tick delta into a microsecond duration for the statistics helper.
    *elapsed_us = elapsed_microseconds(start_ticks, end_ticks);
  }

  return true;
}

bool capture_latency_common(mcp356x_osr osr, mcp356x_sampling_mode mode, uint32_t warmup_count, uint32_t sample_count,
                            PhoenixBenchmarkRunningStats<uint32_t>* stats_out) {
  // Step 1: Ensure the analog front-end is powered before measuring latency.
  if (!ensure_latency_hardware_ready()) {
    return false;
  }

  // Step 2: Remember the active OSR so we can restore it after sampling.
  mcp356x_osr previous_osr     = osr;
  const bool  restore_required = (mcp356x_get_osr(&previous_osr) == MCP356X_OK);

  // Step 3: Program the requested OSR so conversions mirror the benchmark row.
  if (mcp356x_set_osr(osr) != MCP356X_OK) {
    return false;
  }

  bool success = true;

  // Step 4: Execute warm-up conversions to clear stale pipeline data.
  for (uint32_t index = 0u; (index < warmup_count) && success; ++index) {
    success = capture_single_conversion(mode, nullptr);
  }

  // Step 5: Capture timed conversions and stream them into the running statistics.
  if (success && (sample_count > 0u)) {
    if (stats_out != nullptr) {
      for (uint32_t index = 0u; index < sample_count; ++index) {
        uint32_t latency_us = 0u;
        if (!capture_single_conversion(mode, &latency_us)) {
          success = false;
          break;
        }
        stats_out->update(latency_us);
      }
    }
    else {
      for (uint32_t index = 0u; index < sample_count; ++index) {
        if (!capture_single_conversion(mode, nullptr)) {
          success = false;
          break;
        }
      }
    }
  }

  // Step 6: Restore the prior OSR so other benchmarks retain their configuration.
  if (restore_required) {
    if (mcp356x_set_osr(previous_osr) != MCP356X_OK) {
      success = false;
    }
  }

  return success;
}

bool measure_osr_latency_blocking(mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count,
                                  PhoenixBenchmarkRunningStats<uint32_t>* stats_out) {
  return capture_latency_common(osr, mcp356x_sampling_mode::blocking, warmup_count, sample_count, stats_out);
}

bool measure_osr_latency_irq(mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count,
                             PhoenixBenchmarkRunningStats<uint32_t>* stats_out) {
  return capture_latency_common(osr, mcp356x_sampling_mode::irq, warmup_count, sample_count, stats_out);
}

void install_default_samplers(void) {
  g_blocking_sampler = measure_osr_latency_blocking;
  g_irq_sampler      = measure_osr_latency_irq;
}

static constexpr const char k_error_invalid_arguments[] = "osr_latency_invalid_arguments";
static constexpr const char k_error_invalid_options[]   = "osr_latency_invalid_options";
static constexpr const char k_error_sampling_failed[]   = "osr_latency_sampling_failed";

struct PhoenixBenchmarkOsrEntry {
  mcp356x_osr osr_enum;
  uint32_t    osr_value;
};

static constexpr PhoenixBenchmarkOsrEntry k_osr_entries[] = {
    {mcp356x_osr::osr_32, 32u},       {mcp356x_osr::osr_64, 64u},       {mcp356x_osr::osr_128, 128u},
    {mcp356x_osr::osr_256, 256u},     {mcp356x_osr::osr_512, 512u},     {mcp356x_osr::osr_1024, 1024u},
    {mcp356x_osr::osr_2048, 2048u},   {mcp356x_osr::osr_4096, 4096u},   {mcp356x_osr::osr_8192, 8192u},
    {mcp356x_osr::osr_16384, 16384u}, {mcp356x_osr::osr_20480, 20480u}, {mcp356x_osr::osr_24576, 24576u},
    {mcp356x_osr::osr_40960, 40960u}, {mcp356x_osr::osr_49152, 49152u}, {mcp356x_osr::osr_81920, 81920u},
    {mcp356x_osr::osr_98304, 98304u},
};

bool run_sampler(mcp356x_osr osr, mcp356x_sampling_mode mode, const PhoenixBenchmarkOsrLatencyOptions& options,
                 PhoenixBenchmarkRunningStats<uint32_t>* stats_out) {
  // Step 1: Choose the sampler implementation corresponding to the requested mode.
  bool (*sampler)(mcp356x_osr, uint32_t, uint32_t, PhoenixBenchmarkRunningStats<uint32_t>*) = nullptr;

  if (mode == mcp356x_sampling_mode::blocking) {
    sampler = g_blocking_sampler;
  }
  else {
    sampler = g_irq_sampler;
  }

  if (sampler != nullptr) {
    // Step 2: Invoke the registered sampler so hardware-backed measurements populate the stats.
    return sampler(osr, options.warmup_count, options.sample_count, stats_out);
  }

  if ((stats_out != nullptr) && (options.sample_count > 0u)) {
    // Step 3: Fall back to the lookup table when tests override the sampler hooks.
    const uint32_t estimate = mcp356x_estimate_conversion_delay(osr, mode);
    if (estimate > 0u) {
      stats_out->update(estimate);
    }
  }

  return true;
}

void emit_summary_line(const PhoenixBenchmarkOsrLatencyMeasurement& measurement, const char* mode_label,
                       const PhoenixBenchmarkOsrLatencyOutputCallbacks& callbacks) {
  if (callbacks.summary_writer == nullptr) {
    return;
  }

  // Step 1: Produce a human-readable OSR label for the output table.
  char osr_label[16] = {};
  std::snprintf(osr_label, sizeof(osr_label), "OSR%lu", static_cast<unsigned long>(measurement.osr_value));

  PhoenixBenchmarkOsrLatencySummaryRowValues row_values = {};
  row_values.osr_label                                  = osr_label;
  row_values.mode_label                                 = mode_label;
  row_values.sample_count                               = measurement.sample_count;
  row_values.has_metrics                                = measurement.latency_us.has_samples();

  if (row_values.has_metrics) {
    // Step 2: Populate the statistics columns when the sampler captured data.
    row_values.mean_us   = measurement.latency_us.mean;
    row_values.stddev_us = measurement.latency_us.standard_deviation();
    row_values.min_us    = measurement.latency_us.min_value;
    row_values.max_us    = measurement.latency_us.max_value;
  }

  char buffer[k_phoenix_benchmark_osr_latency_summary_buffer_bytes] = {};
  if (phoenix_benchmark_osr_latency_format_summary_row(row_values, buffer, sizeof(buffer))) {
    // Step 3: Emit the formatted row through the registered summary callback.
    callbacks.summary_writer(buffer);
  }
}

}  // namespace

void PhoenixBenchmarkOsrLatencyOptions::apply_defaults(const PhoenixBenchmarkOsrLatencyDefaults& defaults) {
  if (!has_warmup_override) {
    warmup_count = defaults.warmup_count;
  }
  if (!has_sample_override) {
    sample_count = defaults.sample_count;
  }
  if (!has_blocking_override) {
    include_blocking = defaults.include_blocking;
  }
  if (!has_irq_override) {
    include_irq = defaults.include_irq;
  }
}

bool PhoenixBenchmarkOsrLatencyOptions::validate(const char** error_message) const {
  const char* validation_error = nullptr;

  if (sample_count == 0u) {
    validation_error = k_phoenix_benchmark_osr_latency_error_invalid_value;
  }
  else if (!include_blocking && !include_irq) {
    validation_error = k_phoenix_benchmark_osr_latency_error_invalid_value;
  }

  if (error_message != nullptr) {
    *error_message = validation_error;
  }

  return validation_error == nullptr;
}

void phoenix_benchmark_osr_latency_initialise(const PhoenixBenchmarkOsrLatencyDefaults& defaults) {
  g_defaults = defaults;
}

void phoenix_benchmark_osr_latency_reset_state(void) {
  g_defaults = k_default_defaults;
  install_default_samplers();
}

PhoenixBenchmarkOsrLatencyOptions phoenix_benchmark_osr_latency_defaults(void) {
  PhoenixBenchmarkOsrLatencyOptions options = {0u, 0u, false, false, false, false, false, false};
  options.apply_defaults(g_defaults);
  return options;
}

PhoenixBenchmarkOsrLatencyParseResult phoenix_benchmark_osr_latency_parse_command(const char* line) {
  PhoenixBenchmarkOsrLatencyOptions options = phoenix_benchmark_osr_latency_defaults();

  if (line == nullptr) {
    return {false, options, k_phoenix_benchmark_osr_latency_error_invalid_command};
  }

  const PhoenixBenchmarkOsrLatencyParseOutcome outcome = phoenix_benchmark_osr_latency_parse_command_line(line);
  if (!outcome.success) {
    PhoenixBenchmarkOsrLatencyOptions parsed_failure = options;
    if (outcome.options.has_sample_override || outcome.options.has_warmup_override ||
        outcome.options.has_blocking_override || outcome.options.has_irq_override) {
      parsed_failure = outcome.options;
      parsed_failure.apply_defaults(g_defaults);
    }
    return {false, parsed_failure, outcome.error_message};
  }

  PhoenixBenchmarkOsrLatencyOptions parsed_options = outcome.options;
  parsed_options.apply_defaults(g_defaults);

  const char* validation_error = nullptr;
  if (!parsed_options.validate(&validation_error)) {
    if (validation_error == nullptr) {
      validation_error = k_phoenix_benchmark_osr_latency_error_invalid_value;
    }
    return {false, parsed_options, validation_error};
  }

  return {true, parsed_options, nullptr};
}

PhoenixBenchmarkOsrLatencyExecutionStatus phoenix_benchmark_osr_latency_run(
    const PhoenixBenchmarkOsrLatencyOptions& options, PhoenixBenchmarkOsrLatencyMeasurement* rows,
    std::size_t row_capacity, const PhoenixBenchmarkOsrLatencyOutputCallbacks& callbacks) {
  PhoenixBenchmarkOsrLatencyExecutionStatus status = {};

  const char* validation_error = nullptr;
  if (!options.validate(&validation_error)) {
    status.success = false;
    status.message = (validation_error != nullptr) ? validation_error : k_error_invalid_options;
    return status;
  }

  if ((rows == nullptr) || (row_capacity == 0u)) {
    status.success = false;
    status.message = k_error_invalid_arguments;
    return status;
  }

  bool        overall_success = true;
  std::size_t row_index       = 0u;

  if (callbacks.summary_writer != nullptr) {
    char header[k_phoenix_benchmark_osr_latency_summary_buffer_bytes] = {};
    if (phoenix_benchmark_osr_latency_format_summary_header(header, sizeof(header))) {
      callbacks.summary_writer(header);
    }
  }

  for (const PhoenixBenchmarkOsrEntry& entry : k_osr_entries) {
    if (options.include_blocking) {
      if (row_index < row_capacity) {
        PhoenixBenchmarkOsrLatencyMeasurement& measurement = rows[row_index];
        measurement                                        = {};
        measurement.osr_value                              = entry.osr_value;
        measurement.mode                                   = mcp356x_sampling_mode::blocking;
        measurement.warmup_count                           = options.warmup_count;
        measurement.sample_count                           = options.sample_count;
        measurement.executed  = run_sampler(entry.osr_enum, measurement.mode, options, &measurement.latency_us);
        status.rows_generated = static_cast<uint32_t>(row_index + 1u);
        if (!measurement.executed) {
          overall_success = false;
          status.message  = k_error_sampling_failed;
        }
        emit_summary_line(measurement, "blocking", callbacks);
        ++row_index;
      }
      else {
        status.has_warnings = true;
      }
    }

    if (options.include_irq) {
      if (row_index < row_capacity) {
        PhoenixBenchmarkOsrLatencyMeasurement& measurement = rows[row_index];
        measurement                                        = {};
        measurement.osr_value                              = entry.osr_value;
        measurement.mode                                   = mcp356x_sampling_mode::irq;
        measurement.warmup_count                           = options.warmup_count;
        measurement.sample_count                           = options.sample_count;
        measurement.executed  = run_sampler(entry.osr_enum, measurement.mode, options, &measurement.latency_us);
        status.rows_generated = static_cast<uint32_t>(row_index + 1u);
        if (!measurement.executed) {
          overall_success = false;
          status.message  = k_error_sampling_failed;
        }
        emit_summary_line(measurement, "irq", callbacks);
        ++row_index;
      }
      else {
        status.has_warnings = true;
      }
    }
  }

  status.success = overall_success && (status.rows_generated > 0u);
  return status;
}

void phoenix_benchmark_osr_latency_set_blocking_sampler_for_test(bool (*sampler)(
    mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count, PhoenixBenchmarkRunningStats<uint32_t>* stats_out)) {
  g_blocking_sampler = sampler;
}

void phoenix_benchmark_osr_latency_set_irq_sampler_for_test(bool (*sampler)(
    mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count, PhoenixBenchmarkRunningStats<uint32_t>* stats_out)) {
  g_irq_sampler = sampler;
}

void phoenix_benchmark_osr_latency_clear_test_hooks(void) {
  install_default_samplers();
}
