#include "mcp356x.hpp"
#include "unity_config.h"
#include <unity.h>

static void test_mcp356x_estimate_conversion_delay_returns_measured_blocking_latency(void) {
  const mcp356x_conversion_latency_entry* latency_table = NULL;
  const size_t                            table_count   = mcp356x_test_get_conversion_latency_table(&latency_table);
  TEST_ASSERT_NOT_NULL(latency_table);
  TEST_ASSERT_NOT_EQUAL(0u, table_count);

  // Step 1: Iterate the driver’s latency table so the test always reflects the currently published dataset.
  for (size_t index = 0u; index < table_count; ++index) {
    const uint32_t expected_us = static_cast<uint32_t>(latency_table[index].blocking_latency_us + 0.5f);
    const uint32_t observed_us =
        mcp356x_estimate_conversion_delay(latency_table[index].osr, mcp356x_sampling_mode::blocking);
    TEST_ASSERT_EQUAL_UINT32(expected_us, observed_us);
  }
}

static void test_mcp356x_estimate_conversion_delay_returns_measured_irq_latency(void) {
  const mcp356x_conversion_latency_entry* latency_table = NULL;
  const size_t                            table_count   = mcp356x_test_get_conversion_latency_table(&latency_table);
  TEST_ASSERT_NOT_NULL(latency_table);
  TEST_ASSERT_NOT_EQUAL(0u, table_count);

  // Step 1: Iterate the driver’s latency table to ensure IRQ-mode rounding mirrors the blocking path.
  for (size_t index = 0u; index < table_count; ++index) {
    const uint32_t expected_us = static_cast<uint32_t>(latency_table[index].irq_latency_us + 0.5f);
    const uint32_t observed_us =
        mcp356x_estimate_conversion_delay(latency_table[index].osr, mcp356x_sampling_mode::irq);
    TEST_ASSERT_EQUAL_UINT32(expected_us, observed_us);
  }
}

void run_mcp356x_latency_lookup_tests(void) {
  // Step 1: Register empirical coverage so regressions surface if the lookup drifts from measured latencies.
  RUN_TEST(test_mcp356x_estimate_conversion_delay_returns_measured_blocking_latency);
  RUN_TEST(test_mcp356x_estimate_conversion_delay_returns_measured_irq_latency);
}
