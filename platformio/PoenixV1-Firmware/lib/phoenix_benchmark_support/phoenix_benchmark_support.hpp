#ifndef PHOENIX_BENCHMARK_SUPPORT_HPP
#define PHOENIX_BENCHMARK_SUPPORT_HPP

#include <cmath>
#include <limits>
#include <stdint.h>

namespace phoenix_benchmark_support {

/// @brief Tracks running statistics for streaming samples without storing history.
/// @tparam T Numeric value type captured by the accumulator.
template <typename T>
struct RunningStats {
  uint32_t count     = 0;
  double   mean      = 0.0;
  double   m2        = 0.0;
  T        min_value = std::numeric_limits<T>::max();
  T        max_value = std::numeric_limits<T>::lowest();

  /// @brief Incorporate a new sample into the running statistics.
  /// @param value Sample value to add.
  void update(T value) {
    ++count;
    const double double_value = static_cast<double>(value);
    const double delta        = double_value - mean;
    mean += delta / static_cast<double>(count);
    const double delta2 = double_value - mean;
    m2 += delta * delta2;

    if (value < min_value) {
      min_value = value;
    }
    if (value > max_value) {
      max_value = value;
    }
  }

  /// @brief Determine whether at least one sample has been captured.
  /// @return True when samples have been recorded.
  bool has_samples() const {
    return count > 0u;
  }

  /// @brief Calculate the sample standard deviation for captured values.
  /// @return Standard deviation (0 when fewer than two samples are available).
  double standard_deviation() const {
    if (count < 2u) {
      return 0.0;
    }
    return std::sqrt(m2 / static_cast<double>(count - 1u));
  }

  /// @brief Compute the observed range between min and max samples.
  /// @return Difference between maximum and minimum sample values.
  double range() const {
    if (!has_samples()) {
      return 0.0;
    }
    return static_cast<double>(max_value) - static_cast<double>(min_value);
  }
};

/// @brief Aggregates per-state statistics captured during the benchmark run.
struct StateAccumulator {
  RunningStats<int32_t>  channel_a_codes;
  RunningStats<int32_t>  channel_b_codes;
  RunningStats<uint32_t> state_duration_us;
};

/// @brief Captures a single benchmark sample for CSV emission.
struct SampleResult {
  int32_t  channel_a_code;
  int32_t  channel_b_code;
  uint32_t elapsed_us;
  uint32_t timestamp_us;
};

}  // namespace phoenix_benchmark_support

#endif  // PHOENIX_BENCHMARK_SUPPORT_HPP
