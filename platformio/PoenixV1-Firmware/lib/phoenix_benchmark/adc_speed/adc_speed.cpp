#include "adc_speed.hpp"

#include "adc_hal.hpp"
#include "adc_speed_command_parser.hpp"
#include <Arduino.h>

namespace {

static constexpr PhoenixBenchmarkAdcSpeedDefaults k_default_defaults = {1000u, true, true};
static PhoenixBenchmarkAdcSpeedDefaults           g_defaults         = k_default_defaults;

static bool g_adc_initialised = false;

static bool (*g_sample_provider)(PhoenixBenchmarkAdcSpeedTestMode, uint32_t, int32_t*) = nullptr;
static uint32_t (*g_micros_provider)(void)                                             = nullptr;

static constexpr uint32_t k_microseconds_per_second = 1000000u;
static constexpr uint32_t k_adc_timeout_us          = 100000u;
static constexpr uint32_t k_spi_clock_hz            = 500000UL;

#ifdef PIN_ADC_CS
static_assert(PIN_ADC_CS == 13, "adc_speed expects PIN_ADC_CS to remain 13");
#endif
static constexpr int k_pin_adc_cs = 13;

#ifdef PIN_ADC_IRQ
static_assert(PIN_ADC_IRQ == 9, "adc_speed expects PIN_ADC_IRQ to remain 9");
#endif
static constexpr int k_pin_adc_irq = 9;

static constexpr const char* k_error_invalid_options    = "adc_speed_invalid_options";
static constexpr const char* k_error_adc_initialisation = "adc_speed_adc_initialisation_failed";
static constexpr const char* k_error_sampling_failed    = "adc_speed_sampling_failed";
static constexpr const char* k_note_errors_detected     = "errors";

enum class PhoenixBenchmarkAdcSpeedRunMode : uint8_t {
  kBlocking = 0u,
  kIrq      = 1u,
};

struct PhoenixBenchmarkAdcSpeedModeMetrics {
  bool     executed      = false;
  uint32_t loop_count    = 0u;
  uint32_t success_count = 0u;
  uint32_t error_count   = 0u;
  double   elapsed_us    = 0.0;
};

uint32_t current_microseconds(void) {
  if (g_micros_provider != nullptr) {
    return g_micros_provider();
  }
  return micros();
}

uint32_t elapsed_microseconds(uint32_t start, uint32_t end) {
  if (end >= start) {
    return end - start;
  }
  return static_cast<uint32_t>(static_cast<uint64_t>(0xFFFFFFFFu - start) + 1u + end);
}

bool ensure_adc_ready(const char** error_message) {
  if (g_sample_provider != nullptr) {
    return true;
  }

  if (g_adc_initialised) {
    return true;
  }

  const AdcHalConfig config = {
      .chip_select_pin = k_pin_adc_cs,
      .spi_clock_hz    = k_spi_clock_hz,
      .irq_pin         = k_pin_adc_irq,
  };

  if (adc_hal_initialize(&config) != ADC_HAL_OK) {
    if (error_message != nullptr) {
      *error_message = k_error_adc_initialisation;
    }
    return false;
  }

  if (adc_hal_apply_default_configuration() != ADC_HAL_OK) {
    if (error_message != nullptr) {
      *error_message = k_error_adc_initialisation;
    }
    return false;
  }

  g_adc_initialised = true;
  return true;
}

bool capture_sample(PhoenixBenchmarkAdcSpeedRunMode mode, uint32_t iteration) {
  int32_t sample_code = 0;

  if (g_sample_provider != nullptr) {
    const PhoenixBenchmarkAdcSpeedTestMode test_mode = (mode == PhoenixBenchmarkAdcSpeedRunMode::kBlocking) ?
                                                           PhoenixBenchmarkAdcSpeedTestMode::kBlocking :
                                                           PhoenixBenchmarkAdcSpeedTestMode::kIrq;
    return g_sample_provider(test_mode, iteration, &sample_code);
  }

  const AdcHalChannel channel = AdcHalChannel::ADC_HAL_CHANNEL_4;
  if (mode == PhoenixBenchmarkAdcSpeedRunMode::kBlocking) {
    return adc_hal_read_single_ended(channel, k_adc_timeout_us, &sample_code) == ADC_HAL_OK;
  }
  return adc_hal_read_channel_irq(channel, k_adc_timeout_us, &sample_code) == ADC_HAL_OK;
}

bool execute_mode(const PhoenixBenchmarkAdcSpeedOptions& options, PhoenixBenchmarkAdcSpeedRunMode mode,
                  PhoenixBenchmarkAdcSpeedModeMetrics* metrics) {
  if (metrics == nullptr) {
    return false;
  }

  metrics->executed = true;

  const uint32_t start_ticks = current_microseconds();
  uint32_t       last_ticks  = start_ticks;
  const uint64_t duration_us = static_cast<uint64_t>(options.duration_ms) * 1000ULL;
  uint32_t       iteration   = 0u;
  uint32_t       guard       = 0u;

  do {
    const bool success = capture_sample(mode, iteration);
    ++metrics->loop_count;
    if (success) {
      ++metrics->success_count;
    }
    else {
      ++metrics->error_count;
    }

    ++iteration;
    last_ticks = current_microseconds();

    if (g_sample_provider == nullptr) {
      ++guard;
      if (guard > 1000000u) {
        break;
      }
    }
  } while (elapsed_microseconds(start_ticks, last_ticks) < duration_us);

  metrics->elapsed_us = static_cast<double>(elapsed_microseconds(start_ticks, last_ticks));
  return metrics->success_count > 0u;
}

}  // namespace

