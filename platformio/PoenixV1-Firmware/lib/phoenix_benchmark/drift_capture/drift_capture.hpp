#ifndef PHOENIX_BENCHMARK_DRIFT_CAPTURE_HPP
#define PHOENIX_BENCHMARK_DRIFT_CAPTURE_HPP

#include "../../ad524x/ad524x.hpp"
#include "../../adc_hal/adc_hal.hpp"
#include "../../led_router/led_router.hpp"
#include "../../mcp356x/mcp356x.hpp"
#include "../core/phoenix_benchmark_core.hpp"
#include <cstddef>
#include <cstdint>

constexpr std::size_t k_phoenix_benchmark_drift_capture_max_sample_count = 2048u;

constexpr uint8_t k_phoenix_benchmark_drift_capture_warning_buffer_overflow = 0x01u;
constexpr uint8_t k_phoenix_benchmark_drift_capture_warning_saturation      = 0x02u;
constexpr uint8_t k_phoenix_benchmark_drift_capture_warning_restore_failed  = 0x04u;

struct PhoenixBenchmarkDriftCaptureDefaults {
  uint32_t start_time_us;
  uint32_t end_time_us;
  uint32_t step_delay_us;
  uint32_t osr;
  uint8_t  wiper_code;
};

struct PhoenixBenchmarkDriftCaptureOptions {
  uint32_t start_time_us;
  bool     has_start_override;
  uint32_t end_time_us;
  bool     has_end_override;
  uint32_t step_delay_us;
  bool     has_step_override;
  uint32_t osr;
  bool     has_osr_override;
  uint8_t  wiper_code;
  bool     has_wiper_override;

  void apply_defaults(const PhoenixBenchmarkDriftCaptureDefaults& defaults);
  bool validate(const char** error_message) const;
};

struct PhoenixBenchmarkDriftCaptureParseResult {
  bool                                success;
  PhoenixBenchmarkDriftCaptureOptions options;
  const char*                         error_message;
};

struct PhoenixBenchmarkDriftCaptureSample {
  uint32_t elapsed_microseconds;
  int32_t  adc_code;
};

struct PhoenixBenchmarkDriftCaptureExecutionStatus {
  bool        success;
  bool        has_warnings;
  uint8_t     warning_mask;
  const char* message;
  uint32_t    applied_start_us;
  uint32_t    applied_end_us;
  uint32_t    applied_step_us;
  uint32_t    applied_osr;
  uint8_t     applied_wiper_code;
  std::size_t led1_samples;
  std::size_t led2_samples;
};

enum class PhoenixBenchmarkDriftCaptureLed : uint8_t {
  kLed1 = 0u,
  kLed2 = 1u,
};

struct PhoenixBenchmarkDriftCaptureOutputCallbacks {
  void (*print_line)(const char* line);
};

void phoenix_benchmark_drift_capture_initialise(const PhoenixBenchmarkDriftCaptureDefaults& defaults);
void phoenix_benchmark_drift_capture_reset_state(void);
PhoenixBenchmarkDriftCaptureParseResult     phoenix_benchmark_drift_capture_parse_command(const char* line);
PhoenixBenchmarkDriftCaptureExecutionStatus phoenix_benchmark_drift_capture_run(
    const PhoenixBenchmarkDriftCaptureOptions& options, const PhoenixBenchmarkDriftCaptureOutputCallbacks& callbacks);

const PhoenixBenchmarkDriftCaptureSample* phoenix_benchmark_drift_capture_led_samples(
    PhoenixBenchmarkDriftCaptureLed led, std::size_t* count_out);

void phoenix_benchmark_drift_capture_set_hardware_ready_checker_for_test(bool (*checker)(void));
void phoenix_benchmark_drift_capture_set_wiper_setter_for_test(bool (*setter)(uint8_t wiper_code));
void phoenix_benchmark_drift_capture_set_led_setter_for_test(int (*setter)(LedRouterState state));
void phoenix_benchmark_drift_capture_set_osr_setter_for_test(int (*setter)(mcp356x_osr value));
void phoenix_benchmark_drift_capture_set_adc_reader_for_test(bool (*reader)(AdcHalChannel channel, int32_t* code_out));
void phoenix_benchmark_drift_capture_set_micros_provider_for_test(uint32_t (*provider)(void));
void phoenix_benchmark_drift_capture_set_delay_provider_for_test(void (*provider)(uint32_t));
void phoenix_benchmark_drift_capture_clear_test_hooks(void);

#endif  // PHOENIX_BENCHMARK_DRIFT_CAPTURE_HPP
