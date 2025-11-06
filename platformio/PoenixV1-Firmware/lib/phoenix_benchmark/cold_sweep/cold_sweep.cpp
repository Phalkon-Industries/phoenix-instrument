#include "cold_sweep.hpp"

#include <cstddef>
#include <cstdint>

namespace {
[[maybe_unused]] int (*g_light_readings_runner)(uint32_t, LightReadingsSweepCollection*)                  = nullptr;
[[maybe_unused]] int (*g_stats_calculator)(const LightReadingsSweepCollection*, LightReadingsSweepStats*) = nullptr;
[[maybe_unused]] bool (*g_saturation_checker)(void)                                                       = nullptr;
[[maybe_unused]] uint32_t (*g_timestamp_provider)(void)                                                   = nullptr;
}  // namespace

void phoenix_benchmark_cold_sweep_reset_state(void) {
  phoenix_benchmark_cold_sweep_clear_test_hooks();
}

PhoenixBenchmarkColdSweepExecutionStatus phoenix_benchmark_cold_sweep_run(
    const PhoenixBenchmarkColdSweepOptions& options, LightReadingsSweepCollection* sweeps_out,
    LightReadingsSweepStats* stats_out) {
  (void) options;
  (void) sweeps_out;
  (void) stats_out;

  PhoenixBenchmarkColdSweepExecutionStatus status = {};
  status.success                                  = false;
  status.has_warnings                             = false;
  status.warning_mask                             = 0u;
  status.message                                  = "cold_sweep_not_implemented";
  status.captured_sweeps                          = 0u;
  status.timestamp_us                             = 0u;
  return status;
}

void phoenix_benchmark_cold_sweep_set_light_readings_runner_for_test(
    int (*runner)(uint32_t sweep_count, LightReadingsSweepCollection* collection)) {
  g_light_readings_runner = runner;
}

void phoenix_benchmark_cold_sweep_set_stats_calculator_for_test(
    int (*calculator)(const LightReadingsSweepCollection* collection, LightReadingsSweepStats* stats_out)) {
  g_stats_calculator = calculator;
}

void phoenix_benchmark_cold_sweep_set_saturation_checker_for_test(bool (*checker)(void)) {
  g_saturation_checker = checker;
}

void phoenix_benchmark_cold_sweep_set_timestamp_provider_for_test(uint32_t (*provider)(void)) {
  g_timestamp_provider = provider;
}

void phoenix_benchmark_cold_sweep_clear_test_hooks(void) {
  g_light_readings_runner = nullptr;
  g_stats_calculator      = nullptr;
  g_saturation_checker    = nullptr;
  g_timestamp_provider    = nullptr;
}
