#include "mcp356x.hpp"
#include "unity_config.h"
#include <unity.h>

namespace {
struct LatencyExpectation {
  mcp356x_osr osr;
  uint32_t    placeholder_polling_us;
  uint32_t    placeholder_irq_us;
};

constexpr uint32_t k_placeholder_polling_increment_us = 10u;
constexpr uint32_t k_placeholder_irq_increment_us     = 8u;

constexpr LatencyExpectation k_latency_expectations[] = {
    {mcp356x_osr::osr_32, k_placeholder_polling_increment_us * 1u, k_placeholder_irq_increment_us * 1u},
    {mcp356x_osr::osr_64, k_placeholder_polling_increment_us * 2u, k_placeholder_irq_increment_us * 2u},
    {mcp356x_osr::osr_128, k_placeholder_polling_increment_us * 3u, k_placeholder_irq_increment_us * 3u},
    {mcp356x_osr::osr_256, k_placeholder_polling_increment_us * 4u, k_placeholder_irq_increment_us * 4u},
    {mcp356x_osr::osr_512, k_placeholder_polling_increment_us * 5u, k_placeholder_irq_increment_us * 5u},
    {mcp356x_osr::osr_1024, k_placeholder_polling_increment_us * 6u, k_placeholder_irq_increment_us * 6u},
    {mcp356x_osr::osr_2048, k_placeholder_polling_increment_us * 7u, k_placeholder_irq_increment_us * 7u},
    {mcp356x_osr::osr_4096, k_placeholder_polling_increment_us * 8u, k_placeholder_irq_increment_us * 8u},
    {mcp356x_osr::osr_8192, k_placeholder_polling_increment_us * 9u, k_placeholder_irq_increment_us * 9u},
    {mcp356x_osr::osr_16384, k_placeholder_polling_increment_us * 10u, k_placeholder_irq_increment_us * 10u},
    {mcp356x_osr::osr_20480, k_placeholder_polling_increment_us * 11u, k_placeholder_irq_increment_us * 11u},
    {mcp356x_osr::osr_24576, k_placeholder_polling_increment_us * 12u, k_placeholder_irq_increment_us * 12u},
    {mcp356x_osr::osr_40960, k_placeholder_polling_increment_us * 13u, k_placeholder_irq_increment_us * 13u},
    {mcp356x_osr::osr_49152, k_placeholder_polling_increment_us * 14u, k_placeholder_irq_increment_us * 14u},
    {mcp356x_osr::osr_81920, k_placeholder_polling_increment_us * 15u, k_placeholder_irq_increment_us * 15u},
    {mcp356x_osr::osr_98304, k_placeholder_polling_increment_us * 16u, k_placeholder_irq_increment_us * 16u},
};

constexpr size_t k_latency_expectation_count = sizeof(k_latency_expectations) / sizeof(k_latency_expectations[0]);
}  // namespace

static void test_mcp356x_estimate_conversion_delay_returns_placeholder_polling_values(void) {
  // Step 1: Iterate the OSR table to ensure placeholder polling latencies surface for each entry.
  for (size_t index = 0u; index < k_latency_expectation_count; ++index) {
    const LatencyExpectation& expectation = k_latency_expectations[index];
    const uint32_t observed_us = mcp356x_estimate_conversion_delay(expectation.osr, mcp356x_sampling_mode::blocking);
    TEST_ASSERT_EQUAL_UINT32(expectation.placeholder_polling_us, observed_us);
  }
}

static void test_mcp356x_estimate_conversion_delay_returns_placeholder_irq_values(void) {
  // Step 1: Iterate the OSR table to confirm placeholder IRQ latencies surface for each entry.
  for (size_t index = 0u; index < k_latency_expectation_count; ++index) {
    const LatencyExpectation& expectation = k_latency_expectations[index];
    const uint32_t observed_us = mcp356x_estimate_conversion_delay(expectation.osr, mcp356x_sampling_mode::irq);
    TEST_ASSERT_EQUAL_UINT32(expectation.placeholder_irq_us, observed_us);
  }
}

void run_mcp356x_latency_lookup_tests(void) {
  // Step 1: Register placeholder coverage until empirical measurements replace the table values.
  RUN_TEST(test_mcp356x_estimate_conversion_delay_returns_placeholder_polling_values);
  RUN_TEST(test_mcp356x_estimate_conversion_delay_returns_placeholder_irq_values);
}
