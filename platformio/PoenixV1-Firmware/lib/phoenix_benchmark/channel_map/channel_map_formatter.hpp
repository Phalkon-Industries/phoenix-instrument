#ifndef PHOENIX_BENCHMARK_CHANNEL_MAP_FORMATTER_HPP
#define PHOENIX_BENCHMARK_CHANNEL_MAP_FORMATTER_HPP

#include <cstddef>
#include <cstdint>

/// @brief Defines the per-column widths used by the fixed-width summary table.
/// The values were sized so that common sensor magnitudes (up to six digits and
/// three decimals) render without wrapping while keeping the serial view narrow.
static constexpr std::size_t k_phoenix_benchmark_channel_map_summary_label_width        = 8u;
static constexpr std::size_t k_phoenix_benchmark_channel_map_summary_samples_width      = 9u;
static constexpr std::size_t k_phoenix_benchmark_channel_map_summary_channel_width      = 12u;
static constexpr std::size_t k_phoenix_benchmark_channel_map_summary_map_width          = 12u;
static constexpr std::size_t k_phoenix_benchmark_channel_map_summary_warning_width      = 14u;
static constexpr std::size_t k_phoenix_benchmark_channel_map_summary_column_count       = 12u;
static constexpr std::size_t k_phoenix_benchmark_channel_map_summary_table_buffer_bytes = 256u;

/// @brief Captures the values required to populate one summary table row.
struct PhoenixBenchmarkChannelMapSummaryRowValues {
  const char* label;
  uint32_t    sample_count;
  double      mean_channel_a;
  double      std_channel_a;
  double      min_channel_a;
  double      max_channel_a;
  double      mean_channel_b;
  double      std_channel_b;
  double      min_channel_b;
  double      max_channel_b;
  const char* channel_alignment;
  const char* warning_label;
  bool        has_channel_metrics;
};

/// @brief Format the summary table header into a caller-supplied buffer.
bool phoenix_benchmark_channel_map_format_summary_header(char* buffer, std::size_t buffer_length);

/// @brief Format one row of the summary table.
bool phoenix_benchmark_channel_map_format_summary_row(const PhoenixBenchmarkChannelMapSummaryRowValues& values,
                                                      char* buffer, std::size_t buffer_length);

#endif  // PHOENIX_BENCHMARK_CHANNEL_MAP_FORMATTER_HPP