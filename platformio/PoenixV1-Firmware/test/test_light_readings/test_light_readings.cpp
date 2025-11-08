#include "ad524x.hpp"
#include "adc_hal.hpp"
#include "device_setup.hpp"
#include "led_router.hpp"
#include "light_readings.hpp"
#include "power_control.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <stddef.h>
#include <stdint.h>
#include <unity.h>

static LightReadingsSweepSample g_test_sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT];

int power_control_prepare_power_domains_for_test(void) {
  PowerControlConfig power_config = {};
  power_config.led_router_config  = &g_device_led_router_config;
  power_config.adc_config         = &g_device_adc_hal_config;
  power_config.wire_bus           = &Wire;
  power_config.digipot_address    = AD5242_I2C_ADDRESS;
  power_config.power_enable_pin   = PIN_ENABLE_POWER;
#if defined(LED_RED)
  power_config.indicator_red_pin = LED_RED;
#else
  power_config.indicator_red_pin = -1;
#endif
#if defined(LED_BLUE)
  power_config.indicator_blue_pin = LED_BLUE;
#else
  power_config.indicator_blue_pin = -1;
#endif

  return power_control_prepare_power_domains(&power_config);
}

static void bring_light_readings_online(void) {
  TEST_ASSERT_EQUAL_INT(POWER_CONTROL_OK, power_control_prepare_power_domains_for_test());
  // Step 1: Initialise the light readings helper under test.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_initialize(&g_device_light_readings_config));
}

void setUp(void) {
  // Step 1: Reset state so each test exercises a clean instance of every helper.
  power_control_reset_for_test();
  light_readings_reset_for_test();
  led_router_reset_for_test();
  adc_hal_reset_for_test();
  ad524x_deinitialize();
}

void tearDown(void) {
  // Step 1: Clear runtime state after each test to avoid cross-test leakage.
  power_control_reset_for_test();
  light_readings_reset_for_test();
  light_readings_force_saturation_for_test(false);
  led_router_reset_for_test();
  adc_hal_reset_for_test();
  ad524x_deinitialize();
}

static void test_light_readings_initialize_rejects_null_config(void) {
  // Step 1: Pass a null configuration pointer and expect the guard to fire.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_initialize(NULL));
}

static void test_light_readings_initialize_parks_router_in_drain_state(void) {
  Wire.begin();
  // Step 1: Bring the LED router online so the helper can command states.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&g_device_led_router_config));
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));
  // Step 2: Initialise the light readings helper using the canonical configuration.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_initialize(&g_device_light_readings_config));

  // Step 3: Confirm the helper parks the router in the configured drain state.
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_get_state(&observed_state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(g_device_light_readings_config.drain_state), static_cast<int>(observed_state));

  // Step 4: Verify wiper codes were applied for both photodiodes.
  uint8_t blue_wiper = 0u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(0u, &blue_wiper));
  TEST_ASSERT_EQUAL_UINT8(g_device_light_readings_config.blue_channel.wiper_code, blue_wiper);

  uint8_t green_wiper = 0u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(1u, &green_wiper));
  TEST_ASSERT_EQUAL_UINT8(g_device_light_readings_config.green_channel.wiper_code, green_wiper);
}

static void test_light_readings_sweep_requires_initialization(void) {
  // Step 1: Attempt a sweep without initialisation and expect an error.
  LightReadingsSweepSample sweep = {0};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_sweep(&sweep));
}

static void test_light_readings_reports_saturation_metadata(void) {
  // Step 1: Bring the helper online so we can execute a sweep sequence.
  bring_light_readings_online();

  LightReadingsSweepCollection collection = {
      .sweep_count = 0u,
      .sweeps      = g_test_sweep_storage,
  };

  light_readings_force_saturation_for_test(true);

  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep_n(1u, &collection));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, collection.sweep_count, "Expected sweep_n to populate one sweep entry");
  TEST_ASSERT_TRUE_MESSAGE(light_readings_last_sweep_detected_saturation(),
                           "Expected saturation metadata to flag the forced condition");
}

