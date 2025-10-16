#ifndef PHOENIX_BENCHMARK_ADC_SPEED_HPP
#define PHOENIX_BENCHMARK_ADC_SPEED_HPP

#include "adc_speed_formatter.hpp"
#include "core/phoenix_benchmark_core.hpp"
#include <cstddef>
#include <cstdint>

struct PhoenixBenchmarkAdcSpeedDefaults;

struct PhoenixBenchmarkAdcSpeedOptions {
  uint32_t duration_ms;
  bool     enable_blocking;
  bool     enable_irq;
  bool     has_duration_override;
  bool     has_blocking_override;
  bool     has_irq_override;

  void apply_defaults(const PhoenixBenchmarkAdcSpeedDefaults& defaults);
};

struct PhoenixBenchmarkAdcSpeedDefaults {
  uint32_t duration_ms;
  bool     enable_blocking;
  bool     enable_irq;
};

struct PhoenixBenchmarkAdcSpeedExecutionStatus {
  bool        success;
  bool        has_warnings;
  const char* message;
  bool        blocking_executed;
  double      blocking_samples_per_second;
  double      blocking_loop_microseconds;
  uint32_t    blocking_error_count;
  const char* blocking_note;
  bool        irq_executed;
  double      irq_samples_per_second;
  double      irq_loop_microseconds;
  uint32_t    irq_error_count;
  const char* irq_note;
};

void phoenix_benchmark_adc_speed_initialise(const PhoenixBenchmarkAdcSpeedDefaults& defaults);

void phoenix_benchmark_adc_speed_reset_state(void);

PhoenixBenchmarkAdcSpeedOptions phoenix_benchmark_adc_speed_defaults(void);

PhoenixBenchmarkAdcSpeedExecutionStatus phoenix_benchmark_adc_speed_run(const PhoenixBenchmarkAdcSpeedOptions& options,
                                                                        PhoenixBenchmarkStateAccumulator* accumulators,
                                                                        std::size_t accumulator_count);

struct PhoenixBenchmarkAdcSpeedParseResult {
  bool                            success;
  PhoenixBenchmarkAdcSpeedOptions options;
  const char*                     error_message;
};

PhoenixBenchmarkAdcSpeedParseResult phoenix_benchmark_adc_speed_parse_command(const char* line);

bool phoenix_benchmark_adc_speed_validate_options(const PhoenixBenchmarkAdcSpeedOptions& options,
                                                  const char**                           error_message_out);

enum class PhoenixBenchmarkAdcSpeedTestMode : uint8_t {
  kBlocking = 0u,
  kIrq      = 1u,
};

#if defined(UNIT_TEST)
void phoenix_benchmark_adc_speed_set_sample_provider_for_test(bool (*provider)(PhoenixBenchmarkAdcSpeedTestMode mode,
                                                                               uint32_t iteration,
                                                                               int32_t* out_sample));

void phoenix_benchmark_adc_speed_clear_sample_provider_for_test(void);

void phoenix_benchmark_adc_speed_set_micros_provider_for_test(uint32_t (*provider)(void));

void phoenix_benchmark_adc_speed_clear_micros_provider_for_test(void);
#endif  // defined(UNIT_TEST)

#endif  // PHOENIX_BENCHMARK_ADC_SPEED_HPP
