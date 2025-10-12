#ifndef PHOENIX_BENCHMARK_CORE_HPP
#define PHOENIX_BENCHMARK_CORE_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

/// @brief Identifies which ADC channel exhibited the dominant response for a LED state.
enum class PhoenixBenchmarkChannel : uint8_t {
  kUnknown  = 0u,
  kChannelA = 1u,
  kChannelB = 2u,
};

/// @brief Tracks running statistics for streaming samples without storing history.
/// @tparam T Numeric value type captured by the accumulator.
template <typename T>
struct PhoenixBenchmarkRunningStats {
  uint32_t count     = 0u;
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
struct PhoenixBenchmarkStateAccumulator {
  PhoenixBenchmarkRunningStats<int32_t> channel_a_codes;
  PhoenixBenchmarkRunningStats<int32_t> channel_b_codes;
  uint32_t                              channel_a_saturation_count = 0u;
  uint32_t                              channel_b_saturation_count = 0u;
};

/// @brief Captures a single benchmark sample for CSV emission.
struct PhoenixBenchmarkSampleResult {
  int32_t channel_a_code;
  int32_t channel_b_code;
};

constexpr int32_t k_phoenix_benchmark_adc_positive_full_scale_code = 8388607;
constexpr int32_t k_phoenix_benchmark_adc_negative_full_scale_code = -8388608;

/// @brief Determine whether the provided ADC code sits at the hardware saturation limits.
/// @param code ADC sample value to evaluate.
/// @return True when the code is within the saturation bounds.
inline bool phoenix_benchmark_is_adc_code_saturated(int32_t code) {
  return (code >= k_phoenix_benchmark_adc_positive_full_scale_code) ||
         (code <= k_phoenix_benchmark_adc_negative_full_scale_code);
}

#endif  // PHOENIX_BENCHMARK_CORE_HPP
