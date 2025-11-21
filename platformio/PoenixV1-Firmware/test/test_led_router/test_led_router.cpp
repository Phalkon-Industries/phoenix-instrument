#include "led_router.hpp"
#include "main.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <nrf_pwm.h>
#include <stddef.h>
#include <string.h>
#include <unity.h>

static const LedRouterConfig k_router_config = {
    TS5A3359_IN1,
    TS5A3359_IN2,
    {false, nullptr},
};

static NRF_PWM_Type g_test_pwm_instance = {};

static const LedRouterConfig k_pwm_router_config = {
    TS5A3359_IN1,
    TS5A3359_IN2,
    {true, &g_test_pwm_instance},
};

static void reset_pin_modes_to_input(void) {
  // Step 1. Return both switch control lines to inputs between tests.
  pinMode(k_router_config.switch_in1_pin, INPUT);
  pinMode(k_router_config.switch_in2_pin, INPUT);
}

void setUp(void) {
  // Step 1. Reset the fake backend so prior state cannot leak in.
  led_router_reset_for_test();
  // Step 2. Restore the MCU pins to a neutral mode before exercising the driver.
  reset_pin_modes_to_input();
  // Step 3. Clear the fake PWM instance so register writes start from zero.
  memset(&g_test_pwm_instance, 0, sizeof(g_test_pwm_instance));
}

void tearDown(void) {
  // Step 1. Park the pins as inputs after each test to mimic board idle state.
  reset_pin_modes_to_input();
}

static void test_led_router_initialize_requires_configuration(void) {
  // Step 1. Call initialise with a null pointer to prove the guard triggers.
  int return_code = led_router_initialize(NULL);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_ARG, return_code);
}

static void test_led_router_initialize_rejects_duplicate_pins(void) {
  // Step 1. Craft an invalid configuration that reuses the same control pin.
  const LedRouterConfig invalid_config = {
      TS5A3359_IN1,
      TS5A3359_IN1,
      {false, nullptr},
  };

  // Step 2. Confirm the driver rejects the duplicate wiring request.
  const int return_code = led_router_initialize(&invalid_config);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_ARG, return_code);
}

static void test_led_router_set_state_requires_initialization(void) {
  // Step 1. Attempt to change the routing before calling initialise to validate the guard.
  int return_code = led_router_set_state(LedRouterState::LED_ROUTER_STATE_GREEN);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_NOT_INITIALIZED, return_code);
}

static void test_led_router_initialize_programs_pins_and_state(void) {
  // Step 1. Initialise using the known-good configuration and expect success.
  int return_code = led_router_initialize(&k_router_config);
  TEST_ASSERT_EQUAL_INT_MESSAGE(LED_ROUTER_OK, return_code, "initialize should report success");
  // Step 2. Verify both control pins default high to route current away from LEDs.
  TEST_ASSERT_EQUAL(HIGH, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(HIGH, digitalRead(k_router_config.switch_in2_pin));

  // Step 3. Read the cached state and ensure it reflects the default drain mode.
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  return_code                   = led_router_get_state(&observed_state);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, return_code);
  TEST_ASSERT_EQUAL_INT((int) LedRouterState::LED_ROUTER_STATE_DRAIN, (int) observed_state);
}

static void test_led_router_set_state_rejects_invalid_state(void) {
  // Step 1. Bring the driver online to exercise runtime validation.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_router_config));

  // Step 2. Cast an invalid enum value and ensure the API blocks it.
  const LedRouterState invalid_state = static_cast<LedRouterState>(0xFFu);
  int                  return_code   = led_router_set_state(invalid_state);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_STATE, return_code);
}

static void test_led_router_set_state_updates_outputs(void) {
  // Step 1. Initialise so state transitions can drive the hardware pins.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_router_config));

  // Step 2. Route LED1 and confirm the switch poles match the expectation.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_set_state(LedRouterState::LED_ROUTER_STATE_GREEN));
  TEST_ASSERT_EQUAL(HIGH, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in2_pin));

  // Step 3. Flip to LED2 and ensure the outputs swap accordingly.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_set_state(LedRouterState::LED_ROUTER_STATE_BLUE));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(HIGH, digitalRead(k_router_config.switch_in2_pin));

  // Step 4. Turn both LEDs off and verify the hardware releases both lines.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_set_state(LedRouterState::LED_ROUTER_STATE_OFF));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in2_pin));
}

static void test_led_router_shutdown_requires_initialization(void) {
  // Step 1. Attempt to shut down prior to initialise to confirm the guard fires.
  int return_code = led_router_shutdown();
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_NOT_INITIALIZED, return_code);
}

