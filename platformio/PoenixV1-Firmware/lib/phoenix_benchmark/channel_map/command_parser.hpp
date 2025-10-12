#ifndef PHOENIX_BENCHMARK_CHANNEL_MAP_COMMAND_PARSER_HPP
#define PHOENIX_BENCHMARK_CHANNEL_MAP_COMMAND_PARSER_HPP

#include <cstdint>

static constexpr char k_phoenix_benchmark_channel_map_error_invalid_command[]  = "invalid command";
static constexpr char k_phoenix_benchmark_channel_map_error_unknown_argument[] = "unknown argument";
static constexpr char k_phoenix_benchmark_channel_map_error_invalid_value[]    = "invalid value";
static constexpr char k_phoenix_benchmark_channel_map_error_missing_argument[] = "missing argument";

struct PhoenixBenchmarkChannelMapCommandArguments {
  uint32_t sweep_count        = 0u;
  bool     has_sweep_override = false;
  uint32_t dwell_us           = 0u;
  bool     has_dwell_override = false;
  uint8_t  wiper_code         = 0u;
  bool     has_wiper_override = false;
};

struct PhoenixBenchmarkChannelMapParseOutcome {
  bool                                       success;
  PhoenixBenchmarkChannelMapCommandArguments arguments;
  const char*                                error_message;
};

PhoenixBenchmarkChannelMapParseOutcome phoenix_benchmark_channel_map_parse_command_line(const char* line,
                                                                                        const char* expected_command);

#endif  // PHOENIX_BENCHMARK_CHANNEL_MAP_COMMAND_PARSER_HPP
