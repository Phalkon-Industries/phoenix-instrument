#ifndef PHOENIX_BENCHMARK_SUPPORT_HPP
#define PHOENIX_BENCHMARK_SUPPORT_HPP

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdint.h>
namespace phoenix_benchmark_support {

/// @brief Identifies which ADC channel exhibited the dominant response for a LED state.
enum class BenchmarkChannel : uint8_t {
  kUnknown  = 0u,
  kChannelA = 1u,
  kChannelB = 2u,
};

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
  RunningStats<int32_t> channel_a_codes;
  RunningStats<int32_t> channel_b_codes;
};

/// @brief Captures a single benchmark sample for CSV emission.
struct SampleResult {
  int32_t channel_a_code;
  int32_t channel_b_code;
};

struct ChannelMapRequest {
  uint32_t sweep_count        = 0u;
  uint32_t dwell_us           = 0u;
  bool     has_dwell_override = false;
  uint8_t  wiper_code         = 0u;
  bool     has_wiper_override = false;
};

BenchmarkChannel determine_dominant_channel(const StateAccumulator& drain_accumulator,
                                            const StateAccumulator& state_accumulator, double minimum_difference);

bool format_channel_alignment_label(BenchmarkChannel expected, BenchmarkChannel observed, char* buffer,
                                    std::size_t buffer_length);

bool parse_channel_map_command(const char* json_line, ChannelMapRequest* out_request);

constexpr int32_t k_adc_positive_full_scale_code = 8388607;
constexpr int32_t k_adc_negative_full_scale_code = -8388608;

bool        is_adc_code_saturated(int32_t code);
inline bool is_adc_code_saturated(int32_t code) {
  return (code >= k_adc_positive_full_scale_code) || (code <= k_adc_negative_full_scale_code);
}

inline BenchmarkChannel determine_dominant_channel(const StateAccumulator& drain_accumulator,
                                                   const StateAccumulator& state_accumulator,
                                                   double                  minimum_difference) {
  if (!drain_accumulator.channel_a_codes.has_samples() || !drain_accumulator.channel_b_codes.has_samples() ||
      !state_accumulator.channel_a_codes.has_samples() || !state_accumulator.channel_b_codes.has_samples()) {
    return BenchmarkChannel::kUnknown;
  }

  const double drain_mean_a = drain_accumulator.channel_a_codes.mean;
  const double drain_mean_b = drain_accumulator.channel_b_codes.mean;
  const double state_mean_a = state_accumulator.channel_a_codes.mean;
  const double state_mean_b = state_accumulator.channel_b_codes.mean;

  const double delta_a = std::fabs(state_mean_a - drain_mean_a);
  const double delta_b = std::fabs(state_mean_b - drain_mean_b);

  if (delta_a < minimum_difference && delta_b < minimum_difference) {
    return BenchmarkChannel::kUnknown;
  }

  if (delta_a > delta_b && delta_a >= minimum_difference) {
    return BenchmarkChannel::kChannelA;
  }

  if (delta_b > delta_a && delta_b >= minimum_difference) {
    return BenchmarkChannel::kChannelB;
  }

  return BenchmarkChannel::kUnknown;
}

inline bool format_channel_alignment_label(BenchmarkChannel expected, BenchmarkChannel observed, char* buffer,
                                           std::size_t buffer_length) {
  if (buffer == nullptr || buffer_length == 0u) {
    return false;
  }

  auto channel_token = [](BenchmarkChannel channel) -> const char* {
    switch (channel) {
      case BenchmarkChannel::kChannelA:
        return "A";
      case BenchmarkChannel::kChannelB:
        return "B";
      case BenchmarkChannel::kUnknown:
      default:
        return "??";
    }
  };

  if (expected == BenchmarkChannel::kUnknown) {
    if (buffer_length < 3u) {
      return false;
    }
    buffer[0u] = '-';
    buffer[1u] = '-';
    buffer[2u] = '\0';
    return true;
  }

  const char* expected_token = channel_token(expected);
  const char* observed_token = channel_token(observed);

  int written = 0;
  if (observed == expected) {
    written = snprintf(buffer, buffer_length, "%s=OK", expected_token);
  }
  else if (observed == BenchmarkChannel::kUnknown) {
    written = snprintf(buffer, buffer_length,
                       "?"
                       "?!=%s",
                       expected_token);
  }
  else {
    written = snprintf(buffer, buffer_length, "%s!=%s", observed_token, expected_token);
  }

  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= buffer_length) {
    return false;
  }
  return true;
}

inline bool parse_channel_map_command(const char* json_line, ChannelMapRequest* out_request) {
  if (json_line == nullptr || out_request == nullptr) {
    return false;
  }

  const char* command_token = std::strstr(json_line, "\"command\"");
  if (command_token == nullptr) {
    return false;
  }

  const char* command_value = std::strchr(command_token, ':');
  if (command_value == nullptr) {
    return false;
  }
  ++command_value;

  const char* channel_map_token = std::strstr(command_value, "\"channel_map\"");
  if (channel_map_token == nullptr) {
    return false;
  }

  const char* sweeps_token = std::strstr(json_line, "\"sweeps\"");
  if (sweeps_token == nullptr) {
    return false;
  }

  const char* sweeps_value = std::strchr(sweeps_token, ':');
  if (sweeps_value == nullptr) {
    return false;
  }
  ++sweeps_value;

  char*      sweeps_end  = nullptr;
  const long sweeps_long = std::strtol(sweeps_value, &sweeps_end, 10);
  if (sweeps_end == sweeps_value || sweeps_long <= 0L) {
    return false;
  }

  ChannelMapRequest request = {};
  request.sweep_count       = static_cast<uint32_t>(sweeps_long);

  const char* dwell_token = std::strstr(json_line, "\"dwell_us\"");
  if (dwell_token != nullptr) {
    const char* dwell_value = std::strchr(dwell_token, ':');
    if (dwell_value == nullptr) {
      return false;
    }
    ++dwell_value;

    char*      dwell_end  = nullptr;
    const long dwell_long = std::strtol(dwell_value, &dwell_end, 10);
    if (dwell_end == dwell_value || dwell_long < 0L) {
      return false;
    }

    request.dwell_us           = static_cast<uint32_t>(dwell_long);
    request.has_dwell_override = true;
  }

  const char* wiper_token = std::strstr(json_line, "\"wiper_code\"");
  if (wiper_token != nullptr) {
    const char* wiper_value = std::strchr(wiper_token, ':');
    if (wiper_value == nullptr) {
      return false;
    }
    ++wiper_value;

    char*      wiper_end  = nullptr;
    const long wiper_long = std::strtol(wiper_value, &wiper_end, 0);
    if (wiper_end == wiper_value || wiper_long < 0L || wiper_long > 0xFFL) {
      return false;
    }

    request.wiper_code         = static_cast<uint8_t>(wiper_long & 0xFFL);
    request.has_wiper_override = true;
  }

  *out_request = request;
  return true;
}

}  // namespace phoenix_benchmark_support

#endif  // PHOENIX_BENCHMARK_SUPPORT_HPP
