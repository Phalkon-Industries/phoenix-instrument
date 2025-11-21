#include "mcp356x.hpp"
#include "unity_config.h"
#include <unity.h>

namespace {
struct LatencyExpectation {
  mcp356x_osr osr;
  uint32_t    blocking_latency_us;
  uint32_t    irq_latency_us;
};

// Step 1: Mirror the maximum latencies captured on Stormcloud v1.0.0 (see
// python/benchmark_runs/report.md for the measurement transcript).
constexpr LatencyExpectation k_latency_expectations[] = {
    {mcp356x_osr::osr_32, 977u, 977u},        {mcp356x_osr::osr_64, 977u, 977u},
    {mcp356x_osr::osr_128, 977u, 977u},       {mcp356x_osr::osr_256, 977u, 977u},
    {mcp356x_osr::osr_512, 1954u, 1954u},     {mcp356x_osr::osr_1024, 2930u, 2930u},
    {mcp356x_osr::osr_2048, 2930u, 2930u},    {mcp356x_osr::osr_4096, 4883u, 4883u},
    {mcp356x_osr::osr_8192, 8790u, 8790u},    {mcp356x_osr::osr_16384, 15625u, 15625u},
    {mcp356x_osr::osr_20480, 19532u, 19532u}, {mcp356x_osr::osr_24576, 23438u, 23438u},
    {mcp356x_osr::osr_40960, 37110u, 37110u}, {mcp356x_osr::osr_49152, 44922u, 44922u},
    {mcp356x_osr::osr_81920, 73243u, 73243u}, {mcp356x_osr::osr_98304, 87891u, 87891u},
};

constexpr size_t k_latency_expectation_count = sizeof(k_latency_expectations) / sizeof(k_latency_expectations[0]);
}  // namespace

static void test_mcp356x_estimate_conversion_delay_returns_measured_blocking_latency(void) {
  // Step 1: Iterate the OSR table to ensure the measured blocking latencies surface for each entry.
  for (size_t index = 0u; index < k_latency_expectation_count; ++index) {
    const LatencyExpectation& expectation = k_latency_expectations[index];
    const uint32_t observed_us = mcp356x_estimate_conversion_delay(expectation.osr, mcp356x_sampling_mode::blocking);
    TEST_ASSERT_EQUAL_UINT32(expectation.blocking_latency_us, observed_us);
  }
}

static void test_mcp356x_estimate_conversion_delay_returns_measured_irq_latency(void) {
  // Step 1: Iterate the OSR table to confirm the measured IRQ latencies surface for each entry.
  for (size_t index = 0u; index < k_latency_expectation_count; ++index) {
    const LatencyExpectation& expectation = k_latency_expectations[index];
    const uint32_t observed_us = mcp356x_estimate_conversion_delay(expectation.osr, mcp356x_sampling_mode::irq);
    TEST_ASSERT_EQUAL_UINT32(expectation.irq_latency_us, observed_us);
  }
}

void run_mcp356x_latency_lookup_tests(void) {
  // Step 1: Register empirical coverage so regressions surface if the lookup drifts from measured latencies.
  RUN_TEST(test_mcp356x_estimate_conversion_delay_returns_measured_blocking_latency);
  RUN_TEST(test_mcp356x_estimate_conversion_delay_returns_measured_irq_latency);
}
