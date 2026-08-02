#ifndef PHOENIX_BENCHMARK_POT_SWEEP_FORMATTER_HPP
#define PHOENIX_BENCHMARK_POT_SWEEP_FORMATTER_HPP

#include <cstddef>
#include <cstdint>

/**
 * @brief Character capacity required for pot sweep summary formatting helpers.
 */
constexpr std::size_t k_phoenix_benchmark_pot_sweep_summary_buffer_bytes = 160u;

/**
 * @brief Values rendered into a single row of the pot sweep summary table.
 */
struct PhoenixBenchmarkPotSweepSummaryRowValues {
  uint16_t wiper_code;
  int32_t  blue_max_code;
  int32_t  green_max_code;
  bool     blue_saturated;
  bool     green_saturated;
};

/**
 * @brief Format the summary table header for potentiometer sweep results.
 *
 * @param buffer Destination buffer receiving the header string.
 * @param length Number of bytes available in buffer.
 *
 * @return True when the header fits inside the supplied buffer; false otherwise.
 */
bool phoenix_benchmark_pot_sweep_format_summary_header(char* buffer, std::size_t length);

/**
 * @brief Format a single pot sweep summary row representing one wiper measurement.
 *
 * @param values Metrics captured for the current wiper position.
 * @param buffer Destination buffer receiving the formatted row.
 * @param length Number of bytes available in buffer.
 *
 * @return True when the row fits inside the supplied buffer; false otherwise.
 */
bool phoenix_benchmark_pot_sweep_format_summary_row(const PhoenixBenchmarkPotSweepSummaryRowValues& values,
                                                    char* buffer, std::size_t length);

#endif  // PHOENIX_BENCHMARK_POT_SWEEP_FORMATTER_HPP
