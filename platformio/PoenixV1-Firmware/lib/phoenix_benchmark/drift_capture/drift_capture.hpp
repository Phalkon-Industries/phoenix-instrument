#ifndef PHOENIX_BENCHMARK_DRIFT_CAPTURE_HPP
#define PHOENIX_BENCHMARK_DRIFT_CAPTURE_HPP

#include "../../ad524x/ad524x.hpp"
#include "../../adc_hal/adc_hal.hpp"
#include "../../led_router/led_router.hpp"
#include "../../light_readings/light_readings.hpp"
#include "../../mcp356x/mcp356x.hpp"
#include "../core/phoenix_benchmark_core.hpp"
#include <cstddef>
#include <cstdint>

/**
 * @brief Maximum number of samples recorded per LED trace during a drift capture run.
 */
constexpr std::size_t k_phoenix_benchmark_drift_capture_max_sample_count = 2048u;

/**
 * @brief Warning flag indicating that at least one LED trace overflowed the capture buffer.
 */
constexpr uint8_t k_phoenix_benchmark_drift_capture_warning_buffer_overflow = 0x01u;
/**
 * @brief Warning flag indicating that at least one captured ADC code reached saturation.
 */
constexpr uint8_t k_phoenix_benchmark_drift_capture_warning_saturation = 0x02u;
/**
 * @brief Warning flag indicating that the MCP356x OSR could not be restored to the default value.
 */
constexpr uint8_t k_phoenix_benchmark_drift_capture_warning_restore_failed = 0x04u;

/**
 * @brief Default configuration values used when a drift capture command omits explicit overrides.
 */
struct PhoenixBenchmarkDriftCaptureDefaults {
  uint32_t start_time_us;
  uint32_t end_time_us;
  uint32_t step_delay_us;
  uint32_t osr;
  uint8_t  blue_wiper_code;
  uint8_t  green_wiper_code;
};

/**
 * @brief Command options accepted by the drift capture scenario, including override flags.
 */
struct PhoenixBenchmarkDriftCaptureOptions {
  uint32_t start_time_us;
  bool     has_start_override;
  uint32_t end_time_us;
  bool     has_end_override;
  uint32_t step_delay_us;
  bool     has_step_override;
  uint32_t osr;
  bool     has_osr_override;
  uint8_t  blue_wiper_code;
  uint8_t  green_wiper_code;
  bool     has_wiper_override;

  /**
   * @brief Populate unset overrides with the supplied defaults.
   * @param defaults Default values applied when an override flag is false.
   */
  void apply_defaults(const PhoenixBenchmarkDriftCaptureDefaults& defaults);
  /**
   * @brief Validate the option set, reporting the first error encountered.
   * @param error_message Optional pointer that receives a static error string on failure.
   * @return True when the options are internally consistent and supported.
   */
  bool validate(const char** error_message) const;
};

/**
 * @brief Result payload returned when parsing a drift capture command line or JSON request.
 */
struct PhoenixBenchmarkDriftCaptureParseResult {
  bool                                success;
  PhoenixBenchmarkDriftCaptureOptions options;
  const char*                         error_message;
};

/**
 * @brief Single ADC sample taken during a drift capture trace for a specific LED.
 */
struct PhoenixBenchmarkDriftCaptureSample {
  uint32_t elapsed_microseconds;
  int32_t  adc_code;
};

/**
 * @brief Aggregated status returned after executing a drift capture run.
 */
struct PhoenixBenchmarkDriftCaptureExecutionStatus {
  bool        success;
  bool        has_warnings;
  uint8_t     warning_mask;
  const char* message;
  uint32_t    applied_start_us;
  uint32_t    applied_end_us;
  uint32_t    applied_step_us;
  uint32_t    applied_osr;
  uint8_t     applied_blue_wiper_code;
  uint8_t     applied_green_wiper_code;
  std::size_t blue_samples;
  std::size_t green_samples;
};

/**
 * @brief Identifiers for the LEDs measured during drift capture.
 */
enum class PhoenixBenchmarkDriftCaptureLed : uint8_t {
  kBlue  = 0u,
  kGreen = 1u,
};

/**
 * @brief Output hooks used to stream command responses back to the host.
 */
struct PhoenixBenchmarkDriftCaptureOutputCallbacks {
  void (*print_line)(const char* line);
};

