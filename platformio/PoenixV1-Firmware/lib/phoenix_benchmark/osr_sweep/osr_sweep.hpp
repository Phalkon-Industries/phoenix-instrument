#ifndef PHOENIX_BENCHMARK_OSR_SWEEP_HPP
#define PHOENIX_BENCHMARK_OSR_SWEEP_HPP

#include "../../mcp356x/mcp356x.hpp"
#include "../core/phoenix_benchmark_core.hpp"
#include <cstddef>
#include <cstdint>

/**
 * \brief Captures the default OSR sweep configuration loaded at boot.
 *
 * Callers use these defaults when the host command omits explicit overrides.
 */
struct PhoenixBenchmarkOsrSweepDefaults {
  uint32_t sweep_count;
  uint32_t dwell_us;
  uint8_t  wiper_code;
};

/**
 * \brief Holds the effective OSR sweep options applied to a single run.
 *
 * The flag fields indicate whether each option was overridden explicitly by
 * the host. When a flag is false, \ref apply_defaults fills in the value from
 * \ref PhoenixBenchmarkOsrSweepDefaults before validation.
 */
struct PhoenixBenchmarkOsrSweepOptions {
  uint32_t sweep_count;
  bool     has_sweep_override;
  uint32_t dwell_us;
  bool     has_dwell_override;
  uint8_t  wiper_code;
  bool     has_wiper_override;

  /**
   * \brief Applies default values to any option that lacks an override.
   *
   * \param defaults Defaults captured during module initialisation.
   */
  void apply_defaults(const PhoenixBenchmarkOsrSweepDefaults& defaults);

  /**
   * \brief Validates the option ranges and reports the first failure.
   *
   * \param error_message Optional pointer that receives the validation error
   *        string when validation fails.
   * \return True when all options are valid, false otherwise.
   */
  bool validate(const char** error_message) const;
};

/**
 * \brief Surfaces the result of parsing an OSR sweep host command.
 */
struct PhoenixBenchmarkOsrSweepParseResult {
  bool                            success;
  PhoenixBenchmarkOsrSweepOptions options;
  const char*                     error_message;
};

/**
 * \brief Installs the module defaults used when commands omit overrides.
 *
 * \param defaults Host-provided defaults sourced from persistent storage.
 */
void phoenix_benchmark_osr_sweep_initialise(const PhoenixBenchmarkOsrSweepDefaults& defaults);

/**
 * \brief Resets the module state to compile-time defaults and clears test hooks.
 */
void phoenix_benchmark_osr_sweep_reset_state(void);

/**
 * \brief Parses a JSON OSR sweep command, applying defaults and validation.
 *
 * \param line Null-terminated JSON payload received from the host.
 * \return Parse outcome, including validated options or an error message.
 */
PhoenixBenchmarkOsrSweepParseResult phoenix_benchmark_osr_sweep_parse_command(const char* line);

/** \brief Number of OSR values exercised in a full sweep. */
constexpr std::size_t k_phoenix_benchmark_osr_value_count = 16u;

/**
 * \brief Records the captured metrics for a single OSR configuration.
 */
struct PhoenixBenchmarkOsrSweepRowMetrics {
  mcp356x_osr                      osr_value;
  PhoenixBenchmarkStateAccumulator drain;
  PhoenixBenchmarkStateAccumulator blue;
  PhoenixBenchmarkStateAccumulator green;
  uint32_t                         sweep_count;
  uint32_t                         elapsed_microseconds;
};

/**
 * \brief Reports the execution status of an OSR sweep run.
 */
struct PhoenixBenchmarkOsrSweepExecutionStatus {
  bool        success;
  bool        has_warnings;
  const char* message;
  uint32_t    rows_generated;
};

/**
 * \brief Executes an OSR sweep using the supplied options and captures metrics.
 *
 * \param options Validated sweep configuration for the run.
 * \param rows Caller-provided buffer that receives per-OSR metrics.
 * \param row_capacity Number of row entries available in \p rows.
 * \return Execution status, including the number of populated rows.
 */
PhoenixBenchmarkOsrSweepExecutionStatus phoenix_benchmark_osr_sweep_run(const PhoenixBenchmarkOsrSweepOptions& options,
                                                                        PhoenixBenchmarkOsrSweepRowMetrics*    rows,
                                                                        std::size_t row_capacity);

#if defined(UNIT_TEST)
/**
 * \brief Overrides the MCP356x OSR setter used during unit tests.
 *
 * Passing nullptr restores the production implementation.
 */
void phoenix_benchmark_osr_sweep_set_osr_setter_for_test(int (*setter)(mcp356x_osr value));

/**
 * \brief Overrides the microsecond timer provider used during unit tests.
 *
 * Passing nullptr restores the production implementation.
 */
void phoenix_benchmark_osr_sweep_set_micros_provider_for_test(uint32_t (*provider)(void));

/**
 * rief Overrides the hardware-ready checker used during unit tests.
 *
 * Passing nullptr restores the production implementation.
 */
void phoenix_benchmark_osr_sweep_set_hardware_ready_checker_for_test(bool (*checker)(void));

/**
 * \brief Restores all unit-test hook overrides to their production defaults.
 */
void phoenix_benchmark_osr_sweep_clear_test_hooks(void);
#endif  // defined(UNIT_TEST)

#endif  // PHOENIX_BENCHMARK_OSR_SWEEP_HPP
