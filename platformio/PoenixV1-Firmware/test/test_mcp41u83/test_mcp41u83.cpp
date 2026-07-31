#include "device_setup.hpp"
#include "mcp41u83.hpp"
#include "power_control.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <unity.h>

void setUp(void) {
  mcp41u83_deinitialize();
}

void tearDown(void) {
  mcp41u83_deinitialize();
}

static void test_mcp41u83_initialize_rejects_null_wire(void) {
  int result = mcp41u83_initialize(0x2Cu, NULL);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_INVALID_ARG, result);
  TEST_ASSERT_FALSE(mcp41u83_is_initialized());
}

static void test_mcp41u83_initialize_rejects_invalid_address(void) {
  int result = mcp41u83_initialize(0x20u, &Wire);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_INVALID_ARG, result);
  TEST_ASSERT_FALSE(mcp41u83_is_initialized());
}

static void test_mcp41u83_initialize_accepts_valid_inputs(void) {
  int result = mcp41u83_initialize(0x2Cu, &Wire);
  TEST_ASSERT_EQUAL_INT(MCP41U83_OK, result);
  TEST_ASSERT_TRUE(mcp41u83_is_initialized());
}

static void test_mcp41u83_set_wiper_rejects_when_not_initialized(void) {
  int result = mcp41u83_set_wiper(512u);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_NOT_INITIALIZED, result);
}

static void test_mcp41u83_set_wiper_rejects_out_of_range_code(void) {
  TEST_ASSERT_EQUAL_INT(MCP41U83_OK, mcp41u83_initialize(0x2Cu, &Wire));

  int result = mcp41u83_set_wiper(1024u);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_INVALID_ARG, result);
}

static void test_mcp41u83_build_write_frame_rejects_null_outputs(void) {
  uint8_t cmd     = 0u;
  uint8_t data_hi = 0u;
  uint8_t data_lo = 0u;

  int result = mcp41u83_build_write_frame(512u, NULL, &data_hi, &data_lo);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_INVALID_ARG, result);

  result = mcp41u83_build_write_frame(512u, &cmd, NULL, &data_lo);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_INVALID_ARG, result);

  result = mcp41u83_build_write_frame(512u, &cmd, &data_hi, NULL);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_INVALID_ARG, result);
}

static void test_mcp41u83_build_write_frame_rejects_out_of_range_code(void) {
  uint8_t cmd     = 0u;
  uint8_t data_hi = 0u;
  uint8_t data_lo = 0u;

  int result = mcp41u83_build_write_frame(1024u, &cmd, &data_hi, &data_lo);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_INVALID_ARG, result);
}

static void test_mcp41u83_build_write_frame_encodes_10bit_code(void) {
  uint8_t cmd     = 0xFFu;
  uint8_t data_hi = 0xFFu;
  uint8_t data_lo = 0xFFu;

  // Test code 0x2AB (683 decimal) - should encode as:
  // cmd = 0x08, data_hi = 0x02, data_lo = 0xAB
  int result = mcp41u83_build_write_frame(0x2ABu, &cmd, &data_hi, &data_lo);
  TEST_ASSERT_EQUAL_INT(MCP41U83_OK, result);
  TEST_ASSERT_EQUAL_UINT8(0x08u, cmd);
  TEST_ASSERT_EQUAL_UINT8(0x02u, data_hi);
  TEST_ASSERT_EQUAL_UINT8(0xABu, data_lo);

  // Test code 0x000 (0 decimal) - should encode as:
  // cmd = 0x08, data_hi = 0x00, data_lo = 0x00
  result = mcp41u83_build_write_frame(0x000u, &cmd, &data_hi, &data_lo);
  TEST_ASSERT_EQUAL_INT(MCP41U83_OK, result);
  TEST_ASSERT_EQUAL_UINT8(0x08u, cmd);
  TEST_ASSERT_EQUAL_UINT8(0x00u, data_hi);
  TEST_ASSERT_EQUAL_UINT8(0x00u, data_lo);

  // Test code 0x3FF (1023 decimal) - should encode as:
  // cmd = 0x08, data_hi = 0x03, data_lo = 0xFF
  result = mcp41u83_build_write_frame(0x3FFu, &cmd, &data_hi, &data_lo);
  TEST_ASSERT_EQUAL_INT(MCP41U83_OK, result);
  TEST_ASSERT_EQUAL_UINT8(0x08u, cmd);
  TEST_ASSERT_EQUAL_UINT8(0x03u, data_hi);
  TEST_ASSERT_EQUAL_UINT8(0xFFu, data_lo);
}

static void test_mcp41u83_read_wiper_rejects_null_output(void) {
  TEST_ASSERT_EQUAL_INT(MCP41U83_OK, mcp41u83_initialize(0x2Cu, &Wire));

  int result = mcp41u83_read_wiper(NULL);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_INVALID_ARG, result);
}

static void test_mcp41u83_read_wiper_rejects_when_not_initialized(void) {
  uint16_t code   = 1022u;
  int      result = mcp41u83_read_wiper(&code);
  TEST_ASSERT_EQUAL_INT(MCP41U83_ERR_NOT_INITIALIZED, result);
}

static void test_mcp41u83_set_then_read_wiper_round_trips(void) {
  // This test requires actual hardware - skip if not on device
  TEST_ASSERT_EQUAL_INT(MCP41U83_OK, mcp41u83_initialize(0x2Cu, &Wire));

  // Test a few representative values
  const uint16_t test_codes[] = {256u, 512u, 768u, 1023u, 0u};
  const size_t   num_tests    = sizeof(test_codes) / sizeof(test_codes[0]);

  for (size_t i = 0u; i < num_tests; ++i) {
    const uint16_t code = test_codes[i];

    int result = mcp41u83_set_wiper(code);
    TEST_ASSERT_EQUAL_INT_MESSAGE(MCP41U83_OK, result, "set_wiper failed");

    uint16_t readback = 0xFFFFu;
    result            = mcp41u83_read_wiper(&readback);
    TEST_ASSERT_EQUAL_INT_MESSAGE(MCP41U83_OK, result, "read_wiper failed");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(code, readback, "readback mismatch");
  }
}

int runUnityTests(void) {
  UNITY_BEGIN();
  power_control_prepare_power_domains(&g_device_power_control_config);
  Wire.begin();
  RUN_TEST(test_mcp41u83_initialize_rejects_null_wire);
  RUN_TEST(test_mcp41u83_initialize_rejects_invalid_address);
  RUN_TEST(test_mcp41u83_initialize_accepts_valid_inputs);
  RUN_TEST(test_mcp41u83_set_wiper_rejects_when_not_initialized);
  RUN_TEST(test_mcp41u83_set_wiper_rejects_out_of_range_code);
  RUN_TEST(test_mcp41u83_build_write_frame_rejects_null_outputs);
  RUN_TEST(test_mcp41u83_build_write_frame_rejects_out_of_range_code);
  RUN_TEST(test_mcp41u83_build_write_frame_encodes_10bit_code);
  RUN_TEST(test_mcp41u83_read_wiper_rejects_null_output);
  RUN_TEST(test_mcp41u83_read_wiper_rejects_when_not_initialized);
  RUN_TEST(test_mcp41u83_set_then_read_wiper_round_trips);
  return UNITY_END();
}

void setup(void) {
  UNITY_SETUP_SERIAL_DEFAULT();
  delay(200);
  runUnityTests();
}

void loop(void) {
}