static void test_light_readings_sweep_populates_all_fields(void) {
  // Step 1: Bring the helper online so sweep operations can execute.
  bring_light_readings_online();

  // Step 2: Populate the sweep structure with sentinels to confirm the helper writes every field.
  LightReadingsSweepSample sweep = {
      .drain_blue_code  = INT32_MAX,
      .drain_green_code = INT32_MAX,
      .blue_code        = INT32_MAX,
      .green_code       = INT32_MAX,
  };

  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep(&sweep));

  TEST_ASSERT_NOT_EQUAL(INT32_MAX, sweep.drain_blue_code);
  TEST_ASSERT_NOT_EQUAL(INT32_MAX, sweep.drain_green_code);
  TEST_ASSERT_NOT_EQUAL(INT32_MAX, sweep.blue_code);
  TEST_ASSERT_NOT_EQUAL(INT32_MAX, sweep.green_code);

  // Step 3: Confirm the router finishes in the configured drain state.
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_get_state(&observed_state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(g_device_light_readings_config.drain_state), static_cast<int>(observed_state));
}

static void test_light_readings_sweep_returns_reasonable_codes(void) {
  // Step 1: Bring the helper online to capture a real sweep sample from the bench hardware.
  bring_light_readings_online();

  LightReadingsSweepSample sweep = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep(&sweep));

  // Step 2: Verify each captured code clears the minimum level observed on reference hardware.
  const int32_t minimum_expected_code = 10;
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(minimum_expected_code, sweep.drain_blue_code,
                                       "Drain blue reading fell below the calibrated threshold");
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(minimum_expected_code, sweep.drain_green_code,
                                       "Drain green reading fell below the calibrated threshold");
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(sweep.drain_blue_code, sweep.blue_code,
                                       "Blue channel reading fell below the drain value");
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(sweep.drain_green_code, sweep.green_code,
                                       "Green channel reading fell below the drain value");
}

static void test_light_readings_sweep_n_requires_initialization(void) {
  // Step 1: Attempt to sweep multiple times without initialisation.
  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_sweep_n(1u, &collection));
}

static void test_light_readings_sweep_n_rejects_null_collection(void) {
  // Step 1: Bring the helper online to exercise argument validation.
  bring_light_readings_online();

  // Step 2: Confirm passing a null buffer results in an invalid-argument error.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_sweep_n(1u, NULL));
}

static void test_light_readings_sweep_n_rejects_excess_count(void) {
  // Step 1: Bring the helper online so the capacity guard can be evaluated.
  bring_light_readings_online();

  // Step 2: Request more sweeps than the fixed-capacity buffer allows.
  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;
  const uint32_t requested_count          = (uint32_t) LIGHT_READINGS_MAX_SWEEP_COUNT + 1u;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED,
                        light_readings_sweep_n(requested_count, &collection));
}

static void test_light_readings_sweep_n_populates_requested_sweeps(void) {
  // Step 1: Bring the helper online so repeated sweeps can execute.
  bring_light_readings_online();

  // Step 2: Seed the collection with sentinels to confirm every entry is updated.
  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;
  for (size_t index = 0; index < LIGHT_READINGS_MAX_SWEEP_COUNT; ++index) {
    collection.sweeps[index].drain_blue_code  = INT32_MAX;
    collection.sweeps[index].drain_green_code = INT32_MAX;
    collection.sweeps[index].blue_code        = INT32_MAX;
    collection.sweeps[index].green_code       = INT32_MAX;
  }
  collection.sweep_count = 0xFFFFFFFFu;

  const uint32_t requested_count = 2u;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep_n(requested_count, &collection));
  TEST_ASSERT_EQUAL_UINT32(requested_count, collection.sweep_count);

  for (uint32_t index = 0u; index < requested_count; ++index) {
    TEST_ASSERT_NOT_EQUAL(INT32_MAX, collection.sweeps[index].drain_blue_code);
    TEST_ASSERT_NOT_EQUAL(INT32_MAX, collection.sweeps[index].drain_green_code);
    TEST_ASSERT_NOT_EQUAL(INT32_MAX, collection.sweeps[index].blue_code);
    TEST_ASSERT_NOT_EQUAL(INT32_MAX, collection.sweeps[index].green_code);
  }

  // Step 3: Ensure the router finishes a multi-sweep run back in the drain state.
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_get_state(&observed_state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(g_device_light_readings_config.drain_state), static_cast<int>(observed_state));
}

