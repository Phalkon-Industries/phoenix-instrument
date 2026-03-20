#include "light_calibration.hpp"
#include "light_readings.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

namespace {

// ===================== Test Fixtures ============================================

static uint8_t  g_test_wiper_code        = 0u;
static uint32_t g_test_sweep_call_count  = 0u;
static bool     g_test_sweep_should_fail = false;
static int32_t  g_test_blue_max_by_wiper[256];
static int32_t  g_test_green_max_by_wiper[256];

// Mock sweep runner that returns configurable max codes per wiper value.
static int mock_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  if (g_test_sweep_should_fail) {
    return LIGHT_READINGS_ERR_TIMEOUT;
  }

  if ((results_out == nullptr) || (results_out->sweeps == nullptr)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  g_test_sweep_call_count++;

  // Populate a single sweep sample with the configured max codes for current wiper.
  if (sweep_count > 0u) {
    results_out->sweep_count          = 1u;
    results_out->sweeps[0].blue_code  = g_test_blue_max_by_wiper[g_test_wiper_code];
    results_out->sweeps[0].green_code = g_test_green_max_by_wiper[g_test_wiper_code];
    // Set drain codes to small values so they don't affect max detection.
    results_out->sweeps[0].drain_blue_code  = 100;
    results_out->sweeps[0].drain_green_code = 100;
  }
  else {
    results_out->sweep_count = 0u;
  }

  return LIGHT_READINGS_OK;
}

// Mock wiper setter that records the wiper code.
static int mock_set_wiper(uint8_t wiper_code) {
  g_test_wiper_code = wiper_code;
  return LIGHT_READINGS_OK;
}

// Resets all test state.
static void reset_test_state(void) {
  g_test_wiper_code        = 0u;
  g_test_sweep_call_count  = 0u;
  g_test_sweep_should_fail = false;

  // Default: linear increase with wiper code (higher wiper = more light = higher ADC code).
  for (int i = 0; i < 256; ++i) {
    g_test_blue_max_by_wiper[i]  = i * 30000;  // 0 to 7,650,000
    g_test_green_max_by_wiper[i] = i * 29000;  // 0 to 7,395,000
  }
}

// ===================== Test Cases ===============================================

// Test that calibration finds the optimal wiper below the saturation threshold.
static void test_calibration_finds_optimal_below_threshold(void) {
  reset_test_state();

  // Step 1: Configure test hooks.
  light_calibration_set_sweep_runner_for_test(mock_sweep_n);
  light_calibration_set_wiper_setter_for_test(mock_set_wiper);

  // Step 2: Set up ADC codes so wiper 250 is just below threshold (90% of 8,388,607 = 7,549,746).
  // Wipers 251-255 will exceed threshold and be skipped.
  // ADC codes increase linearly with wiper to ensure highest valid wiper wins.
  const int32_t threshold = light_calibration_saturation_threshold();
  for (int i = 0; i < 256; ++i) {
    // Blue: wipers 0-250 are below threshold (scaling up), 251-255 saturate.
    g_test_blue_max_by_wiper[i] = (i <= 250) ? (1000000 + i * 25000) : (threshold + 100000);
    // Green: wipers 0-248 are below threshold (scaling up), 249-255 saturate.
    g_test_green_max_by_wiper[i] = (i <= 248) ? (1000000 + i * 25000) : (threshold + 100000);
  }

  // Step 3: Run calibration with defaults.
  LightCalibrationConfig config = k_light_calibration_default_config;
  LightCalibrationResult result = light_calibration_run(&config);

  // Step 4: Verify success and recommended wipers are the highest below threshold.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_TRUE(result.blue_valid);
  TEST_ASSERT_EQUAL_UINT8(250u, result.blue_wiper_code);
  TEST_ASSERT_TRUE(result.green_valid);
  TEST_ASSERT_EQUAL_UINT8(248u, result.green_wiper_code);

  light_calibration_clear_test_hooks();
}

// Test that calibration returns wiper 0 when all values saturate.
static void test_calibration_handles_all_saturated(void) {
  reset_test_state();

  // Step 1: Configure test hooks.
  light_calibration_set_sweep_runner_for_test(mock_sweep_n);
  light_calibration_set_wiper_setter_for_test(mock_set_wiper);

  // Step 2: Set all ADC codes above saturation threshold.
  const int32_t threshold = light_calibration_saturation_threshold();
  for (int i = 0; i < 256; ++i) {
    g_test_blue_max_by_wiper[i]  = threshold + 100000;
    g_test_green_max_by_wiper[i] = threshold + 100000;
  }

  // Step 3: Run calibration.
  LightCalibrationConfig config = k_light_calibration_default_config;
  LightCalibrationResult result = light_calibration_run(&config);

  // Step 4: Verify we still get a result but with fallback to 0.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_FALSE(result.blue_valid);
  TEST_ASSERT_EQUAL_UINT8(0u, result.blue_wiper_code);
  TEST_ASSERT_FALSE(result.green_valid);
  TEST_ASSERT_EQUAL_UINT8(0u, result.green_wiper_code);

  light_calibration_clear_test_hooks();
}

// Test that calibration respects the sweeps_per_wiper configuration.
static void test_calibration_uses_configured_sweeps_per_wiper(void) {
  reset_test_state();

  // Step 1: Configure test hooks.
  light_calibration_set_sweep_runner_for_test(mock_sweep_n);
  light_calibration_set_wiper_setter_for_test(mock_set_wiper);

  // Step 2: Run calibration with custom sweep count over a limited range.
  LightCalibrationConfig config = k_light_calibration_default_config;
  config.start_wiper            = 0u;
  config.end_wiper              = 9u;  // Only 10 wiper values
  config.sweeps_per_wiper       = 7u;

  LightCalibrationResult result = light_calibration_run(&config);

  // Step 3: Verify sweep runner was called for each wiper value.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_EQUAL_UINT32(10u, g_test_sweep_call_count);

  light_calibration_clear_test_hooks();
}

// Test that blue and green have independent recommendations.
static void test_calibration_reports_per_channel_results(void) {
  reset_test_state();

  // Step 1: Configure test hooks.
  light_calibration_set_sweep_runner_for_test(mock_sweep_n);
  light_calibration_set_wiper_setter_for_test(mock_set_wiper);

  // Step 2: Set up so blue peaks at wiper 100 and green peaks at wiper 200.
  // ADC codes increase with wiper to ensure highest valid wiper is selected.
  const int32_t threshold = light_calibration_saturation_threshold();
  for (int i = 0; i < 256; ++i) {
    g_test_blue_max_by_wiper[i]  = (i <= 100) ? (1000000 + i * 60000) : (threshold + 100);
    g_test_green_max_by_wiper[i] = (i <= 200) ? (1000000 + i * 30000) : (threshold + 100);
  }

  // Step 3: Run calibration.
  LightCalibrationConfig config = k_light_calibration_default_config;
  LightCalibrationResult result = light_calibration_run(&config);

  // Step 4: Verify independent recommendations.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_TRUE(result.blue_valid);
  TEST_ASSERT_EQUAL_UINT8(100u, result.blue_wiper_code);
  TEST_ASSERT_TRUE(result.green_valid);
  TEST_ASSERT_EQUAL_UINT8(200u, result.green_wiper_code);

  light_calibration_clear_test_hooks();
}

// Test that calibration aborts and returns error on hardware failure.
static void test_calibration_aborts_on_hardware_error(void) {
  reset_test_state();

  // Step 1: Configure test hooks with failure mode.
  light_calibration_set_sweep_runner_for_test(mock_sweep_n);
  light_calibration_set_wiper_setter_for_test(mock_set_wiper);
  g_test_sweep_should_fail = true;

  // Step 2: Run calibration.
  LightCalibrationConfig config = k_light_calibration_default_config;
  LightCalibrationResult result = light_calibration_run(&config);

  // Step 3: Verify failure is reported.
  TEST_ASSERT_FALSE(result.success);
  TEST_ASSERT_NOT_NULL(result.error_message);

  light_calibration_clear_test_hooks();
}

// Test that null config uses defaults.
static void test_calibration_null_config_uses_defaults(void) {
  reset_test_state();

  // Step 1: Configure test hooks.
  light_calibration_set_sweep_runner_for_test(mock_sweep_n);
  light_calibration_set_wiper_setter_for_test(mock_set_wiper);

  // Step 2: Run calibration with null config.
  LightCalibrationResult result = light_calibration_run(nullptr);

  // Step 3: Verify it ran successfully with defaults (256 wiper values).
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_EQUAL_UINT32(256u, g_test_sweep_call_count);

  light_calibration_clear_test_hooks();
}

// Test that calibration handles restricted wiper range correctly.
static void test_calibration_wiper_range_boundary(void) {
  reset_test_state();

  // Step 1: Configure test hooks.
  light_calibration_set_sweep_runner_for_test(mock_sweep_n);
  light_calibration_set_wiper_setter_for_test(mock_set_wiper);

  // Step 2: Configure a narrow wiper range (start to end inclusive).
  LightCalibrationConfig config = k_light_calibration_default_config;
  config.start_wiper            = 100u;
  config.end_wiper              = 105u;  // Only 6 wiper values: 100, 101, 102, 103, 104, 105

  // Step 3: Run calibration with restricted range.
  LightCalibrationResult result = light_calibration_run(&config);

  // Step 4: Verify correct number of wiper values were tested.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_EQUAL_UINT32(6u, g_test_sweep_call_count);

  light_calibration_clear_test_hooks();
}

}  // namespace

void setUp(void) {
  reset_test_state();
}

void tearDown(void) {
  light_calibration_clear_test_hooks();
}

void setup(void) {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_calibration_finds_optimal_below_threshold);
  RUN_TEST(test_calibration_handles_all_saturated);
  RUN_TEST(test_calibration_uses_configured_sweeps_per_wiper);
  RUN_TEST(test_calibration_reports_per_channel_results);
  RUN_TEST(test_calibration_aborts_on_hardware_error);
  RUN_TEST(test_calibration_null_config_uses_defaults);
  RUN_TEST(test_calibration_wiper_range_boundary);
  UNITY_END();
}

void loop(void) {
  // Unity tests run once; leave loop empty.
}
