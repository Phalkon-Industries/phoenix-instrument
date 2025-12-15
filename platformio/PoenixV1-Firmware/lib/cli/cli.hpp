#ifndef CLI_HPP
#define CLI_HPP

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

#endif  // CLI_HPP