void PhoenixBenchmarkAdcSpeedOptions::apply_defaults(const PhoenixBenchmarkAdcSpeedDefaults& defaults) {
  if (!has_duration_override) {
    duration_ms = defaults.duration_ms;
  }
  if (!has_blocking_override) {
    enable_blocking = defaults.enable_blocking;
  }
  if (!has_irq_override) {
    enable_irq = defaults.enable_irq;
  }
}

void phoenix_benchmark_adc_speed_initialise(const PhoenixBenchmarkAdcSpeedDefaults& defaults) {
  g_defaults = defaults;
}

void phoenix_benchmark_adc_speed_reset_state(void) {
  g_defaults        = k_default_defaults;
  g_adc_initialised = false;
}

PhoenixBenchmarkAdcSpeedOptions phoenix_benchmark_adc_speed_defaults(void) {
  PhoenixBenchmarkAdcSpeedOptions options = {0u, false, false, false, false, false};
  options.apply_defaults(g_defaults);
  return options;
}

PhoenixBenchmarkAdcSpeedParseResult phoenix_benchmark_adc_speed_parse_command(const char* line) {
  PhoenixBenchmarkAdcSpeedOptions options = phoenix_benchmark_adc_speed_defaults();

  if (line == nullptr) {
    return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
  }

  const PhoenixBenchmarkAdcSpeedParseOutcome outcome = phoenix_benchmark_adc_speed_parse_command_line(line);
  if (!outcome.success) {
    return {false, options, outcome.error_message};
  }

  PhoenixBenchmarkAdcSpeedOptions parsed = outcome.options;
  parsed.apply_defaults(g_defaults);

  const char* validation_error = nullptr;
  if (!phoenix_benchmark_adc_speed_validate_options(parsed, &validation_error)) {
    if (validation_error == nullptr) {
      validation_error = k_phoenix_benchmark_adc_speed_error_invalid_value;
    }
    return {false, parsed, validation_error};
  }

  return {true, parsed, nullptr};
}

bool phoenix_benchmark_adc_speed_validate_options(const PhoenixBenchmarkAdcSpeedOptions& options,
                                                  const char**                           error_message_out) {
  const char* error_message = nullptr;

  if (options.duration_ms == 0u) {
    error_message = k_phoenix_benchmark_adc_speed_error_invalid_value;
  }
  else if (!options.enable_blocking && !options.enable_irq) {
    error_message = k_phoenix_benchmark_adc_speed_error_invalid_value;
  }

  if (error_message_out != nullptr) {
    *error_message_out = error_message;
  }

  return error_message == nullptr;
}

