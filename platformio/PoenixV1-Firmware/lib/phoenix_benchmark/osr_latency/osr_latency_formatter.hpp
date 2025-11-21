#ifndef PHOENIX_BENCHMARK_OSR_LATENCY_FORMATTER_HPP
#define PHOENIX_BENCHMARK_OSR_LATENCY_FORMATTER_HPP

#include <cstddef>
#include <cstdint>

static constexpr std::size_t k_phoenix_benchmark_osr_latency_summary_buffer_bytes = 128u;

struct PhoenixBenchmarkOsrLatencySummaryRowValues {
  const char* osr_label;
  const char* mode_label;
  uint32_t    sample_count;
  double      mean_us;
  double      stddev_us;
  uint32_t    min_us;
  uint32_t    max_us;
  bool        has_metrics;
};

bool phoenix_benchmark_osr_latency_format_summary_header(char* buffer, std::size_t buffer_length);

bool phoenix_benchmark_osr_latency_format_summary_row(const PhoenixBenchmarkOsrLatencySummaryRowValues& values,
                                                      char* buffer, std::size_t buffer_length);

#endif  // PHOENIX_BENCHMARK_OSR_LATENCY_FORMATTER_HPP
