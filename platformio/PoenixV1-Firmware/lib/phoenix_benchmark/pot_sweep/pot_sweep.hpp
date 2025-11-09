#ifndef PHOENIX_BENCHMARK_POT_SWEEP_HPP
#define PHOENIX_BENCHMARK_POT_SWEEP_HPP

#include <cstddef>
#include <cstdint>

/**
 * @brief Maximum number of discrete wiper positions supported per sweep.
 */
constexpr std::size_t k_phoenix_benchmark_pot_sweep_max_wiper_count = 256u;

/**
 * @brief Baseline configuration applied to potentiometer sweeps when callers omit overrides.
 */
struct PhoenixBenchmarkPotSweepDefaults {
  /** Number of channel-map mini-sweeps executed for each wiper position. */
  uint32_t sweeps_per_wiper;
  /** Microsecond dwell applied before each channel-map measurement. */
  uint32_t dwell_us;
};

/**
 * @brief Call-site options controlling a potentiometer sweep execution.
 */
struct PhoenixBenchmarkPotSweepOptions {
  uint32_t sweeps_per_wiper;
  bool     has_sweeps_override;
  uint32_t dwell_us;
  bool     has_dwell_override;

  /**
   * @brief Populate unset fields with the supplied defaults.
   *
   * @param defaults Baseline sweep configuration captured during initialise.
   */
  void apply_defaults(const PhoenixBenchmarkPotSweepDefaults& defaults);
  /**
   * @brief Validate option ranges before executing the sweep.
   *
   * @param error_message Optional pointer receiving a descriptive failure string.
   *
   * @return True when the configuration is valid; false otherwise.
   */
  bool validate(const char** error_message) const;
};

/**
 * @brief Result payload returned when parsing pot sweep commands from the host.
 */
struct PhoenixBenchmarkPotSweepParseResult {
  bool                            success;
  PhoenixBenchmarkPotSweepOptions options;
  const char*                     error_message;
};

/**
 * @brief Per-wiper metrics collected during a sweep execution.
 */
struct PhoenixBenchmarkPotSweepRowMetrics {
  uint8_t wiper_code;
  int32_t blue_max_code;
  int32_t green_max_code;
  bool    blue_saturated;
  bool    green_saturated;
};

/**
 * @brief Status describing the outcome of a potentiometer sweep run.
 */
struct PhoenixBenchmarkPotSweepExecutionStatus {
  bool        success;
  bool        has_warnings;
  const char* message;
  uint32_t    rows_generated;
  bool        blue_recommendation_valid;
  uint8_t     blue_recommended_wiper;
  bool        green_recommendation_valid;
  uint8_t     green_recommended_wiper;
};

/**
 * @brief Establish baseline sweep defaults applied to future runs.
 *
 * @param defaults Desired default configuration captured from host commands or firmware setup.
 */
void phoenix_benchmark_pot_sweep_initialise(const PhoenixBenchmarkPotSweepDefaults& defaults);
/**
 * @brief Reset module state to factory defaults and clear testing hooks.
 */
void phoenix_benchmark_pot_sweep_reset_state(void);
/**
 * @brief Parse a serialized command payload describing a potentiometer sweep.
 *
 * @param line Raw command string, typically JSON, supplied by the host.
 *
 * @return Parse outcome including populated options and optional error string.
 */
PhoenixBenchmarkPotSweepParseResult phoenix_benchmark_pot_sweep_parse_command(const char* line);
/**
 * @brief Execute a potentiometer sweep using the provided options and row buffer.
 *
 * @param options       Caller-supplied configuration (defaults applied internally as needed).
 * @param rows          Buffer receiving per-wiper metrics.
 * @param row_capacity  Number of entries available in the rows buffer.
 *
 * @return Execution status containing success flag, warnings, and recommendation metadata.
 */
PhoenixBenchmarkPotSweepExecutionStatus phoenix_benchmark_pot_sweep_run(const PhoenixBenchmarkPotSweepOptions& options,
                                                                        PhoenixBenchmarkPotSweepRowMetrics*    rows,
                                                                        std::size_t row_capacity);

/**
 * @brief Retrieve the ADC code threshold used to detect saturation events.
 *
 * @return Absolute ADC code corresponding to 90% of full scale.
 */
int32_t phoenix_benchmark_pot_sweep_saturation_threshold(void);
/**
 * @brief Override the hardware readiness check to avoid real device dependencies during tests.
 *
 * @param checker Replacement predicate or nullptr to restore defaults.
 */
void phoenix_benchmark_pot_sweep_set_hardware_ready_checker_for_test(bool (*checker)(void));
/**
 * @brief Override the ADC default configurator used during pot sweep tests.
 *
 * Passing nullptr restores the production implementation.
 */
void phoenix_benchmark_pot_sweep_set_adc_default_configurator_for_test(int (*configurator)(void));
/**
 * @brief Restore production hooks after tests replace runner or readiness helpers.
 */
void phoenix_benchmark_pot_sweep_clear_test_hooks(void);

#endif  // PHOENIX_BENCHMARK_POT_SWEEP_HPP