PhoenixBenchmarkAdcSpeedExecutionStatus phoenix_benchmark_adc_speed_run(const PhoenixBenchmarkAdcSpeedOptions& options,
                                                                        PhoenixBenchmarkStateAccumulator* accumulators,
                                                                        std::size_t accumulator_count) {
  (void) accumulators;
  (void) accumulator_count;

  PhoenixBenchmarkAdcSpeedExecutionStatus status = {};

  const char* validation_error = nullptr;
  if (!phoenix_benchmark_adc_speed_validate_options(options, &validation_error)) {
    status.success = false;
    status.message = (validation_error != nullptr) ? validation_error : k_error_invalid_options;
    return status;
  }

  if (!options.enable_blocking && !options.enable_irq) {
    status.success = false;
    status.message = k_error_invalid_options;
    return status;
  }

  if (!ensure_adc_ready(&status.message)) {
    status.success = false;
    if (status.message == nullptr) {
      status.message = k_error_adc_initialisation;
    }
    return status;
  }

  PhoenixBenchmarkAdcSpeedModeMetrics blocking_metrics = {};
  PhoenixBenchmarkAdcSpeedModeMetrics irq_metrics      = {};

  bool blocking_ok = true;
  bool irq_ok      = true;

  status.blocking_executed = options.enable_blocking;
  status.irq_executed      = options.enable_irq;

  if (options.enable_blocking) {
    blocking_ok                 = execute_mode(options, PhoenixBenchmarkAdcSpeedRunMode::kBlocking, &blocking_metrics);
    status.blocking_error_count = blocking_metrics.error_count;
    status.has_warnings         = status.has_warnings || (blocking_metrics.error_count > 0u);
    if (blocking_metrics.elapsed_us > 0.0) {
      const double loop_denominator = static_cast<double>(blocking_metrics.loop_count);
      if (blocking_metrics.success_count > 0u) {
        status.blocking_samples_per_second =
            (static_cast<double>(blocking_metrics.success_count) * static_cast<double>(k_microseconds_per_second)) /
            blocking_metrics.elapsed_us;
      }
      if (blocking_metrics.loop_count > 0u) {
        status.blocking_loop_microseconds = blocking_metrics.elapsed_us / loop_denominator;
      }
    }
    if (blocking_metrics.error_count > 0u) {
      status.blocking_note = k_note_errors_detected;
    }
  }

  if (options.enable_irq) {
    irq_ok                 = execute_mode(options, PhoenixBenchmarkAdcSpeedRunMode::kIrq, &irq_metrics);
    status.irq_error_count = irq_metrics.error_count;
    status.has_warnings    = status.has_warnings || (irq_metrics.error_count > 0u);
    if (irq_metrics.elapsed_us > 0.0) {
      const double loop_denominator = static_cast<double>(irq_metrics.loop_count);
      if (irq_metrics.success_count > 0u) {
        status.irq_samples_per_second =
            (static_cast<double>(irq_metrics.success_count) * static_cast<double>(k_microseconds_per_second)) /
            irq_metrics.elapsed_us;
      }
      if (irq_metrics.loop_count > 0u) {
        status.irq_loop_microseconds = irq_metrics.elapsed_us / loop_denominator;
      }
    }
    if (irq_metrics.error_count > 0u) {
      status.irq_note = k_note_errors_detected;
    }
  }

  status.success = blocking_ok && irq_ok;
  if (!status.success) {
    status.message = k_error_sampling_failed;
  }

  return status;
}
void phoenix_benchmark_adc_speed_set_sample_provider_for_test(bool (*provider)(PhoenixBenchmarkAdcSpeedTestMode mode,
                                                                               uint32_t iteration,
                                                                               int32_t* out_sample)) {
  g_sample_provider = provider;
}

void phoenix_benchmark_adc_speed_clear_sample_provider_for_test(void) {
  g_sample_provider = nullptr;
}

void phoenix_benchmark_adc_speed_set_micros_provider_for_test(uint32_t (*provider)(void)) {
  g_micros_provider = provider;
}

void phoenix_benchmark_adc_speed_clear_micros_provider_for_test(void) {
  g_micros_provider = nullptr;
}
