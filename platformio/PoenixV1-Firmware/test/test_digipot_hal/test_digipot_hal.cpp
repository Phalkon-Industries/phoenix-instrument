#include "device_setup.hpp"
#include "digipot_hal.hpp"
#include <Wire.h>
#include <unity.h>

void setUp(void) {
  // Reset state before each test
  Wire.begin();
}

void tearDown(void) {
  // Clean up after each test
}

// Test: Blue wiper rejects out-of-range code (> 1023)
static void test_digipot_blue_set_wiper_rejects_out_of_range_code(void) {
  int result = digipot_blue_initialize(MCP41U83_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Try to set code beyond 10-bit range
  result = digipot_blue_set_wiper(1024u);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_ERR_INVALID_ARG, result);
}

// Test: Green wiper rejects out-of-range code (> 255)
static void test_digipot_green_set_wiper_rejects_out_of_range_code(void) {
  int result = digipot_green_initialize(AD5242_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Try to set code beyond 8-bit range
  result = digipot_green_set_wiper(256u);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_ERR_INVALID_ARG, result);
}

// Test: Blue wiper round-trip (write then read)
static void test_digipot_blue_round_trip(void) {
  int result = digipot_blue_initialize(MCP41U83_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Write a test value
  const uint16_t test_code = 0x2ABu;
  result                   = digipot_blue_set_wiper(test_code);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Read back the value
  uint16_t readback = 0u;
  result            = digipot_blue_read_wiper(&readback);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);
  TEST_ASSERT_EQUAL_UINT16(test_code, readback);
}

// Test: Green wiper round-trip (write then read)
static void test_digipot_green_round_trip(void) {
  int result = digipot_green_initialize(AD5242_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Write a test value
  const uint16_t test_code = 0x80u;
  result                   = digipot_green_set_wiper(test_code);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Read back the value
  uint16_t readback = 0u;
  result            = digipot_green_read_wiper(&readback);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);
  TEST_ASSERT_EQUAL_UINT16(test_code, readback);
}

// Test: Green shutdown works
static void test_digipot_green_shutdown_works(void) {
  int result = digipot_green_initialize(AD5242_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Enable shutdown
  result = digipot_green_shutdown(true);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Disable shutdown
  result = digipot_green_shutdown(false);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);
}

// Test: Both digipots can coexist on the same I2C bus
static void test_digipot_both_coexist(void) {
  // Initialize both
  int result = digipot_green_initialize(AD5242_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  result = digipot_blue_initialize(MCP41U83_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Write different values to each
  result = digipot_green_set_wiper(100u);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  result = digipot_blue_set_wiper(500u);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);

  // Read back and verify they're independent
  uint16_t green_readback = 0u;
  uint16_t blue_readback  = 0u;

  result = digipot_green_read_wiper(&green_readback);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);
  TEST_ASSERT_EQUAL_UINT16(100u, green_readback);

  result = digipot_blue_read_wiper(&blue_readback);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);
  TEST_ASSERT_EQUAL_UINT16(500u, blue_readback);
}

int runUnityTests(void) {
  UNITY_BEGIN();
  power_control_prepare_power_domains(&g_device_power_control_config);
  Wire.begin();
  RUN_TEST(test_digipot_blue_set_wiper_rejects_out_of_range_code);
  RUN_TEST(test_digipot_green_set_wiper_rejects_out_of_range_code);
  RUN_TEST(test_digipot_blue_round_trip);
  RUN_TEST(test_digipot_green_round_trip);
  RUN_TEST(test_digipot_green_shutdown_works);
  RUN_TEST(test_digipot_both_coexist);
  return UNITY_END();
}

void setup(void) {
  UNITY_SETUP_SERIAL_DEFAULT();
  Wire.begin();
  delay(200);
  runUnityTests();
}

void loop(void) {
}