static void test_light_readings_sweep_n_handles_max_capacity(void) {
  // Step 1: Bring the helper online so a full-capacity sweep can execute.
  bring_light_readings_online();

  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;

  // Step 2: Perform the maximum number of sweeps supported by the module.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep_n(LIGHT_READINGS_MAX_SWEEP_COUNT, &collection));
  TEST_ASSERT_EQUAL_UINT32(LIGHT_READINGS_MAX_SWEEP_COUNT, collection.sweep_count);

  // Step 3: Spot-check a subset of entries to confirm data was populated.
  TEST_ASSERT_NOT_EQUAL(0, collection.sweeps[0].drain_blue_code);
  TEST_ASSERT_NOT_EQUAL(0, collection.sweeps[LIGHT_READINGS_MAX_SWEEP_COUNT - 1u].green_code);
}

static void test_light_readings_compute_sweep_stats_requires_arguments(void) {
  // Step 1: Verify the helper guards against null parameters.
  LightReadingsSweepStats stats = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_compute_sweep_stats(NULL, &stats));

  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_compute_sweep_stats(&collection, NULL));
}

static void test_light_readings_compute_sweep_stats_handles_empty_collection(void) {
  // Step 1: Seed an empty collection to emulate a caller that captured no sweeps.
  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;
  collection.sweep_count                  = 0u;

  LightReadingsSweepStats stats = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_compute_sweep_stats(&collection, &stats));

  TEST_ASSERT_EQUAL_UINT32(0u, stats.sweep_count);
  TEST_ASSERT_FALSE(stats.drain_blue.has_samples);
  TEST_ASSERT_FALSE(stats.drain_green.has_samples);
  TEST_ASSERT_FALSE(stats.blue.has_samples);
  TEST_ASSERT_FALSE(stats.green.has_samples);
  TEST_ASSERT_EQUAL_UINT32(0u, stats.drain_blue.sample_count);
  TEST_ASSERT_EQUAL_UINT32(0u, stats.drain_green.sample_count);
  TEST_ASSERT_EQUAL_UINT32(0u, stats.blue.sample_count);
  TEST_ASSERT_EQUAL_UINT32(0u, stats.green.sample_count);
  TEST_ASSERT_EQUAL_INT32(0, stats.drain_blue.min_value);
  TEST_ASSERT_EQUAL_INT32(0, stats.drain_blue.max_value);
  TEST_ASSERT_EQUAL_INT32(0, stats.blue.min_value);
  TEST_ASSERT_EQUAL_INT32(0, stats.blue.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.drain_blue.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.drain_blue.standard_deviation));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.drain_blue.drift_slope));
}

