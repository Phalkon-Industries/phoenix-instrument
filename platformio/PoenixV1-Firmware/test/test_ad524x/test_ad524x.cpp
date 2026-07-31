#include "ad524x.hpp"
#include "device_setup.hpp"
#include "power_control.hpp"
#include "unity_config.h"
#include <Wire.h>
#include <stddef.h>
#include <unity.h>

void setUp(void) {
  // Step 1. Reset the driver so each test starts with a clean state.
  ad524x_deinitialize();
}

void tearDown(void) {
  if (ad524x_is_initialized()) {
    // Step 1. Return channel zero to midscale to avoid affecting later tests.
    (void) ad524x_set_midscale(0u);
    // Step 2. Leave channel zero powered for immediate reuse.
    (void) ad524x_shutdown(0u, false);
    // Step 3. Repeat the midscale positioning for channel one.
    (void) ad524x_set_midscale(1u);
    // Step 4. Ensure channel one remains active after cleanup.
    (void) ad524x_shutdown(1u, false);
    // Step 5. Fully deinitialise the driver so future tests must opt in.
    ad524x_deinitialize();
  }
}

// The application is expected to call Wire.begin() once during bring-up. Tests
// performing real transactions follow the same pattern by initialising the bus
// in the Arduino `setup()` routine before exercising the driver APIs.

static void test_ad524x_initialize_rejects_null_wire_handle(void) {
  // Step 1. Attempt to initialise with a null Wire pointer to validate guard logic.
  int result = ad524x_initialize(0x2Cu, NULL);
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, result);
}

static void test_ad524x_initialize_rejects_out_of_range_address(void) {
  // Step 1. Try an address outside the supported range to ensure validation holds.
  int result = ad524x_initialize(0x20u, &Wire);
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, result);
}

static void test_ad524x_is_initialized_reports_false_before_init(void) {
  TEST_ASSERT_FALSE(ad524x_is_initialized());
}

static void test_ad524x_build_instruction_rejects_invalid_inputs(void) {
  // Step 1. Prime the buffer so we can confirm it remains unchanged on failure.
  uint8_t instruction = 0xAAu;
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, ad524x_build_instruction(2u, false, false, &instruction));
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, ad524x_build_instruction(0u, false, false, NULL));
  // Step 2. Ensure the buffer is left untouched on failure to help catch stale usage.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xAAu, instruction, "Instruction buffer should remain unchanged on error");
}

static void test_ad524x_write_frame_rejects_when_not_initialized(void) {
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_NOT_INITIALIZED, ad524x_write_frame(0x00u, 0x00u));
}

static void test_ad524x_read_frame_rejects_null_buffer(void) {
  TEST_ASSERT_EQUAL_INT(AD524X_ERR_INVALID_ARG, ad524x_read_frame(0x00u, NULL));
}

static void test_ad524x_initialize_accepts_valid_inputs(void) {
  // Step 1. Attempt to initialise the device using the supported address.
  int result = ad524x_initialize(AD5242_I2C_ADDRESS, &Wire);
  // Step 2. Verify the call succeeded and marked the driver as ready.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, result);
  TEST_ASSERT_TRUE(ad524x_is_initialized());
}

static void test_ad524x_build_instruction_sets_expected_bits(void) {
  // Step 1. Seed the instruction buffer to prove the builder overwrites it.
  uint8_t instruction = 0xFFu;
  // Step 2. Build a channel zero instruction with default flags and confirm bits.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_build_instruction(0u, false, false, &instruction));
  TEST_ASSERT_EQUAL_UINT8(0x00u, instruction);

  // Step 3. Build a channel one instruction with shutdown and EEPROM flags set.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_build_instruction(1u, true, true, &instruction));
  TEST_ASSERT_EQUAL_UINT8(0xE0u, instruction);
}

static void test_ad524x_write_and_read_roundtrip_wiper_codes(void) {
  // Step 1. Prepare a range of wiper codes covering low, mid, and high positions.
  const uint8_t test_values[] = {0x00u, 0x80u, 0xFFu};

  // Step 2. Bring the device online to permit frame transactions.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));

  // Step 3. Iterate over both channels to confirm each honours writes.
  for (uint8_t channel = 0u; channel < 2u; ++channel) {
    uint8_t instruction = 0u;
    TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_build_instruction(channel, false, false, &instruction));
    const uint8_t expected_instruction = (channel == 0u) ? 0x00u : 0x80u;
    TEST_ASSERT_EQUAL_UINT8(expected_instruction, instruction);

    // Step 4. Write each test code and ensure an immediate readback matches.
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
  // Step 1. Initialise the driver so the accessors can operate.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));

  // Step 2. Program distinctive values on each channel.
  const uint8_t channel_values[2] = {0x12u, 0xABu};
  for (uint8_t channel = 0u; channel < 2u; ++channel) {
    TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_set_wiper(channel, channel_values[channel]));
  }

  // Step 3. Read the values back to confirm the setters latched correctly.
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
  // Step 1. Create a known non-midscale starting point.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_set_wiper(0u, 0x01u));
  // Step 2. Command the helper to move the wiper to the midpoint.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_set_midscale(0u));

  // Step 3. Confirm the hardware now reports the midscale code.
  uint8_t readback = 0u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(0u, &readback));
  TEST_ASSERT_EQUAL_UINT8(0x80u, readback);
}

static void test_ad524x_shutdown_preserves_wiper_code(void) {
  // Step 1. Initialise the part and capture a distinctive target code.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));
  const uint8_t target = 0x55u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_set_wiper(1u, target));

  // Step 2. Enter and exit shutdown, verifying the code survives both transitions.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_shutdown(1u, true));

  uint8_t readback = 0u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(1u, &readback));
  TEST_ASSERT_EQUAL_UINT8(target, readback);

  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_shutdown(1u, false));
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(1u, &readback));
  TEST_ASSERT_EQUAL_UINT8(target, readback);
}

// Test that shutdown toggles the channel power state (enter/exit shutdown).
static void test_ad524x_shutdown_toggles_channel_power(void) {
  // Step 1: Initialize the driver.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_initialize(AD5242_I2C_ADDRESS, &Wire));

  // Step 2: Set a known wiper value.
  const uint8_t test_value = 0x40u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_set_wiper(0u, test_value));

  // Step 3: Enter shutdown mode for channel 0.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_shutdown(0u, true));

  // Step 4: Verify wiper value is preserved during shutdown.
  uint8_t readback = 0u;
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(0u, &readback));
  TEST_ASSERT_EQUAL_UINT8(test_value, readback);

  // Step 5: Exit shutdown mode.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_shutdown(0u, false));

  // Step 6: Verify wiper value is still preserved after exiting shutdown.
  TEST_ASSERT_EQUAL_INT(AD524X_OK, ad524x_get_wiper(0u, &readback));
  TEST_ASSERT_EQUAL_UINT8(test_value, readback);
}

void setup() {
  // Step 1. Initialise the serial transport shared across Unity tests.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2. Run production bring-up so the analog rails energise and the powered digipot
  // responds on the I2C bus.
  power_control_prepare_power_domains(&g_device_power_control_config);
  Wire.begin();
  // Step 3. Start the Unity harness and execute each suite member.
  UNITY_BEGIN();
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
  RUN_TEST(test_ad524x_shutdown_toggles_channel_power);
  // Step 4. Signal Unity to wrap up so the firmware can idle in loop().
  UNITY_END();
}

void loop() {
}
