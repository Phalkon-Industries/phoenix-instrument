#include "led_router.hpp"
#include "main.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <stddef.h>
#include <unity.h>

static const LedRouterConfig k_router_config = {
    TS5A3359_IN1,
    TS5A3359_IN2,
};

static void reset_pin_modes_to_input(void) {
  pinMode(k_router_config.switch_in1_pin, INPUT);
  pinMode(k_router_config.switch_in2_pin, INPUT);
}

void setUp(void) {
  led_router_reset_for_test();
  reset_pin_modes_to_input();
}

void tearDown(void) {
  reset_pin_modes_to_input();
}

static void test_led_router_initialize_requires_configuration(void) {
  int return_code = led_router_initialize(NULL);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_ARG, return_code);
}

static void test_led_router_initialize_rejects_duplicate_pins(void) {
  const LedRouterConfig invalid_config = {
      TS5A3359_IN1,
      TS5A3359_IN1,
  };

  const int return_code = led_router_initialize(&invalid_config);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_ARG, return_code);
}

static void test_led_router_set_state_requires_initialization(void) {
  int return_code = led_router_set_state(LedRouterState::LED_ROUTER_STATE_LED1);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_NOT_INITIALIZED, return_code);
}

static void test_led_router_initialize_programs_pins_and_state(void) {
  int return_code = led_router_initialize(&k_router_config);
  TEST_ASSERT_EQUAL_INT_MESSAGE(LED_ROUTER_OK, return_code, "initialize should report success");
  TEST_ASSERT_EQUAL(HIGH, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(HIGH, digitalRead(k_router_config.switch_in2_pin));

  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  return_code                   = led_router_get_state(&observed_state);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, return_code);
  TEST_ASSERT_EQUAL_INT((int) LedRouterState::LED_ROUTER_STATE_DRAIN, (int) observed_state);
}

static void test_led_router_set_state_rejects_invalid_state(void) {
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_router_config));

  const LedRouterState invalid_state = static_cast<LedRouterState>(0xFFu);
  int                  return_code   = led_router_set_state(invalid_state);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_STATE, return_code);
}

static void test_led_router_set_state_updates_outputs(void) {
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_router_config));

  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_set_state(LedRouterState::LED_ROUTER_STATE_LED1));
  TEST_ASSERT_EQUAL(HIGH, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in2_pin));

  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_set_state(LedRouterState::LED_ROUTER_STATE_LED2));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(HIGH, digitalRead(k_router_config.switch_in2_pin));

  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_set_state(LedRouterState::LED_ROUTER_STATE_OFF));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in2_pin));
}

static void test_led_router_shutdown_requires_initialization(void) {
  int return_code = led_router_shutdown();
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_NOT_INITIALIZED, return_code);
}

static void test_led_router_shutdown_parks_outputs_off(void) {
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_router_config));
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_set_state(LedRouterState::LED_ROUTER_STATE_LED1));

  int return_code = led_router_shutdown();
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, return_code);
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in1_pin));
  TEST_ASSERT_EQUAL(LOW, digitalRead(k_router_config.switch_in2_pin));
}

static void test_led_router_get_state_rejects_null_pointer(void) {
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_ARG, led_router_get_state(NULL));
}

static void test_led_router_get_state_rejects_null_pointer_after_init(void) {
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_OK, led_router_initialize(&k_router_config));
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_INVALID_ARG, led_router_get_state(NULL));
}

static void test_led_router_get_state_requires_initialization(void) {
  LedRouterState observed_state = LedRouterState::LED_ROUTER_STATE_OFF;
  const int      return_code    = led_router_get_state(&observed_state);
  TEST_ASSERT_EQUAL_INT(LED_ROUTER_ERR_NOT_INITIALIZED, return_code);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
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
  UNITY_END();
}

void loop() {
}
