#ifndef PHOENIX_BENCHMARK_OSR_LATENCY_HPP
#define PHOENIX_BENCHMARK_OSR_LATENCY_HPP

#include "../../mcp356x/mcp356x.hpp"
#include "../core/phoenix_benchmark_core.hpp"
#include "osr_latency_formatter.hpp"
#include <cstddef>
#include <cstdint>

struct PhoenixBenchmarkOsrLatencyDefaults {
  uint32_t warmup_count;
  uint32_t sample_count;
  bool     include_blocking;
  bool     include_irq;
};

struct PhoenixBenchmarkOsrLatencyOptions {
  uint32_t warmup_count;
  uint32_t sample_count;
  bool     include_blocking;
  bool     include_irq;
  bool     has_warmup_override;
  bool     has_sample_override;
  bool     has_blocking_override;
  bool     has_irq_override;

  void apply_defaults(const PhoenixBenchmarkOsrLatencyDefaults& defaults);

  bool validate(const char** error_message) const;
};

struct PhoenixBenchmarkOsrLatencyParseResult {
  bool                              success;
  PhoenixBenchmarkOsrLatencyOptions options;
  const char*                       error_message;
};

void phoenix_benchmark_osr_latency_initialise(const PhoenixBenchmarkOsrLatencyDefaults& defaults);

void phoenix_benchmark_osr_latency_reset_state(void);

PhoenixBenchmarkOsrLatencyOptions phoenix_benchmark_osr_latency_defaults(void);

PhoenixBenchmarkOsrLatencyParseResult phoenix_benchmark_osr_latency_parse_command(const char* line);

struct PhoenixBenchmarkOsrLatencyMeasurement {
  uint32_t                               osr_value;
  mcp356x_sampling_mode                  mode;
  PhoenixBenchmarkRunningStats<uint32_t> latency_us;
  uint32_t                               warmup_count;
  uint32_t                               sample_count;
  bool                                   executed;
};

static constexpr std::size_t k_phoenix_benchmark_osr_latency_row_capacity = 32u;

struct PhoenixBenchmarkOsrLatencyOutputCallbacks {
  void (*summary_writer)(const char* line);
};

struct PhoenixBenchmarkOsrLatencyExecutionStatus {
  bool        success;
  bool        has_warnings;
  const char* message;
  uint32_t    rows_generated;
};

PhoenixBenchmarkOsrLatencyExecutionStatus phoenix_benchmark_osr_latency_run(
    const PhoenixBenchmarkOsrLatencyOptions& options, PhoenixBenchmarkOsrLatencyMeasurement* rows,
    std::size_t row_capacity, const PhoenixBenchmarkOsrLatencyOutputCallbacks& callbacks);

void phoenix_benchmark_osr_latency_set_blocking_sampler_for_test(bool (*sampler)(
    mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count, PhoenixBenchmarkRunningStats<uint32_t>* stats_out));

void phoenix_benchmark_osr_latency_set_irq_sampler_for_test(bool (*sampler)(
    mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count, PhoenixBenchmarkRunningStats<uint32_t>* stats_out));

void phoenix_benchmark_osr_latency_clear_test_hooks(void);

#endif  // PHOENIX_BENCHMARK_OSR_LATENCY_HPP
