#ifndef PHOENIX_BENCHMARK_DWELL_SWEEP_HPP
#define PHOENIX_BENCHMARK_DWELL_SWEEP_HPP

#include "../channel_map/channel_map.hpp"
#include "../core/phoenix_benchmark_core.hpp"
#include <cstddef>
#include <cstdint>

constexpr std::size_t k_phoenix_benchmark_dwell_sweep_max_step_count = 128u;

constexpr uint8_t k_phoenix_benchmark_dwell_sweep_warning_saturation = 0x01u;
constexpr uint8_t k_phoenix_benchmark_dwell_sweep_warning_adc_error  = 0x02u;
constexpr uint8_t k_phoenix_benchmark_dwell_sweep_warning_alignment  = 0x04u;

struct PhoenixBenchmarkDwellSweepDefaults {
  uint32_t sweeps_per_dwell;
  uint32_t start_dwell_us;
  uint32_t end_dwell_us;
  uint32_t dwell_step_us;
};

struct PhoenixBenchmarkDwellSweepOptions {
  uint32_t sweeps_per_dwell;
  bool     has_sweeps_override;
  uint32_t start_dwell_us;
  bool     has_start_override;
  uint32_t end_dwell_us;
  bool     has_end_override;
  uint32_t dwell_step_us;
  bool     has_step_override;

  void apply_defaults(const PhoenixBenchmarkDwellSweepDefaults& defaults);
  bool validate(const char** error_message) const;
};

struct PhoenixBenchmarkDwellSweepParseResult {
  bool                              success;
  PhoenixBenchmarkDwellSweepOptions options;
  const char*                       error_message;
};

struct PhoenixBenchmarkDwellSweepRowMetrics {
  uint32_t                         dwell_us;
  uint32_t                         sweeps_requested;
  uint32_t                         sweeps_completed;
  PhoenixBenchmarkStateAccumulator drain;
  PhoenixBenchmarkStateAccumulator led1;
  PhoenixBenchmarkStateAccumulator led2;
  PhoenixBenchmarkChannel          dominant_channel;
  uint32_t                         elapsed_microseconds;
  uint8_t                          warning_mask;
  const char*                      error_message;
};

struct PhoenixBenchmarkDwellSweepExecutionStatus {
  bool        success;
  bool        has_warnings;
  const char* message;
  uint32_t    rows_generated;
};

void phoenix_benchmark_dwell_sweep_initialise(const PhoenixBenchmarkDwellSweepDefaults& defaults);
void phoenix_benchmark_dwell_sweep_reset_state(void);
PhoenixBenchmarkDwellSweepParseResult     phoenix_benchmark_dwell_sweep_parse_command(const char* line);
PhoenixBenchmarkDwellSweepExecutionStatus phoenix_benchmark_dwell_sweep_run(
    const PhoenixBenchmarkDwellSweepOptions& options, PhoenixBenchmarkDwellSweepRowMetrics* rows,
    std::size_t row_capacity);

void phoenix_benchmark_dwell_sweep_set_channel_map_runner_for_test(PhoenixBenchmarkChannelMapExecutionStatus (*runner)(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks));
void phoenix_benchmark_dwell_sweep_set_hardware_ready_checker_for_test(bool (*checker)(void));
void phoenix_benchmark_dwell_sweep_set_micros_provider_for_test(uint32_t (*provider)(void));
void phoenix_benchmark_dwell_sweep_clear_test_hooks(void);

#endif  // PHOENIX_BENCHMARK_DWELL_SWEEP_HPP
