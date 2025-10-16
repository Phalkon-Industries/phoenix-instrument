#include "adc_speed.hpp"

void phoenix_benchmark_adc_speed_initialise(const PhoenixBenchmarkAdcSpeedDefaults& defaults) {
  (void) defaults;
}

void phoenix_benchmark_adc_speed_reset_state(void) {
}

PhoenixBenchmarkAdcSpeedOptions phoenix_benchmark_adc_speed_defaults(void) {
  PhoenixBenchmarkAdcSpeedOptions options = {0u, false, false};
  return options;
}

PhoenixBenchmarkAdcSpeedExecutionStatus phoenix_benchmark_adc_speed_run(const PhoenixBenchmarkAdcSpeedOptions& options,
                                                                        PhoenixBenchmarkStateAccumulator* accumulators,
                                                                        std::size_t accumulator_count) {
  (void) options;
  (void) accumulators;
  (void) accumulator_count;
  PhoenixBenchmarkAdcSpeedExecutionStatus status = {};
  status.success                                 = false;
  status.message                                 = "adc_speed_not_implemented";
  return status;
}
