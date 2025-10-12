#ifndef PHOENIX_BENCHMARK_CHANNEL_MAP_HPP
#define PHOENIX_BENCHMARK_CHANNEL_MAP_HPP

#include "../core/phoenix_benchmark_core.hpp"
#include "channel_map_formatter.hpp"
#include "channel_map_support.hpp"
#include <cstddef>
#include <cstdint>

constexpr int PHOENIX_BENCHMARK_OK                   = 0;
constexpr int PHOENIX_BENCHMARK_ERR_UNIMPLEMENTED    = -1;
constexpr int PHOENIX_BENCHMARK_ERR_INVALID_ARGUMENT = -2;

struct PhoenixBenchmarkChannelMapDefaults {
  uint32_t sweep_count;
  uint32_t dwell_us;
  uint8_t  wiper_code;
  bool     include_drain_state;
};

struct PhoenixBenchmarkChannelMapOptions {
  uint32_t sweep_count;
  bool     has_sweep_override;
  uint32_t dwell_us;
  bool     has_dwell_override;
  uint8_t  wiper_code;
  bool     has_wiper_override;

  void apply_defaults(const PhoenixBenchmarkChannelMapDefaults& defaults);
  bool validate(char* error_buffer, std::size_t buffer_length) const;
};

struct PhoenixBenchmarkChannelMapExecutionStatus {
  bool        success;
  int         return_code;
  const char* message;
};

struct PhoenixBenchmarkChannelMapParseResult {
  bool                              success;
  PhoenixBenchmarkChannelMapOptions options;
  const char*                       error_message;
};

struct PhoenixBenchmarkChannelMapStateDescriptor {
  const char*             label;
  PhoenixBenchmarkChannel expected_channel;
  bool                    include_in_summary;
  bool                    is_reference_state;
};

struct PhoenixBenchmarkChannelMapOutputCallbacks {
  void (*print_line)(const char* line);
  void (*print_ready)(void);
};

constexpr std::size_t k_phoenix_benchmark_channel_map_state_descriptor_count = 3u;
constexpr std::size_t k_phoenix_benchmark_channel_map_drain_state_index      = 0u;

const PhoenixBenchmarkChannelMapStateDescriptor* phoenix_benchmark_channel_map_state_descriptors(void);

void phoenix_benchmark_channel_map_initialise(const PhoenixBenchmarkChannelMapDefaults& defaults);

PhoenixBenchmarkChannelMapExecutionStatus phoenix_benchmark_channel_map_run(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks);

PhoenixBenchmarkChannelMapParseResult phoenix_benchmark_channel_map_parse_command(const char* line);

void phoenix_benchmark_channel_map_reset_state(void);

#endif  // PHOENIX_BENCHMARK_CHANNEL_MAP_HPP
