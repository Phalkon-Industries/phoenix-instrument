#ifndef PHOENIX_BENCHMARK_CHANNEL_MAP_HPP
#define PHOENIX_BENCHMARK_CHANNEL_MAP_HPP

#include "phoenix_benchmark_support.hpp"
#include <stddef.h>
#include <stdint.h>

namespace phoenix_benchmark {

constexpr int PHOENIX_BENCHMARK_OK                   = 0;
constexpr int PHOENIX_BENCHMARK_ERR_UNIMPLEMENTED    = -1;
constexpr int PHOENIX_BENCHMARK_ERR_INVALID_ARGUMENT = -2;

/**
 * @brief Defines the default channel map benchmark configuration applied when no overrides are provided.
 *
 * Each field mirrors the configuration exposed to the CLI so that host tools and firmware share a common baseline.
 */
struct ChannelMapDefaults {
  uint32_t sweep_count;
  uint32_t dwell_us;
  uint8_t  wiper_code;
  bool     include_drain_state;
};

/**
 * @brief Captures per-run overrides for the channel map benchmark.
 *
 * All overrides are optional; the helper functions supply defaults and validation so callers can focus on hardware
 * orchestration.
 */
struct ChannelMapOptions {
  uint32_t sweep_count;
  bool     has_sweep_override;
  uint32_t dwell_us;
  bool     has_dwell_override;
  uint8_t  wiper_code;
  bool     has_wiper_override;

  /**
   * @brief Applies any unset fields from the provided defaults.
   *
   * @param defaults Reference defaults established during initialisation.
   */
  void apply_defaults(const ChannelMapDefaults& defaults);

  /**
   * @brief Validates option bounds and reports descriptive error text.
   *
   * @param error_buffer Optional output buffer for a human-readable error description.
   * @param buffer_length Length of @p error_buffer in bytes.
   * @return true when the configuration is ready for execution, false otherwise.
   */
  bool validate(char* error_buffer, size_t buffer_length) const;
};

/**
 * @brief Summarises the outcome of a benchmark run.
 */
struct ExecutionStatus {
  bool        success;
  int         return_code;
  const char* message;
};

/**
 * @brief Represents the result of parsing a CLI command into benchmark options.
 */
struct ParseResult {
  bool              success;
  ChannelMapOptions options;
  const char*       error_message;
};

struct ChannelMapStateDescriptor {
  const char*                                 label;
  phoenix_benchmark_support::BenchmarkChannel expected_channel;
  bool                                        include_in_summary;
  bool                                        is_reference_state;
};

/**
 * @brief Callbacks that allow the library to emit textual updates without owning the transport.
 */
struct OutputCallbacks {
  void (*print_line)(const char* line);
  void (*print_ready)(void);
};

namespace channel_map {

constexpr size_t k_state_descriptor_count = 3u;
constexpr size_t k_drain_state_index      = 0u;

const ChannelMapStateDescriptor* state_descriptors(void);

/**
 * @brief Stores the defaults used when a command omits overrides.
 *
 * @param defaults Baseline configuration sourced from board-level settings.
 */
void initialise(const ChannelMapDefaults& defaults);

/**
 * @brief Executes the channel map benchmark using the supplied options and accumulators.
 *
 * @param options Validated run configuration.
 * @param accumulators Array of accumulators sized to match the benchmark state sequence.
 * @param callbacks Optional output handlers for progress and completion messages.
 * @return ExecutionStatus describing success, error code, and message.
 */
ExecutionStatus run(const ChannelMapOptions& options, phoenix_benchmark_support::StateAccumulator* accumulators,
                    const OutputCallbacks& callbacks);

/**
 * @brief Parses a CLI line (JSON or key-value) into runnable options.
 *
 * @param line Null-terminated command string provided by the host or Serial console.
 * @return ParseResult containing populated options or descriptive failure information.
 */
ParseResult parse_command(const char* line);

/**
 * @brief Resets any static state maintained by the library so subsequent tests start clean.
 */
void reset_state(void);

}  // namespace channel_map

}  // namespace phoenix_benchmark

#endif  // PHOENIX_BENCHMARK_CHANNEL_MAP_HPP
