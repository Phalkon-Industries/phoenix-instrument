#ifndef CLI_HPP
#define CLI_HPP

#include "light_readings.hpp"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Dispatch outcomes for CLI command parsing.
 */
enum class CliDispatchResult : int {
  ok              = 0,
  empty_command   = -1,
  unknown_command = -2,
};

/**
 * @brief Measurement hooks that allow tests to stub sweep and statistic helpers.
 *
 * These indirections let Unity tests run without driving real LEDs or ADC sweeps by
 * swapping in lightweight fakes. Production uses the default light_readings helpers.
 */
struct CliMeasurementHooks {
  int (*sweep_n)(uint32_t sweep_count, LightReadingsSweepCollection* results_out);
  int (*compute_stats)(const LightReadingsSweepCollection* sweep_collection, LightReadingsSweepStats* stats_out);
};

/**
 * @brief Initialise CLI state.
 *
 * Serial must already be configured by the caller before invoking this helper.
 */
void cli_initialize(void);

/**
 * @brief Poll the serial interface for newline-terminated commands and dispatch them.
 */
void cli_poll(void);

/**
 * @brief Dispatch a single tokenised command.
 *
 * Useful for tests and for callers that pre-tokenise input.
 */
CliDispatchResult cli_dispatch_command(const char* command_token);

/**
 * @brief Report whether a baseline command has been received in this session (test-only helper).
 */
bool cli_test_is_baseline_cached(void);

/**
 * @brief Retrieve the cached baseline statistics (test-only helper).
 */
void cli_test_get_baseline_stats(LightReadingsSweepStats* stats_out);

/**
 * @brief Override measurement hooks for testing to avoid exercising real hardware. Passing NULL restores defaults.
 */
void cli_test_set_measurement_hooks(const CliMeasurementHooks* hooks);

#endif  // CLI_HPP
