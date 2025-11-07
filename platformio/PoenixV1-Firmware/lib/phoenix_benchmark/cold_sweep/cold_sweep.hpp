#ifndef PHOENIX_BENCHMARK_COLD_SWEEP_HPP
#define PHOENIX_BENCHMARK_COLD_SWEEP_HPP

#include "../../light_readings/light_readings.hpp"
#include <cstddef>
#include <cstdint>

constexpr uint8_t k_phoenix_benchmark_cold_sweep_warning_saturation = 0x01u;

struct PhoenixBenchmarkColdSweepOptions {
  uint32_t sweep_count;
  bool     has_sweep_override;
  uint32_t dwell_override_us;
  bool     has_dwell_override;
};

struct PhoenixBenchmarkColdSweepExecutionStatus {
  bool        success;
  bool        has_warnings;
  uint8_t     warning_mask;
  const char* message;
  uint32_t    captured_sweeps;
  uint32_t    timestamp_us;
};

struct PhoenixBenchmarkColdSweepParseResult {
  bool                             success;
  PhoenixBenchmarkColdSweepOptions options;
  const char*                      error_message;
};

PhoenixBenchmarkColdSweepParseResult phoenix_benchmark_cold_sweep_parse_command(const char* line);

void                                     phoenix_benchmark_cold_sweep_reset_state(void);
PhoenixBenchmarkColdSweepExecutionStatus phoenix_benchmark_cold_sweep_run(
    const PhoenixBenchmarkColdSweepOptions& options, LightReadingsSweepCollection* sweeps_out,
    LightReadingsSweepStats* stats_out);

void phoenix_benchmark_cold_sweep_set_light_readings_runner_for_test(
    int (*runner)(uint32_t sweep_count, LightReadingsSweepCollection* collection));
void phoenix_benchmark_cold_sweep_set_stats_calculator_for_test(
    int (*calculator)(const LightReadingsSweepCollection* collection, LightReadingsSweepStats* stats_out));
void phoenix_benchmark_cold_sweep_set_saturation_checker_for_test(bool (*checker)(void));
void phoenix_benchmark_cold_sweep_set_timestamp_provider_for_test(uint32_t (*provider)(void));
void phoenix_benchmark_cold_sweep_set_hardware_ready_checker_for_test(bool (*checker)(void));
void phoenix_benchmark_cold_sweep_clear_test_hooks(void);

#endif  // PHOENIX_BENCHMARK_COLD_SWEEP_HPP
