#ifndef PHOENIX_BENCHMARK_ADC_SPEED_HPP
#define PHOENIX_BENCHMARK_ADC_SPEED_HPP

#include "adc_speed_command_parser.hpp"
#include "adc_speed_formatter.hpp"
#include "core/phoenix_benchmark_core.hpp"
#include <cstddef>
#include <cstdint>

struct PhoenixBenchmarkAdcSpeedDefaults {
  uint32_t duration_ms;
  bool     enable_blocking;
  bool     enable_irq;
};

struct PhoenixBenchmarkAdcSpeedExecutionStatus {
  bool        success;
  bool        has_warnings;
  const char* message;
  double      blocking_samples_per_second;
  double      blocking_loop_microseconds;
  uint32_t    blocking_error_count;
  double      irq_samples_per_second;
  double      irq_loop_microseconds;
  uint32_t    irq_error_count;
};

void phoenix_benchmark_adc_speed_initialise(const PhoenixBenchmarkAdcSpeedDefaults& defaults);

void phoenix_benchmark_adc_speed_reset_state(void);

PhoenixBenchmarkAdcSpeedOptions phoenix_benchmark_adc_speed_defaults(void);

PhoenixBenchmarkAdcSpeedExecutionStatus phoenix_benchmark_adc_speed_run(const PhoenixBenchmarkAdcSpeedOptions& options,
                                                                        PhoenixBenchmarkStateAccumulator* accumulators,
                                                                        std::size_t accumulator_count);

#endif  // PHOENIX_BENCHMARK_ADC_SPEED_HPP
