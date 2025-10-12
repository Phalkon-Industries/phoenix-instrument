#ifndef PHOENIX_BENCHMARK_CHANNEL_MAP_SUPPORT_HPP
#define PHOENIX_BENCHMARK_CHANNEL_MAP_SUPPORT_HPP

#include "../core/phoenix_benchmark_core.hpp"
#include <cstddef>
#include <cstdint>

struct PhoenixBenchmarkChannelMapRequest {
  uint32_t sweep_count        = 0u;
  uint32_t dwell_us           = 0u;
  bool     has_dwell_override = false;
  uint8_t  wiper_code         = 0u;
  bool     has_wiper_override = false;
};

/// @brief Analyse state statistics to identify the dominant channel.
PhoenixBenchmarkChannel phoenix_benchmark_channel_map_determine_dominant_channel(
    const PhoenixBenchmarkStateAccumulator& drain_accumulator,
    const PhoenixBenchmarkStateAccumulator& state_accumulator, double minimum_difference);

/// @brief Render a short alignment label comparing expected vs observed channels.
bool phoenix_benchmark_channel_map_format_alignment_label(PhoenixBenchmarkChannel expected,
                                                          PhoenixBenchmarkChannel observed, char* buffer,
                                                          std::size_t buffer_length);

/// @brief Extract channel-map parameters from a JSON benchmark command.
bool phoenix_benchmark_channel_map_parse_command(const char* json_line, PhoenixBenchmarkChannelMapRequest* out_request);

#endif  // PHOENIX_BENCHMARK_CHANNEL_MAP_SUPPORT_HPP
