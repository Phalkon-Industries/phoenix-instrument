#include "ad524x.hpp"
#include "main.hpp"
#include "unity_config.h"
#include <Wire.h>
#include <unity.h>

void setUp(void) {
}

void tearDown(void) {
}

// The application is expected to call Wire.begin() once during bring-up. These
// tests only exercise the argument validation path, so we rely on the default
// global Wire instance without touching the bus.

static void test_ad524x_initialize_rejects_null_wire_handle(void) {
  int result = ad524x_initialize(0x2Cu, NULL);
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, result);
}

static void test_ad524x_initialize_rejects_out_of_range_address(void) {
  int result = ad524x_initialize(0x20u, &Wire);
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, result);
}

static void test_ad524x_initialize_accepts_valid_inputs(void) {
  int result = ad524x_initialize(AD5242_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(AD524X_OK, result);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  UNITY_BEGIN();
  RUN_TEST(test_ad524x_initialize_rejects_null_wire_handle);
  RUN_TEST(test_ad524x_initialize_rejects_out_of_range_address);
  RUN_TEST(test_ad524x_initialize_accepts_valid_inputs);
  UNITY_END();
}

void loop() {
}
