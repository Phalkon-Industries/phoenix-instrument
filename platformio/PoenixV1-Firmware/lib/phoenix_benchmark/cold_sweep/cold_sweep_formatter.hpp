#ifndef PHOENIX_BENCHMARK_COLD_SWEEP_FORMATTER_HPP
#define PHOENIX_BENCHMARK_COLD_SWEEP_FORMATTER_HPP

#include <cstddef>
#include <cstdint>

constexpr std::size_t k_phoenix_benchmark_cold_sweep_summary_buffer_bytes = 128u;
constexpr std::size_t k_phoenix_benchmark_cold_sweep_sample_buffer_bytes  = 128u;

constexpr uint8_t k_phoenix_benchmark_cold_sweep_saturation_drain_blue  = 0x01u;
constexpr uint8_t k_phoenix_benchmark_cold_sweep_saturation_drain_green = 0x02u;
constexpr uint8_t k_phoenix_benchmark_cold_sweep_saturation_blue        = 0x04u;
constexpr uint8_t k_phoenix_benchmark_cold_sweep_saturation_green       = 0x08u;

struct PhoenixBenchmarkColdSweepSummaryRowValues {
  const char* label;
  uint32_t    sample_count;
  double      mean;
  double      standard_deviation;
  int32_t     min_code;
  int32_t     max_code;
  bool        has_samples;
  bool        saturated;
};

struct PhoenixBenchmarkColdSweepSampleRowValues {
  uint32_t sweep_index;
  int32_t  drain_blue_code;
  int32_t  drain_green_code;
  int32_t  blue_code;
  int32_t  green_code;
  uint8_t  saturation_mask;
};

bool phoenix_benchmark_cold_sweep_format_summary_header(char* buffer, std::size_t length);
bool phoenix_benchmark_cold_sweep_format_summary_row(const PhoenixBenchmarkColdSweepSummaryRowValues& values,
                                                     char* buffer, std::size_t length);

bool phoenix_benchmark_cold_sweep_format_sample_header(char* buffer, std::size_t length);
bool phoenix_benchmark_cold_sweep_format_sample_row(const PhoenixBenchmarkColdSweepSampleRowValues& values,
                                                    char* buffer, std::size_t length);

#endif  // PHOENIX_BENCHMARK_COLD_SWEEP_FORMATTER_HPP
