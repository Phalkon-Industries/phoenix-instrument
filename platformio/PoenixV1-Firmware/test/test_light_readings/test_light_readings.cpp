#include "adc_hal.hpp"
#include "device_settings.hpp"
#include "led_router.hpp"
#include "light_readings.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <unity.h>

static void bring_light_readings_online(void) {
  // Step 1: Initialise the LED router used to steer photodiode channels.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_device_led_router_config));
  // Step 2: Initialise the ADC HAL so conversions can complete during tests.
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_device_adc_hal_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());
  // Step 3: Initialise the light readings helper under test.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_initialize(&k_device_light_readings_config));
}

void setUp(void) {
  // Step 1: Reset state so each test exercises a clean instance of every helper.
  light_readings_reset_for_test();
  led_router_reset_for_test();
  adc_hal_reset_for_test();
}

void tearDown(void) {
  // Step 1: Clear runtime state after each test to avoid cross-test leakage.
  light_readings_reset_for_test();
  led_router_reset_for_test();
  adc_hal_reset_for_test();
}

static void test_light_readings_initialize_rejects_null_config(void) {
  // Step 1: Pass a null configuration pointer and expect the guard to fire.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_initialize(NULL));
}

static void test_light_readings_initialize_parks_router_in_drain_state(void) {
  // Step 1: Bring the LED router online so the helper can command states.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_device_led_router_config));
  // Step 2: Initialise the light readings helper using the canonical configuration.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_initialize(&k_device_light_readings_config));

  // Step 3: Confirm the helper parks the router in the configured drain state.
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_get_state(&observed_state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(k_device_light_readings_config.drain_state), static_cast<int>(observed_state));
}

static void test_light_readings_read_channel_requires_initialization(void) {
  // Step 1: Attempt to read without prior initialisation and expect an error.
  int32_t sample_code = 0;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED,
                        light_readings_read_channel(LightReadingsChannel::LIGHT_READINGS_CHANNEL_A, &sample_code));
}

static void test_light_readings_read_channel_rejects_null_storage(void) {
  // Step 1: Bring the helper online to exercise argument validation.
  bring_light_readings_online();

  // Step 2: Request a reading using a null pointer and expect an invalid-argument error.
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG,
                        light_readings_read_channel(LightReadingsChannel::LIGHT_READINGS_CHANNEL_A, NULL));
}

static void test_light_readings_read_channel_routes_channel_a_and_parks_drain(void) {
  // Step 1: Initialise dependencies so the helper can perform conversions.
  bring_light_readings_online();

  // Step 2: Seed the sample with a sentinel that falls outside the 24-bit ADC range.
  int32_t sample_code = INT32_MAX;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK,
                        light_readings_read_channel(LightReadingsChannel::LIGHT_READINGS_CHANNEL_A, &sample_code));
  TEST_ASSERT_NOT_EQUAL(INT32_MAX, sample_code);

  // Step 3: Ensure the ADC HAL received the channel corresponding to photodiode A.
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(k_device_light_readings_config.channel_a_adc),
                          static_cast<uint8_t>(adc_hal_test_last_channel_requested()));

  // Step 4: Verify the helper returns the router to the drain state after sampling.
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_get_state(&observed_state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(k_device_light_readings_config.drain_state), static_cast<int>(observed_state));
}

static void test_light_readings_sweep_requires_initialization(void) {
  // Step 1: Attempt a sweep without initialisation and expect an error.
  LightReadingsSweepSample sweep = {0};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_sweep(&sweep));
}

static void test_light_readings_sweep_populates_all_fields(void) {
  // Step 1: Bring the helper online so sweep operations can execute.
  bring_light_readings_online();

  // Step 2: Populate the sweep structure with sentinels to confirm the helper writes every field.
  LightReadingsSweepSample sweep = {
      .drain_channel_a_code = INT32_MAX,
      .drain_channel_b_code = INT32_MAX,
      .channel_a_code       = INT32_MAX,
      .channel_b_code       = INT32_MAX,
  };

  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep(&sweep));

  TEST_ASSERT_NOT_EQUAL(INT32_MAX, sweep.drain_channel_a_code);
  TEST_ASSERT_NOT_EQUAL(INT32_MAX, sweep.drain_channel_b_code);
  TEST_ASSERT_NOT_EQUAL(INT32_MAX, sweep.channel_a_code);
  TEST_ASSERT_NOT_EQUAL(INT32_MAX, sweep.channel_b_code);

  // Step 3: Confirm the router finishes in the configured drain state.
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_get_state(&observed_state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(k_device_light_readings_config.drain_state), static_cast<int>(observed_state));
}

