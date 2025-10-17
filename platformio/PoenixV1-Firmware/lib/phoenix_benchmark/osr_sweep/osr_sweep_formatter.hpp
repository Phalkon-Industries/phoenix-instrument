#ifndef PHOENIX_BENCHMARK_OSR_SWEEP_FORMATTER_HPP
#define PHOENIX_BENCHMARK_OSR_SWEEP_FORMATTER_HPP

#include <cstddef>
#include <cstdint>

constexpr std::size_t k_phoenix_benchmark_osr_sweep_summary_buffer_bytes = 192u;

struct PhoenixBenchmarkOsrSweepSummaryRowValues {
  const char* label;
  uint32_t    sample_count;
  double      drain_mean;
  double      drain_std;
  double      drain_min;
  double      drain_max;
  double      led1_mean;
  double      led1_std;
  double      led1_min;
  double      led1_max;
  double      led2_mean;
  double      led2_std;
  double      led2_min;
  double      led2_max;
  uint32_t    sweep_duration_us;
  bool        has_metrics;
};

bool phoenix_benchmark_osr_sweep_format_summary_header(char* buffer, std::size_t length);

bool phoenix_benchmark_osr_sweep_format_summary_row(const PhoenixBenchmarkOsrSweepSummaryRowValues& values,
                                                    char* buffer, std::size_t length);

#endif  // PHOENIX_BENCHMARK_OSR_SWEEP_FORMATTER_HPP
