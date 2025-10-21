#ifndef PHOENIX_BENCHMARK_DWELL_SWEEP_FORMATTER_HPP
#define PHOENIX_BENCHMARK_DWELL_SWEEP_FORMATTER_HPP

#include <cstddef>
#include <cstdint>

constexpr std::size_t k_phoenix_benchmark_dwell_sweep_summary_buffer_bytes = 192u;

struct PhoenixBenchmarkDwellSweepSummaryRowValues {
  uint32_t dwell_us;
  uint32_t sweeps_completed;
  double   drain_mean;
  double   drain_std;
  double   led1_mean;
  double   led1_std;
  double   led2_mean;
  double   led2_std;
  uint32_t duration_us;
  uint8_t  warning_mask;
  bool     has_metrics;
};

bool phoenix_benchmark_dwell_sweep_format_summary_header(char* buffer, std::size_t length);

bool phoenix_benchmark_dwell_sweep_format_summary_row(const PhoenixBenchmarkDwellSweepSummaryRowValues& values,
                                                      char* buffer, std::size_t length);

#endif  // PHOENIX_BENCHMARK_DWELL_SWEEP_FORMATTER_HPP