static void test_light_readings_sweep_n_requires_initialization(void) {
  // Step 1: Attempt to sweep multiple times without initialisation.
  LightReadingsSweepCollection collection = {0};
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
  LightReadingsSweepCollection collection      = {0};
  const uint32_t               requested_count = (uint32_t) LIGHT_READINGS_MAX_SWEEP_COUNT + 1u;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED,
                        light_readings_sweep_n(requested_count, &collection));
}

static void test_light_readings_sweep_n_populates_requested_sweeps(void) {
  // Step 1: Bring the helper online so repeated sweeps can execute.
  bring_light_readings_online();

  // Step 2: Seed the collection with sentinels to confirm every entry is updated.
  LightReadingsSweepCollection collection = {0};
  for (size_t index = 0; index < LIGHT_READINGS_MAX_SWEEP_COUNT; ++index) {
    collection.sweeps[index].drain_channel_a_code = INT32_MAX;
    collection.sweeps[index].drain_channel_b_code = INT32_MAX;
    collection.sweeps[index].channel_a_code       = INT32_MAX;
    collection.sweeps[index].channel_b_code       = INT32_MAX;
  }
  collection.sweep_count = 0xFFFFFFFFu;

  const uint32_t requested_count = 2u;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep_n(requested_count, &collection));
  TEST_ASSERT_EQUAL_UINT32(requested_count, collection.sweep_count);

  for (uint32_t index = 0u; index < requested_count; ++index) {
    TEST_ASSERT_NOT_EQUAL(INT32_MAX, collection.sweeps[index].drain_channel_a_code);
    TEST_ASSERT_NOT_EQUAL(INT32_MAX, collection.sweeps[index].drain_channel_b_code);
    TEST_ASSERT_NOT_EQUAL(INT32_MAX, collection.sweeps[index].channel_a_code);
    TEST_ASSERT_NOT_EQUAL(INT32_MAX, collection.sweeps[index].channel_b_code);
  }

  // Step 3: Ensure the router finishes a multi-sweep run back in the drain state.
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_get_state(&observed_state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(k_device_light_readings_config.drain_state), static_cast<int>(observed_state));
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

  // Step 3: Validate that subsequent reads fail because the helper is no longer initialised.
  int32_t sample_code = 0;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED,
                        light_readings_read_channel(LightReadingsChannel::LIGHT_READINGS_CHANNEL_A, &sample_code));
}

static void test_light_readings_reset_for_test_clears_initialization(void) {
  // Step 1: Bring the helper online so we can confirm the reset hook clears state.
  bring_light_readings_online();

  // Step 2: Invoke the test hook and ensure future reads report the module as uninitialised.
  light_readings_reset_for_test();
  int32_t sample_code = 0;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED,
                        light_readings_read_channel(LightReadingsChannel::LIGHT_READINGS_CHANNEL_A, &sample_code));
}

void setup() {
  // Step 1: Prepare the Unity serial transport shared across firmware tests.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2: Start Unity and register each light readings test case.
  UNITY_BEGIN();
  RUN_TEST(test_light_readings_initialize_rejects_null_config);
  RUN_TEST(test_light_readings_initialize_parks_router_in_drain_state);
  RUN_TEST(test_light_readings_read_channel_requires_initialization);
  RUN_TEST(test_light_readings_read_channel_rejects_null_storage);
  RUN_TEST(test_light_readings_read_channel_routes_channel_a_and_parks_drain);
  RUN_TEST(test_light_readings_sweep_requires_initialization);
  RUN_TEST(test_light_readings_sweep_populates_all_fields);
  RUN_TEST(test_light_readings_sweep_n_requires_initialization);
  RUN_TEST(test_light_readings_sweep_n_rejects_null_collection);
  RUN_TEST(test_light_readings_sweep_n_rejects_excess_count);
  RUN_TEST(test_light_readings_sweep_n_populates_requested_sweeps);
  RUN_TEST(test_light_readings_shutdown_requires_initialization);
  RUN_TEST(test_light_readings_shutdown_clears_module_state);
  RUN_TEST(test_light_readings_reset_for_test_clears_initialization);
  // Step 3: Finalise Unity so loop() can idle once tests complete.
  UNITY_END();
}

void loop() {
}
