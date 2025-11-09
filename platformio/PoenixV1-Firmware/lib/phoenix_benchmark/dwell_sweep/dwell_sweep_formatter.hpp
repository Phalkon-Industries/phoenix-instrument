#ifndef PHOENIX_BENCHMARK_DWELL_SWEEP_FORMATTER_HPP
#define PHOENIX_BENCHMARK_DWELL_SWEEP_FORMATTER_HPP

#include <cstddef>
#include <cstdint>

/**
 * @brief Byte capacity required when formatting dwell sweep summary rows.
 */
constexpr std::size_t k_phoenix_benchmark_dwell_sweep_summary_buffer_bytes = 512u;

/**
 * @brief Aggregated metrics rendered into a single dwell sweep summary row.
 */
struct PhoenixBenchmarkDwellSweepSummaryRowValues {
  uint32_t dwell_us;          /**< Dwell duration in microseconds for the current row. */
  uint32_t sweeps_completed;  /**< Number of sweeps successfully executed at this dwell. */
  double   drain_blue_mean;   /**< Mean ADC code for the blue drain channel. */
  double   drain_blue_std;    /**< Standard deviation of the blue drain channel. */
  double   drain_blue_slope;  /**< Drift slope computed for the blue drain channel. */
  double   drain_green_mean;  /**< Mean ADC code for the green drain channel. */
  double   drain_green_std;   /**< Standard deviation of the green drain channel. */
  double   drain_green_slope; /**< Drift slope computed for the green drain channel. */
  double   blue_mean;         /**< Mean ADC code captured from the blue LED. */
  double   blue_std;          /**< Standard deviation of blue LED samples. */
  double   blue_slope;        /**< Drift slope computed for the blue LED channel. */
  double   green_mean;        /**< Mean ADC code captured from the green LED. */
  double   green_std;         /**< Standard deviation of green LED samples. */
  double   green_slope;       /**< Drift slope computed for the green LED channel. */
  uint32_t duration_us;       /**< Total sweep duration in microseconds for the dwell. */
  uint8_t  warning_mask;      /**< Warning bitmask populated during execution. */
  bool     has_metrics;       /**< Indicates whether mean and stddev fields are valid. */
};

/**
 * @brief Format the dwell sweep summary header into the supplied buffer.
 *
 * @param buffer Destination buffer that receives the header text.
 * @param length Writable byte capacity available in @p buffer.
 *
 * @return True when the header fits in @p buffer; false on failure.
 */
bool phoenix_benchmark_dwell_sweep_format_summary_header(char* buffer, std::size_t length);

/**
 * @brief Format a single dwell sweep summary row using the provided metrics.
 *
 * @param values Aggregated dwell sweep metrics to render.
 * @param buffer Destination buffer that receives the formatted row text.
 * @param length Writable byte capacity available in @p buffer.
 *
 * @return True when the row fits in @p buffer; false on failure.
 */
bool phoenix_benchmark_dwell_sweep_format_summary_row(const PhoenixBenchmarkDwellSweepSummaryRowValues& values,
                                                      char* buffer, std::size_t length);

#endif  // PHOENIX_BENCHMARK_DWELL_SWEEP_FORMATTER_HPP
