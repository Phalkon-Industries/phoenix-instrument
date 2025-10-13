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
constexpr int PHOENIX_BENCHMARK_ERR_HARDWARE_FAILURE = -3;
constexpr int PHOENIX_BENCHMARK_ERR_SAMPLING_FAILURE = -4;

/**
 * @brief Default configuration applied to channel map runs when callers omit overrides.
 *
 * @details Each field seeds the matching option in
 * PhoenixBenchmarkChannelMapOptions. Callers typically populate this via
 * phoenix_benchmark_channel_map_initialise().
 */
struct PhoenixBenchmarkChannelMapDefaults {
  uint32_t sweep_count;
  uint32_t dwell_us;
  uint8_t  wiper_code;
};

/**
 * @brief Options controlling a channel map sweep.
 *
 * @details Flags indicate whether a field was explicitly overridden on the
 * command line. When a flag is false, apply_defaults() will back-fill from the
 * current defaults.
 */
struct PhoenixBenchmarkChannelMapOptions {
  uint32_t sweep_count;
  bool     has_sweep_override;
  uint32_t dwell_us;
  bool     has_dwell_override;
  uint8_t  wiper_code;
  bool     has_wiper_override;

  /**
   * @brief Replace unset option fields with values from the provided defaults.
   *
   * @param defaults Defaults established via
   * phoenix_benchmark_channel_map_initialise().
   */
  void apply_defaults(const PhoenixBenchmarkChannelMapDefaults& defaults);
  /**
   * @brief Validate option ranges before executing a sweep.
   *
   * @param error_buffer Optional buffer populated with a human-readable
   * message on failure.
   * @param buffer_length Length of error_buffer in bytes.
   *
   * @return True when the configuration is acceptable; false otherwise.
   */
  bool validate(char* error_buffer, std::size_t buffer_length) const;
};

/**
 * @brief Result metadata describing a completed channel map run.
 */
struct PhoenixBenchmarkChannelMapExecutionStatus {
  bool        success;
  int         return_code;
  const char* message;
  bool        has_warnings;
};

/**
 * @brief Parse result returned by phoenix_benchmark_channel_map_parse_command().
 */
struct PhoenixBenchmarkChannelMapParseResult {
  bool                              success;
  PhoenixBenchmarkChannelMapOptions options;
  const char*                       error_message;
};

/**
 * @brief Static descriptor for each state visited during a channel map sweep.
 */
struct PhoenixBenchmarkChannelMapStateDescriptor {
  const char*             label;
  PhoenixBenchmarkChannel expected_channel;
  bool                    include_in_summary;
  bool                    is_reference_state;
};

/**
 * @brief Callbacks used to report progress and readiness messages.
 */
struct PhoenixBenchmarkChannelMapOutputCallbacks {
  void (*print_line)(const char* line);
  void (*print_ready)(void);
};

constexpr std::size_t k_phoenix_benchmark_channel_map_state_descriptor_count = 3u;
constexpr std::size_t k_phoenix_benchmark_channel_map_drain_state_index      = 0u;

/**
 * @brief Retrieve the static table describing channel map states.
 *
 * @return Pointer to k_phoenix_benchmark_channel_map_state_descriptor_count
 * descriptors ordered by execution sequence.
 */
const PhoenixBenchmarkChannelMapStateDescriptor* phoenix_benchmark_channel_map_state_descriptors(void);

/**
 * @brief Establish default sweep parameters for subsequent runs.
 *
 * @param defaults Baseline configuration copied into new option sets.
 */
void phoenix_benchmark_channel_map_initialise(const PhoenixBenchmarkChannelMapDefaults& defaults);

/**
 * @brief Execute a channel map sweep using the provided options and accumulators.
 *
 * @param options Parsed options describing sweep count, dwell time, and wiper code.
 * @param accumulators Array of PhoenixBenchmarkStateAccumulator sized to
 * k_phoenix_benchmark_channel_map_state_descriptor_count.
 * @param callbacks Optional printing hooks for progress and errors.
 *
 * @return Execution status summarizing success, error details, and warning flag.
 */
PhoenixBenchmarkChannelMapExecutionStatus phoenix_benchmark_channel_map_run(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks);

/**
 * @brief Parse a command-line request into channel map options.
 *
 * @param line Raw command payload (e.g. received over serial).
 *
 * @return Parse outcome containing populated options or an error message.
 */
PhoenixBenchmarkChannelMapParseResult phoenix_benchmark_channel_map_parse_command(const char* line);

/**
 * @brief Clear cached state so future runs start from a known baseline.
 */
void phoenix_benchmark_channel_map_reset_state(void);

/**
 * @brief Force synthetic ADC saturation during tests.
 *
 * @param enabled True to clamp future samples to full-scale codes; false to
 * restore normal behavior.
 */
void phoenix_benchmark_channel_map_set_force_saturation_for_test(bool enabled);

#endif  // PHOENIX_BENCHMARK_CHANNEL_MAP_HPP