static void test_led_router_shutdown_parks_outputs_off(void) {
  // Step 1. Initialise and drive an active LED path.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_router_config));
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_set_state(LedRouterState::LED_ROUTER_STATE_GREEN));

  // Step 2. Shut down the driver and confirm both outputs are deasserted.
  int return_code = led_router_shutdown();
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, return_code);
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in2_pin));
}

static void test_led_router_get_state_rejects_null_pointer(void) {
  // Step 1. Request the state using a null pointer to confirm validation holds.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_ARG, led_router_get_state(NULL));
}

static void test_led_router_get_state_rejects_null_pointer_after_init(void) {
  // Step 1. Bring the driver online, then call with a null pointer to recheck validation.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_router_config));
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_ARG, led_router_get_state(NULL));
}

static void test_led_router_get_state_requires_initialization(void) {
  // Step 1. Attempt to read the cached state before initialise and expect failure.
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  const int      return_code    = led_router_get_state(&observed_state);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_NOT_INITIALIZED, return_code);
}

static void test_led_router_pwm_configure_requires_initialization(void) {
  // Step 1. Attempt to configure PWM without initialising the router.
  const int return_code = led_router_pwm_start(1000u);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_NOT_INITIALIZED, return_code);
}

static void test_led_router_pwm_configure_rejects_disabled_configuration(void) {
  // Step 1. Initialise with a configuration that leaves PWM disabled.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_router_config));

  // Step 2. Confirm PWM configuration rejects the request.
  const int return_code = led_router_pwm_start(1000u);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_ARG, return_code);
}

static void test_led_router_pwm_configure_programs_inverted_waveform(void) {
  // Step 1. Bring up the router with PWM enabled and a fake instance for inspection.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_pwm_router_config));

  // Step 2. Configure PWM with a minimum period and expect success.
  const uint32_t minimum_period_us = 3000u;
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_pwm_start(minimum_period_us));

  // Step 3. Capture diagnostic values and validate duty cycle ratios.
  LedRouterPwmTestSnapshot snapshot = {};
  led_router_get_pwm_test_snapshot(&snapshot);
  TEST_ASSERT_TRUE(snapshot.pwm_configured);
  TEST_ASSERT_GREATER_THAN_UINT16(0u, snapshot.countertop);
  TEST_ASSERT_EQUAL_UINT16(snapshot.countertop / 4u, snapshot.channel0_level);
  TEST_ASSERT_EQUAL_UINT16((snapshot.countertop * 3u) / 4u, snapshot.channel1_level);
  TEST_ASSERT_TRUE(snapshot.channel1_is_inverted);

  // Step 4. Ensure the programmed period honours the minimum request.
  const uint64_t required_ticks =
      ((static_cast<uint64_t>(snapshot.base_frequency_hz) * minimum_period_us) + 999999u) / 1000000u;
  TEST_ASSERT_TRUE(snapshot.countertop >= required_ticks);
}

static void test_led_router_pwm_stop_releases_pwm_resources(void) {
  // Step 1. Initialise and configure PWM so the stop API has work to do.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_pwm_router_config));
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_pwm_start(2000u));

  // Step 2. Request PWM stop and gather diagnostics.
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_pwm_stop());
  LedRouterPwmTestSnapshot snapshot = {};
  led_router_get_pwm_test_snapshot(&snapshot);

  // Step 3. Confirm PWM is no longer configured.
  TEST_ASSERT_FALSE(snapshot.pwm_configured);
  TEST_ASSERT_EQUAL_UINT16(0u, snapshot.countertop);
}

void setup() {
  // Step 1. Prepare the Unity serial interface for log output.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2. Start Unity and register each LED router test case.
  UNITY_BEGIN();
  RUN_TEST(test_led_router_initialize_requires_configuration);
  RUN_TEST(test_led_router_initialize_rejects_duplicate_pins);
  RUN_TEST(test_led_router_set_state_requires_initialization);
  RUN_TEST(test_led_router_initialize_programs_pins_and_state);
  RUN_TEST(test_led_router_set_state_rejects_invalid_state);
  RUN_TEST(test_led_router_set_state_updates_outputs);
  RUN_TEST(test_led_router_shutdown_requires_initialization);
  RUN_TEST(test_led_router_shutdown_parks_outputs_off);
  RUN_TEST(test_led_router_get_state_rejects_null_pointer);
  RUN_TEST(test_led_router_get_state_rejects_null_pointer_after_init);
  RUN_TEST(test_led_router_get_state_requires_initialization);
  RUN_TEST(test_led_router_pwm_configure_requires_initialization);
  RUN_TEST(test_led_router_pwm_configure_rejects_disabled_configuration);
  RUN_TEST(test_led_router_pwm_configure_programs_inverted_waveform);
  RUN_TEST(test_led_router_pwm_stop_releases_pwm_resources);
  // Step 3. Finalise Unity so the firmware can idle in loop().
  UNITY_END();
}

void loop() {
}
