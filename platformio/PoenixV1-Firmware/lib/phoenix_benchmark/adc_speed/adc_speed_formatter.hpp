#ifndef PHOENIX_BENCHMARK_ADC_SPEED_FORMATTER_HPP
#define PHOENIX_BENCHMARK_ADC_SPEED_FORMATTER_HPP

#include <cstddef>
#include <cstdint>

static constexpr std::size_t k_phoenix_benchmark_adc_speed_summary_mode_width    = 10u;
static constexpr std::size_t k_phoenix_benchmark_adc_speed_summary_rate_width    = 16u;
static constexpr std::size_t k_phoenix_benchmark_adc_speed_summary_loop_width    = 14u;
static constexpr std::size_t k_phoenix_benchmark_adc_speed_summary_error_width   = 10u;
static constexpr std::size_t k_phoenix_benchmark_adc_speed_summary_notes_width   = 18u;
static constexpr std::size_t k_phoenix_benchmark_adc_speed_summary_buffer_bytes  = 160u;

struct PhoenixBenchmarkAdcSpeedSummaryRowValues {
  const char* mode_label;
  double      samples_per_second;
  double      loop_microseconds;
  uint32_t    error_count;
  const char* notes;
  bool        has_metrics;
};

bool phoenix_benchmark_adc_speed_format_summary_header(char* buffer, std::size_t buffer_length);

bool phoenix_benchmark_adc_speed_format_summary_row(const PhoenixBenchmarkAdcSpeedSummaryRowValues& values,
                                                    char* buffer, std::size_t buffer_length);

#endif  // PHOENIX_BENCHMARK_ADC_SPEED_FORMATTER_HPP