static void test_light_readings_compute_sweep_stats_calculates_metrics(void) {
  // Step 1: Populate a sweep collection with deterministic values for validation.
  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;
  collection.sweep_count                  = 3u;
  collection.sweeps[0]                    = {1000, 1500, 1100, 1200};
  collection.sweeps[1]                    = {2000, 2500, 2100, 2200};
  collection.sweeps[2]                    = {3000, 3500, 3100, 3200};

  LightReadingsSweepStats stats = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_compute_sweep_stats(&collection, &stats));

  TEST_ASSERT_EQUAL_UINT32(3u, stats.sweep_count);

  TEST_ASSERT_TRUE(stats.drain_blue.has_samples);
  TEST_ASSERT_EQUAL_UINT32(3u, stats.drain_blue.sample_count);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2000.0f, static_cast<float>(stats.drain_blue.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.drain_blue.standard_deviation));
  TEST_ASSERT_EQUAL_INT32(1000, stats.drain_blue.min_value);
  TEST_ASSERT_EQUAL_INT32(3000, stats.drain_blue.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.drain_blue.drift_slope));

  TEST_ASSERT_TRUE(stats.drain_green.has_samples);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2500.0f, static_cast<float>(stats.drain_green.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.drain_green.standard_deviation));
  TEST_ASSERT_EQUAL_INT32(1500, stats.drain_green.min_value);
  TEST_ASSERT_EQUAL_INT32(3500, stats.drain_green.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.drain_green.drift_slope));

  TEST_ASSERT_TRUE(stats.blue.has_samples);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2100.0f, static_cast<float>(stats.blue.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.blue.standard_deviation));
  TEST_ASSERT_EQUAL_INT32(1100, stats.blue.min_value);
  TEST_ASSERT_EQUAL_INT32(3100, stats.blue.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.blue.drift_slope));

  TEST_ASSERT_TRUE(stats.green.has_samples);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2200.0f, static_cast<float>(stats.green.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.green.standard_deviation));
  TEST_ASSERT_EQUAL_INT32(1200, stats.green.min_value);
  TEST_ASSERT_EQUAL_INT32(3200, stats.green.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.green.drift_slope));
}

static void test_light_readings_compute_sweep_stats_reports_zero_drift_for_flat_series(void) {
  // Step 1: Populate a sweep collection with identical samples to emulate a steady signal.
  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;
  collection.sweep_count                  = 4u;
  for (uint32_t index = 0u; index < collection.sweep_count; ++index) {
    collection.sweeps[index] = {1000, 1200, 1300, 1400};
  }

  LightReadingsSweepStats stats = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_compute_sweep_stats(&collection, &stats));

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.drain_blue.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.drain_green.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.blue.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.green.drift_slope));
}

static void test_light_readings_compute_sweep_stats_reports_positive_drift(void) {
  // Step 1: Create a sweep collection with values increasing steadily each iteration.
  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;
  collection.sweep_count                  = 5u;
  for (uint32_t index = 0u; index < collection.sweep_count; ++index) {
    const int32_t base_value = static_cast<int32_t>(index) * 100;
    collection.sweeps[index] = {
        1000 + base_value,
        2000 + base_value,
        3000 + base_value,
        4000 + base_value,
    };
  }

  LightReadingsSweepStats stats = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_compute_sweep_stats(&collection, &stats));

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 100.0f, static_cast<float>(stats.drain_blue.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 100.0f, static_cast<float>(stats.drain_green.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 100.0f, static_cast<float>(stats.blue.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 100.0f, static_cast<float>(stats.green.drift_slope));
}

static void test_light_readings_compute_sweep_stats_reports_negative_drift(void) {
  // Step 1: Capture a sweep collection with steadily decreasing values to emulate decay.
  LightReadingsSweepCollection collection = {};
  collection.sweeps                       = g_test_sweep_storage;
  collection.sweep_count                  = 3u;
  collection.sweeps[0]                    = {3000, 2800, 2600, 2400};
  collection.sweeps[1]                    = {2000, 1800, 1600, 1400};
  collection.sweeps[2]                    = {1000, 800, 600, 400};

  LightReadingsSweepStats stats = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_compute_sweep_stats(&collection, &stats));

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1000.0f, static_cast<float>(stats.drain_blue.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1000.0f, static_cast<float>(stats.drain_green.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1000.0f, static_cast<float>(stats.blue.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1000.0f, static_cast<float>(stats.green.drift_slope));
}

static void test_light_readings_modify_settings_requires_initialization(void) {
  // Step 1: Attempt to modify settings prior to initialisation and expect a guard failure.
  const LightReadingsRuntimeSettings overrides = {
      .apply_dwell_override = true,
      .dwell_us             = 250u,
      .apply_wiper_override = true,
      .wiper_code           = 0x42u,
  };

  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_modify_settings(&overrides));
}

static void test_light_readings_modify_settings_updates_runtime_configuration(void) {
  // Step 1: Bring the helper online so runtime overrides can be applied safely.
  bring_light_readings_online();

  const LightReadingsRuntimeSettings overrides = {
      .apply_dwell_override = true,
      .dwell_us             = 250u,
      .apply_wiper_override = true,
      .wiper_code           = 0x31u,
  };

  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_modify_settings(&overrides));

  // Step 2: Confirm the digi-pot wiper codes were updated for both photodiodes.
  uint8_t blue_wiper  = 0u;
  uint8_t green_wiper = 0u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(0u, &blue_wiper));
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(1u, &green_wiper));
  TEST_ASSERT_EQUAL_UINT8(overrides.wiper_code, blue_wiper);
  TEST_ASSERT_EQUAL_UINT8(overrides.wiper_code, green_wiper);

  // Step 3: Capture the cached configuration so dwell overrides can be validated.
  LightReadingsConfig config_snapshot = {};
  light_readings_get_config_for_test(&config_snapshot);
  TEST_ASSERT_EQUAL_UINT32(overrides.dwell_us, config_snapshot.blue_channel.dwell_us);
  TEST_ASSERT_EQUAL_UINT32(overrides.dwell_us, config_snapshot.green_channel.dwell_us);
}

