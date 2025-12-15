#include "cli.hpp"
#include "device_setup.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <unity.h>

static uint32_t g_last_sweep_requested = 0u;

static int stub_sweep_success(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  g_last_sweep_requested = sweep_count;
  if (results_out != NULL) {
    results_out->sweep_count = sweep_count;
  }
  return LIGHT_READINGS_OK;
}

static int stub_sweep_error(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  g_last_sweep_requested = sweep_count;
  if (results_out != NULL) {
    results_out->sweep_count = sweep_count;
  }
  return LIGHT_READINGS_ERR_INVALID_ARG;
}

static int stub_compute_success(const LightReadingsSweepCollection* sweep_collection,
                                LightReadingsSweepStats*            stats_out) {
  if ((sweep_collection == NULL) || (stats_out == NULL)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  stats_out->sweep_count                   = sweep_collection->sweep_count;
  stats_out->drain_blue.sample_count       = 4u;
  stats_out->drain_blue.mean               = 1.25;
  stats_out->drain_blue.standard_deviation = 0.5;
  stats_out->drain_blue.min_value          = -2;
  stats_out->drain_blue.max_value          = 3;
  stats_out->drain_blue.drift_slope        = 0.1;
  stats_out->drain_blue.has_samples        = true;

  stats_out->drain_green.sample_count       = 4u;
  stats_out->drain_green.mean               = 2.5;
  stats_out->drain_green.standard_deviation = 0.75;
  stats_out->drain_green.min_value          = -1;
  stats_out->drain_green.max_value          = 4;
  stats_out->drain_green.drift_slope        = 0.2;
  stats_out->drain_green.has_samples        = true;

  stats_out->blue.sample_count       = 4u;
  stats_out->blue.mean               = 3.75;
  stats_out->blue.standard_deviation = 1.25;
  stats_out->blue.min_value          = 0;
  stats_out->blue.max_value          = 6;
  stats_out->blue.drift_slope        = 0.3;
  stats_out->blue.has_samples        = true;

  stats_out->green.sample_count       = 4u;
  stats_out->green.mean               = 4.5;
  stats_out->green.standard_deviation = 1.5;
  stats_out->green.min_value          = 1;
  stats_out->green.max_value          = 7;
  stats_out->green.drift_slope        = 0.4;
  stats_out->green.has_samples        = true;

  return LIGHT_READINGS_OK;
}

static const CliMeasurementHooks k_stub_hooks_success = {stub_sweep_success, stub_compute_success};
static const CliMeasurementHooks k_stub_hooks_error   = {stub_sweep_error, stub_compute_success};

void setUp(void) {
  cli_initialize();
  cli_test_set_measurement_hooks(&k_stub_hooks_success);
  g_last_sweep_requested = 0u;
}

void tearDown(void) {
  cli_test_set_measurement_hooks(NULL);
}

static void test_cli_dispatch_rejects_empty_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::empty_command, (int) cli_dispatch_command(""));
}

static void test_cli_dispatch_rejects_unknown_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::unknown_command, (int) cli_dispatch_command("unknown"));
}

static void test_cli_dispatch_accepts_baseline_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("b"));
}

static void test_cli_dispatch_accepts_help_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("help"));
}

static void test_cli_baseline_command_sets_cached_flag(void) {
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("b"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());
}

static void test_cli_baseline_command_caches_stats_and_sweep_count(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("b"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());

  LightReadingsSweepStats cached_stats = {};
  cli_test_get_baseline_stats(&cached_stats);

  TEST_ASSERT_EQUAL_UINT(500u, g_last_sweep_requested);
  TEST_ASSERT_EQUAL_UINT(500u, cached_stats.sweep_count);
  TEST_ASSERT_EQUAL_UINT(4u, cached_stats.drain_blue.sample_count);
  TEST_ASSERT_EQUAL_UINT(4u, cached_stats.drain_green.sample_count);
  TEST_ASSERT_EQUAL_UINT(4u, cached_stats.blue.sample_count);
  TEST_ASSERT_EQUAL_UINT(4u, cached_stats.green.sample_count);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.1, cached_stats.drain_blue.drift_slope);
}

static void test_cli_sample_without_baseline_reports_missing(void) {
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("s"));
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
}

static void test_cli_sample_after_baseline_uses_cached_flag(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("b"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("s"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());
}

static void test_cli_baseline_failure_does_not_set_cache(void) {
  cli_test_set_measurement_hooks(&k_stub_hooks_error);

  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("b"));
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  UNITY_BEGIN();

  cli_initialize();

  RUN_TEST(test_cli_dispatch_rejects_empty_command);
  RUN_TEST(test_cli_dispatch_rejects_unknown_command);
  RUN_TEST(test_cli_dispatch_accepts_baseline_command);
  RUN_TEST(test_cli_dispatch_accepts_help_command);
  RUN_TEST(test_cli_baseline_command_sets_cached_flag);
  RUN_TEST(test_cli_baseline_command_caches_stats_and_sweep_count);
  RUN_TEST(test_cli_sample_without_baseline_reports_missing);
  RUN_TEST(test_cli_sample_after_baseline_uses_cached_flag);
  RUN_TEST(test_cli_baseline_failure_does_not_set_cache);

  UNITY_END();
}

void loop() {
}
