#ifndef PHOENIX_BENCHMARK_OSR_SWEEP_FORMATTER_HPP
#define PHOENIX_BENCHMARK_OSR_SWEEP_FORMATTER_HPP

#include <cstddef>
#include <cstdint>

constexpr std::size_t k_phoenix_benchmark_osr_sweep_summary_buffer_bytes = 512u;

struct PhoenixBenchmarkOsrSweepSummaryRowValues {
  const char* label;
  uint32_t    sample_count;
  double      drain_blue_mean;
  double      drain_blue_std;
  double      drain_blue_min;
  double      drain_blue_max;
  double      drain_green_mean;
  double      drain_green_std;
  double      drain_green_min;
  double      drain_green_max;
  double      blue_mean;
  double      blue_std;
  double      blue_min;
  double      blue_max;
  double      green_mean;
  double      green_std;
  double      green_min;
  double      green_max;
  uint32_t    sweep_duration_us;
  bool        has_metrics;
};

bool phoenix_benchmark_osr_sweep_format_summary_header(char* buffer, std::size_t length);

bool phoenix_benchmark_osr_sweep_format_summary_row(const PhoenixBenchmarkOsrSweepSummaryRowValues& values,
                                                    char* buffer, std::size_t length);

#endif  // PHOENIX_BENCHMARK_OSR_SWEEP_FORMATTER_HPP
