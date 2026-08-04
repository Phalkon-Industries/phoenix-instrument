#include "device_setup.hpp"
#include "power_control.hpp"
#include <Arduino.h>
#include <unity.h>

// Assemble the board-specific power control configuration used throughout the suite.
static PowerControlConfig build_power_control_config(void) {
  PowerControlConfig config = {};
  // Step 1: Bind each dependency to the board-specific configuration.
  config.power_enable_pin      = PIN_ENABLE_5V_POWER;
  config.neg_bias_shutdown_pin = PIN_NEG_BIAS_SHUTDOWN;
#if defined(LED_RED)
  config.indicator_red_pin = LED_RED;
#else
  config.indicator_red_pin = -1;
#endif
#if defined(LED_BLUE)
  config.indicator_blue_pin = LED_BLUE;
#else
  config.indicator_blue_pin = -1;
#endif
  return config;
}

// Helper that asserts the power domains are online before validating hardware state.
static void bring_power_domains_online(void) {
  PowerControlConfig config = build_power_control_config();
  // Step 1: Initialise the helper so subsequent tests run against live hardware.
  TEST_ASSERT_EQUAL_INT(POWER_CONTROL_OK, power_control_prepare_power_domains(&config));
}

void setUp(void) {
  // Step 1: Reset the power control helper so each test observes cold-start behaviour.
  power_control_reset_for_test();
}

void tearDown(void) {
  // Step 1: Clear cached state to prevent cross-test leakage.
  power_control_reset_for_test();
}

static void test_power_control_rejects_null_config(void) {
  // Step 1: Validate guard behaviour when callers omit the configuration struct.
  TEST_ASSERT_EQUAL_INT(POWER_CONTROL_ERR_INVALID_ARG, power_control_prepare_power_domains(NULL));
}

static void test_power_control_asserts_power_domains(void) {
  // Step 1: Bring the power domains online so the resulting GPIO state can be inspected.
  // power_control is only responsible for the 5V rail, the LM7705 negative-bias generator,
  // and the indicator LEDs; peripheral drivers are initialised elsewhere by device_setup.
  bring_power_domains_online();

  // Step 2: Verify board-level GPIOs settle into the expected levels.
  if (PIN_ENABLE_5V_POWER >= 0) {
    TEST_ASSERT_EQUAL_MESSAGE(HIGH, digitalRead(PIN_ENABLE_5V_POWER), "Expected power enable pin to assert HIGH");
  }
  if (PIN_NEG_BIAS_SHUTDOWN >= 0) {
    TEST_ASSERT_EQUAL_MESSAGE(LOW, digitalRead(PIN_NEG_BIAS_SHUTDOWN),
                              "Expected negative bias shutdown pin to assert LOW");
  }

#if defined(LED_RED)
  TEST_ASSERT_EQUAL_MESSAGE(LOW, digitalRead(LED_RED), "Expected red indicator pin to assert LOW");
#endif
#if defined(LED_BLUE)
  TEST_ASSERT_EQUAL_MESSAGE(LOW, digitalRead(LED_BLUE), "Expected blue indicator pin to assert LOW");
#endif
}

static void test_power_control_requires_initialization_for_low_power(void) {
  // Step 1: Low-power transitions should be rejected until prepare_power_domains succeeds.
  TEST_ASSERT_EQUAL_INT(POWER_CONTROL_ERR_NOT_INITIALIZED, power_control_enter_low_power());
}

static void test_power_control_enters_low_power(void) {
  // Step 1: Prepare the power domains before attempting the transition.
  bring_power_domains_online();
  // Step 2: Ask the helper to enter low power and verify the rail drops.
  TEST_ASSERT_EQUAL_INT(POWER_CONTROL_OK, power_control_enter_low_power());
  if (PIN_ENABLE_5V_POWER >= 0) {
    TEST_ASSERT_EQUAL_MESSAGE(LOW, digitalRead(PIN_ENABLE_5V_POWER), "Expected power enable pin to drive LOW");
  }
  if (PIN_NEG_BIAS_SHUTDOWN >= 0) {
    TEST_ASSERT_EQUAL_MESSAGE(HIGH, digitalRead(PIN_NEG_BIAS_SHUTDOWN),
                              "Expected low-power to drive the negative bias shutdown pin HIGH");
  }
}

