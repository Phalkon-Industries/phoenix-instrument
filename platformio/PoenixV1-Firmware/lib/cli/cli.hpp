#ifndef CLI_HPP
#define CLI_HPP

#include "light_calibration.hpp"
#include "light_readings.hpp"
#include "phoenix_settings.hpp"
#include "thermistor_reader.hpp"
#include <stdbool.h>
#include <stddef.h>

class Print;

/**
 * @brief Dispatch outcomes for CLI command parsing.
 */
typedef enum CliDispatchResult {
  CLI_DISPATCH_OK              = 0,
  CLI_DISPATCH_EMPTY_COMMAND   = -1,
  CLI_DISPATCH_UNKNOWN_COMMAND = -2,
} CliDispatchResult;

/**
 * @brief Measurement hooks that allow tests to stub sweep and statistic helpers.
 *
 * These indirections let Unity tests run without driving real LEDs or ADC sweeps by
 * swapping in lightweight fakes. Production uses the default light_readings helpers.
 */
struct CliMeasurementHooks {
  int (*sweep_n)(uint32_t sweep_count, LightReadingsSweepCollection* results_out);
  int (*compute_stats)(const LightReadingsSweepCollection* sweep_collection, LightReadingsSweepStats* stats_out);
  int (*measure_temperature)(ThermistorId id, float* temperature_c_out);
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
 *
 * @param command_token Null-terminated command string to interpret.
 * @return CLI_DISPATCH_OK when handled, otherwise the matching error.
 */
CliDispatchResult cli_dispatch_command(const char* command_token);

/**
 * @brief Report whether a baseline command has been received in this session (test-only helper).
 *
 * @return True when a baseline capture is cached for reuse.
 */
bool cli_test_is_baseline_cached(void);

/**
 * @brief Retrieve the cached baseline statistics (test-only helper).
 *
 * @param stats_out Output buffer for the cached stats; ignored when NULL.
 */
void cli_test_get_baseline_stats(LightReadingsSweepStats* stats_out);

/**
 * @brief Override measurement hooks for testing to avoid exercising real hardware. Passing NULL restores defaults.
 *
 * @param hooks Replacement hook table; pass NULL to restore defaults.
 */
void cli_test_set_measurement_hooks(const CliMeasurementHooks* hooks);

/**
 * @brief Override the CLI output stream for tests. Passing NULL restores Serial.
 *
 * @param output Alternate Print implementation; pass NULL to restore Serial.
 */
void cli_test_set_output(Print* output);

#endif  // CLI_HPP