/**
 * @brief Initialise the drift capture subsystem with scenario defaults.
 * @param defaults Defaults applied whenever the user omits a configuration override.
 */
void phoenix_benchmark_drift_capture_initialise(const PhoenixBenchmarkDriftCaptureDefaults& defaults);
/**
 * @brief Restore the drift capture subsystem to its factory defaults, removing any test hooks.
 */
void phoenix_benchmark_drift_capture_reset_state(void);
/**
 * @brief Parse a CLI or JSON command into drift capture options.
 * @param line Null-terminated command string.
 * @return Parse result containing either the populated options or an error description.
 */
PhoenixBenchmarkDriftCaptureParseResult phoenix_benchmark_drift_capture_parse_command(const char* line);
/**
 * @brief Execute a drift capture run using the provided options.
 * @param options Fully-populated option set controlling delays, OSR, and digipot state.
 * @param callbacks Output callbacks used to emit human-readable telemetry.
 * @return Execution status including warning masks and the number of captured samples per LED.
 */
PhoenixBenchmarkDriftCaptureExecutionStatus phoenix_benchmark_drift_capture_run(
    const PhoenixBenchmarkDriftCaptureOptions& options, const PhoenixBenchmarkDriftCaptureOutputCallbacks& callbacks);

/**
 * @brief Derive drift capture defaults from a light readings configuration.
 *
 * @param light_config Light readings configuration describing per-colour wiper codes.
 * @param baseline_defaults Baseline drift defaults providing timing and OSR overrides.
 * @return Defaults populated with the light readings wiper codes while preserving other fields.
 */
PhoenixBenchmarkDriftCaptureDefaults phoenix_benchmark_drift_capture_defaults_from_light_config(
    const LightReadingsConfig& light_config, const PhoenixBenchmarkDriftCaptureDefaults& baseline_defaults);

/**
 * @brief Access the captured samples for a specific LED channel.
 * @param led Enum identifying the blue or green trace.
 * @param count_out Optional pointer that receives the number of valid samples.
 * @return Pointer to the head of the internal sample buffer for the requested LED.
 */
const PhoenixBenchmarkDriftCaptureSample* phoenix_benchmark_drift_capture_led_samples(
    PhoenixBenchmarkDriftCaptureLed led, std::size_t* count_out);

/**
 * @brief Override the hardware-ready checker used during unit testing.
 * @param checker Optional hook that returns true when the bench hardware is configured.
 */
void phoenix_benchmark_drift_capture_set_hardware_ready_checker_for_test(bool (*checker)(void));
/**
 * @brief Override the digipot wiper setter used during unit testing.
 * @param setter Optional hook that applies a wiper code and reports success.
 */
void phoenix_benchmark_drift_capture_set_wiper_setter_for_test(bool (*setter)(uint8_t blue_wiper_code,
                                                                              uint8_t green_wiper_code));
/**
 * @brief Override the LED router setter used during unit testing.
 * @param setter Optional hook that switches the LED routing state.
 */
void phoenix_benchmark_drift_capture_set_led_setter_for_test(int (*setter)(LedRouterState state));
/**
 * @brief Override the MCP356x OSR setter used during unit testing.
 * @param setter Optional hook that programs the desired OSR value.
 */
void phoenix_benchmark_drift_capture_set_osr_setter_for_test(int (*setter)(mcp356x_osr value));
/**
 * @brief Override the ADC reader used during unit testing.
 * @param reader Optional hook that fetches a single-ended sample for the requested channel.
 */
void phoenix_benchmark_drift_capture_set_adc_reader_for_test(bool (*reader)(AdcHalChannel channel, int32_t* code_out));
/**
 * @brief Override the micros() provider used during unit testing.
 * @param provider Optional hook that returns the current time in microseconds.
 */
void phoenix_benchmark_drift_capture_set_micros_provider_for_test(uint32_t (*provider)(void));
/**
 * @brief Override the microsecond delay provider used during unit testing.
 * @param provider Optional hook that blocks for the requested number of microseconds.
 */
void phoenix_benchmark_drift_capture_set_delay_provider_for_test(void (*provider)(uint32_t));
/**
 * @brief Clear all test hooks so runtime execution uses the production dependencies.
 */
void phoenix_benchmark_drift_capture_clear_test_hooks(void);

#endif  // PHOENIX_BENCHMARK_DRIFT_CAPTURE_HPP