static void test_power_control_shutdown_disables_power(void) {
  // Step 1: Start from a powered state so shutdown can release resources.
  bring_power_domains_online();
  // Step 2: Request shutdown and confirm the enable pin deasserts.
  TEST_ASSERT_EQUAL_INT(POWER_CONTROL_OK, power_control_shutdown());
  if (PIN_ENABLE_5V_POWER >= 0) {
    TEST_ASSERT_EQUAL_MESSAGE(LOW, digitalRead(PIN_ENABLE_5V_POWER), "Expected shutdown to drive LOW on power enable");
  }
  if (PIN_NEG_BIAS_SHUTDOWN >= 0) {
    TEST_ASSERT_EQUAL_MESSAGE(HIGH, digitalRead(PIN_NEG_BIAS_SHUTDOWN),
                              "Expected shutdown to drive the negative bias shutdown pin HIGH");
  }
  // Step 3: Subsequent low-power calls should fail until reinitialised.
  TEST_ASSERT_EQUAL_INT(POWER_CONTROL_ERR_NOT_INITIALIZED, power_control_enter_low_power());
}

static void test_led_power_is_ready_returns_false_before_initialization(void) {
  // Step 1: Verify the predicate returns false when the helper has not been initialised.
  TEST_ASSERT_FALSE_MESSAGE(power_control_led_power_is_ready(),
                            "Expected led_power_is_ready to return false before initialization");
}

static void test_led_power_is_ready_returns_true_after_bring_up(void) {
  // Step 1: Bring the power domains online so the predicate can inspect live state.
  bring_power_domains_online();

  // Step 2: Confirm the predicate reports both rails are ready.
  TEST_ASSERT_TRUE_MESSAGE(power_control_led_power_is_ready(),
                           "Expected led_power_is_ready to return true after bring-up");
}

static void test_led_power_is_ready_returns_false_after_low_power(void) {
  // Step 1: Bring up and then enter low power so the predicate reflects the de-energised state.
  bring_power_domains_online();
  TEST_ASSERT_EQUAL_INT(POWER_CONTROL_OK, power_control_enter_low_power());

  // Step 2: Confirm the predicate returns false when the rails are dropped.
  TEST_ASSERT_FALSE_MESSAGE(power_control_led_power_is_ready(),
                            "Expected led_power_is_ready to return false after entering low power");
}

static void test_enter_low_power_disables_lm7705_and_5v(void) {
  // Step 1: Bring the power domains online so the low-power transition can be observed.
  bring_power_domains_online();

  // Step 2: Enter low power and verify both rails are de-energised.
  TEST_ASSERT_EQUAL_INT(POWER_CONTROL_OK, power_control_enter_low_power());
  if (PIN_ENABLE_5V_POWER >= 0) {
    TEST_ASSERT_EQUAL_MESSAGE(LOW, digitalRead(PIN_ENABLE_5V_POWER),
                              "Expected 5V power enable pin to drive LOW in low-power state");
  }
  if (PIN_NEG_BIAS_SHUTDOWN >= 0) {
    TEST_ASSERT_EQUAL_MESSAGE(HIGH, digitalRead(PIN_NEG_BIAS_SHUTDOWN),
                              "Expected LM7705 shutdown pin to drive HIGH (generator off) in low-power state");
  }
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  UNITY_BEGIN();
  RUN_TEST(test_power_control_rejects_null_config);
  RUN_TEST(test_power_control_asserts_power_domains);
  RUN_TEST(test_power_control_requires_initialization_for_low_power);
  RUN_TEST(test_power_control_enters_low_power);
  RUN_TEST(test_power_control_shutdown_disables_power);
  RUN_TEST(test_led_power_is_ready_returns_false_before_initialization);
  RUN_TEST(test_led_power_is_ready_returns_true_after_bring_up);
  RUN_TEST(test_led_power_is_ready_returns_false_after_low_power);
  RUN_TEST(test_enter_low_power_disables_lm7705_and_5v);
  UNITY_END();
}

void loop() {
}
