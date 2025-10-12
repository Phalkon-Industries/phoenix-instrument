#ifndef PHOENIX_BENCHMARK_COMMAND_PARSER_HPP
#define PHOENIX_BENCHMARK_COMMAND_PARSER_HPP

#include <stdint.h>

namespace phoenix_benchmark {
namespace parser {

static constexpr char k_error_invalid_command[]  = "invalid command";
static constexpr char k_error_unknown_argument[] = "unknown argument";
static constexpr char k_error_invalid_value[]    = "invalid value";
static constexpr char k_error_missing_argument[] = "missing argument";

struct CommandArguments {
  uint32_t sweep_count        = 0u;
  bool     has_sweep_override = false;
  uint32_t dwell_us           = 0u;
  bool     has_dwell_override = false;
  uint8_t  wiper_code         = 0u;
  bool     has_wiper_override = false;
};

struct ParseOutcome {
  bool             success;
  CommandArguments arguments;
  const char*      error_message;
};

ParseOutcome parse_command(const char* line, const char* expected_command);

}  // namespace parser
}  // namespace phoenix_benchmark

#endif  // PHOENIX_BENCHMARK_COMMAND_PARSER_HPP