static void test_light_readings_shutdown_requires_initialization(void) {
  // Step 1: Attempt to shut down before initialise and expect an error.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_shutdown());
}

static void test_light_readings_shutdown_clears_module_state(void) {
  // Step 1: Bring the helper online to exercise the shutdown logic.
  bring_light_readings_online();

  // Step 2: Shut down the helper and confirm success surfaces to the caller.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_shutdown());
}

static void test_light_readings_reset_for_test_clears_initialization(void) {
  // Step 1: Bring the helper online so we can confirm the reset hook clears state.
  bring_light_readings_online();

  // Step 2: Invoke the test hook and ensure future reads report the module as uninitialised.
  light_readings_reset_for_test();
  LightReadingsSweepSample sweep = {0};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_sweep(&sweep));
}

void setup() {
  // Step 1: Prepare the Unity serial transport shared across firmware tests.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2: Start Unity and register each light readings test case.
  UNITY_BEGIN();
  RUN_TEST(test_light_readings_initialize_rejects_null_config);
  RUN_TEST(test_light_readings_initialize_parks_router_in_drain_state);
  RUN_TEST(test_light_readings_sweep_requires_initialization);
  RUN_TEST(test_light_readings_reports_saturation_metadata);
  RUN_TEST(test_light_readings_sweep_populates_all_fields);
  RUN_TEST(test_light_readings_sweep_returns_reasonable_codes);
  RUN_TEST(test_light_readings_sweep_n_requires_initialization);
  RUN_TEST(test_light_readings_sweep_n_rejects_null_collection);
  RUN_TEST(test_light_readings_sweep_n_rejects_excess_count);
  RUN_TEST(test_light_readings_sweep_n_populates_requested_sweeps);
  RUN_TEST(test_light_readings_sweep_n_handles_max_capacity);
  RUN_TEST(test_light_readings_compute_sweep_stats_requires_arguments);
  RUN_TEST(test_light_readings_compute_sweep_stats_handles_empty_collection);
  RUN_TEST(test_light_readings_compute_sweep_stats_calculates_metrics);
  RUN_TEST(test_light_readings_compute_sweep_stats_reports_zero_drift_for_flat_series);
  RUN_TEST(test_light_readings_compute_sweep_stats_reports_positive_drift);
  RUN_TEST(test_light_readings_compute_sweep_stats_reports_negative_drift);
  RUN_TEST(test_light_readings_modify_settings_requires_initialization);
  RUN_TEST(test_light_readings_modify_settings_updates_runtime_configuration);
  RUN_TEST(test_light_readings_shutdown_requires_initialization);
  RUN_TEST(test_light_readings_shutdown_clears_module_state);
  RUN_TEST(test_light_readings_reset_for_test_clears_initialization);
  // Step 3: Finalise Unity so loop() can idle once tests complete.
  UNITY_END();
}

void loop() {
}
