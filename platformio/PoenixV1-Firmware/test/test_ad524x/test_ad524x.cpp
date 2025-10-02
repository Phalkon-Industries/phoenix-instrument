#include "ad524x.hpp"
#include "main.hpp"
#include "unity_config.h"
#include <Wire.h>
#include <stddef.h>
#include <unity.h>

void setUp(void) {
  ad524x_deinitialize();
}

void tearDown(void) {
  if (ad524x_is_initialized()) {
    (void) ad524x_set_midscale(0u);
    (void) ad524x_shutdown(0u, false);
    (void) ad524x_set_midscale(1u);
    (void) ad524x_shutdown(1u, false);
    ad524x_deinitialize();
  }
}

// The application is expected to call Wire.begin() once during bring-up. Tests
// performing real transactions follow the same pattern by initialising the bus
// in the Arduino `setup()` routine before exercising the driver APIs.

static void test_ad524x_initialize_rejects_null_wire_handle(void) {
  int result = ad524x_initialize(0x2Cu, NULL);
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, result);
}

static void test_ad524x_initialize_rejects_out_of_range_address(void) {
  int result = ad524x_initialize(0x20u, &Wire);
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, result);
}

static void test_ad524x_is_initialized_reports_false_before_init(void) {
  TEST_ASSERT_FALSE(ad524x_is_initialized());
}

static void test_ad524x_build_instruction_rejects_invalid_inputs(void) {
  uint8_t instruction = 0xAAu;
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, ad524x_build_instruction(2u, false, false, &instruction));
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, ad524x_build_instruction(0u, false, false, NULL));
  // Ensure the buffer is left untouched on failure to help catch stale usage.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xAAu, instruction, "Instruction buffer should remain unchanged on error");
}

static void test_ad524x_write_frame_rejects_when_not_initialized(void) {
  int result = ad524x_write_frame(0x00u, 0x00u);
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_NOT_INITIALIZED, result);
}

static void test_ad524x_read_frame_rejects_null_buffer(void) {
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, ad524x_read_frame(0x00u, NULL));
}

static void test_ad524x_initialize_accepts_valid_inputs(void) {
  int result = ad524x_initialize(AD5242_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(AD524X_OK, result);
  TEST_ASSERT_TRUE(ad524x_is_initialized());
}

static void test_ad524x_build_instruction_sets_expected_bits(void) {
  uint8_t instruction = 0xFFu;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_build_instruction(0u, false, false, &instruction));
  TEST_ASSERT_EQUAL_UINT8(0x00u, instruction);

  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_build_instruction(1u, true, true, &instruction));
  TEST_ASSERT_EQUAL_UINT8(0xE0u, instruction);
}

static void test_ad524x_write_and_read_roundtrip_wiper_codes(void) {
  const uint8_t test_values[] = {0x00u, 0x80u, 0xFFu};

  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));

  for (uint8_t channel = 0u; channel < 2u; ++channel) {
    uint8_t instruction = 0u;
    TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_build_instruction(channel, false, false, &instruction));
    const uint8_t expected_instruction = (channel == 0u) ? 0x00u : 0x80u;
    TEST_ASSERT_EQUAL_UINT8(expected_instruction, instruction);

    for (size_t i = 0u; i < sizeof(test_values) / sizeof(test_values[0]); ++i) {
      const uint8_t value = test_values[i];
      TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_write_frame(instruction, value));
      delayMicroseconds(50);

      uint8_t readback = 0u;
      TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_read_frame(instruction, &readback));
      TEST_ASSERT_EQUAL_UINT8_MESSAGE(value, readback, "Wiper readback mismatch");
    }
  }
}

static void test_ad524x_set_wiper_rejects_when_not_initialized(void) {
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_NOT_INITIALIZED, ad524x_set_wiper(0u, 0x10u));
}

static void test_ad524x_set_wiper_rejects_invalid_channel(void) {
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, ad524x_set_wiper(2u, 0x10u));
}

static void test_ad524x_set_and_get_wiper_roundtrip(void) {
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));

  const uint8_t channel_values[2] = {0x12u, 0xABu};
  for (uint8_t channel = 0u; channel < 2u; ++channel) {
    TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_set_wiper(channel, channel_values[channel]));
  }

  for (uint8_t channel = 0u; channel < 2u; ++channel) {
    uint8_t readback = 0u;
    TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(channel, &readback));
    TEST_ASSERT_EQUAL_UINT8(channel_values[channel], readback);
  }
}

static void test_ad524x_get_wiper_rejects_null_pointer(void) {
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, ad524x_get_wiper(0u, NULL));
}

static void test_ad524x_set_midscale_positions_wiper(void) {
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_set_wiper(0u, 0x01u));
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_set_midscale(0u));

  uint8_t readback = 0u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(0u, &readback));
  TEST_ASSERT_EQUAL_UINT8(0x80u, readback);
}

static void test_ad524x_shutdown_preserves_wiper_code(void) {
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));
  const uint8_t target = 0x55u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_set_wiper(1u, target));

  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_shutdown(1u, true));

  uint8_t readback = 0u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(1u, &readback));
  TEST_ASSERT_EQUAL_UINT8(target, readback);

  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_shutdown(1u, false));
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(1u, &readback));
  TEST_ASSERT_EQUAL_UINT8(target, readback);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  Wire.begin();
  UNITY_BEGIN();
  RUN_TEST(test_ad524x_is_initialized_reports_false_before_init);
  RUN_TEST(test_ad524x_build_instruction_rejects_invalid_inputs);
  RUN_TEST(test_ad524x_read_frame_rejects_null_buffer);
  RUN_TEST(test_ad524x_write_frame_rejects_when_not_initialized);
  RUN_TEST(test_ad524x_initialize_rejects_null_wire_handle);
  RUN_TEST(test_ad524x_initialize_rejects_out_of_range_address);
  RUN_TEST(test_ad524x_initialize_accepts_valid_inputs);
  RUN_TEST(test_ad524x_build_instruction_sets_expected_bits);
  RUN_TEST(test_ad524x_write_and_read_roundtrip_wiper_codes);
  RUN_TEST(test_ad524x_set_wiper_rejects_when_not_initialized);
  RUN_TEST(test_ad524x_set_wiper_rejects_invalid_channel);
  RUN_TEST(test_ad524x_set_and_get_wiper_roundtrip);
  RUN_TEST(test_ad524x_get_wiper_rejects_null_pointer);
  RUN_TEST(test_ad524x_set_midscale_positions_wiper);
  RUN_TEST(test_ad524x_shutdown_preserves_wiper_code);
  UNITY_END();
}

void loop() {
}
