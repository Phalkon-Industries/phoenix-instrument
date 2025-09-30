// test_mcp356x.cpp (Fast command initial test)
// Goal: verify that sending FAST START command (0b01101000) returns STATUS 0b00010111.

#include <unity.h>
#include "unity_config.h"
#include "main.hpp"

// Expected status byte when issuing FAST START command (device address bits = 0b01)
#define MCP356X_EXPECTED_STATUS_START 0x17u

static void test_fast_command_start_status(void)
{
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, 500000UL));

    uint8_t status = 0xFF;
    int rc = mcp356x_send_fast_command(MCP356X_FASTCMD_START, &status);
    TEST_ASSERT_EQUAL_MESSAGE(MCP356X_OK, rc, "Fast command send failed");

    TEST_ASSERT_EQUAL_HEX8_MESSAGE(MCP356X_EXPECTED_STATUS_START, status, "Unexpected STATUS byte from FAST START command");
}

static void test_config0_register_roundtrip(void)
{
  // Arrange: start the driver and capture the current CONFIG0 byte.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, 500000UL));

  uint8_t config0_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_read_register(MCP356X_REG_CONFIG0, &config0_before, 1u, NULL));

  // Act: flip a safe OSR bit (bit 2) so the value definitely changes.
  const uint8_t config0_flip_mask = 0x04u;
  uint8_t config0_modified = (uint8_t)(config0_before ^ config0_flip_mask);
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_CONFIG0, &config0_modified, 1u, NULL));

  // Assert: the new value is observable via another read.
  uint8_t config0_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_read_register(MCP356X_REG_CONFIG0, &config0_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(config0_modified, config0_after);
  TEST_ASSERT_NOT_EQUAL_HEX8(config0_before, config0_after);

  // Cleanup: restore the original CONFIG0 settings.
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_CONFIG0, &config0_before, 1u, NULL));
}

static void test_single_ended_ch0_conversion(void)
{
  // Configure the ADC for a single-ended measurement on channel 0 relative to AGND
  // and verify that a conversion completes with a fresh STATUS response.
  // Steps:
  //   1. Reset the device to a known baseline (datasheet POR settings).
  //   2. Program CONFIG0-3 and the MUX for single-ended CH0 -> AGND sampling.
  //   3. Issue START, poll ADCDATA until DR_STATUS reports "new data".
  //   4. Confirm the data register is non-zero, decode it, and ensure DR_STATUS
  //      clears after the read (meaning we consumed the sample).
  //   5. Return the device to a quiescent state (standby + reset) for the next test.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, 500000UL));

  uint8_t status = 0xFFu;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_send_fast_command(MCP356X_FASTCMD_FULLRESET, &status));
  delay(2);  // allow registers to settle after POR reset

  // Config block derived from exploratory sketch (adc_example.cpp). The values
  // prioritise stability and a known reference point for regression tests.
  static const uint8_t config0 = 0b10110011;  // Internal ref, continuous conversion, standby disabled
  static const uint8_t config1 = 0b00001100;  // OSR=4096, boost off (high-resolution mode)
  static const uint8_t config2 = 0b10001011;  // 24-bit data, gain=1, auto-zero enabled
  static const uint8_t config3 = 0b00000000;  // No auto-seq, default conversion delay
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_CONFIG0, &config0, 1u, NULL));
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_CONFIG1, &config1, 1u, NULL));
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_CONFIG2, &config2, 1u, NULL));
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_CONFIG3, &config3, 1u, NULL));

  const uint8_t mux_single_ch0 = (uint8_t)((MCP356X_MUX_CH0 << 4) | MCP356X_MUX_AGND);
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_MUX, &mux_single_ch0, 1u, NULL));

  // Kick off a single conversion and allow the modulator/decimator pipeline to run.
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_send_fast_command(MCP356X_FASTCMD_START, &status));

  uint8_t adc_bytes[3] = {0};
  // Poll ADCDATA until the datasheet DR_STATUS bit indicates "new data" and
  // track whether any of the returned bytes are non-zero. A stuck-zero payload
  // is a strong indicator the ADC never left reset.
  bool data_ready = false;
  bool read_back_nonzero = false;
  for (int attempt = 0; attempt < 10 && !data_ready; ++attempt) {
    TEST_ASSERT_EQUAL(MCP356X_OK,
              mcp356x_read_register(MCP356X_REG_ADCDATA, adc_bytes, sizeof(adc_bytes), &status));
    data_ready = ((status & MCP356X_STATUS_DR_MASK) == 0u);
    read_back_nonzero = read_back_nonzero || (adc_bytes[0] | adc_bytes[1] | adc_bytes[2]);
    if (!data_ready) {
      delay(10);
    }
  }

  TEST_ASSERT_TRUE_MESSAGE(data_ready, "ADC did not present fresh data within timeout");
  TEST_ASSERT_TRUE_MESSAGE(read_back_nonzero, "ADC data register remained zero; conversion likely failed");

  int32_t raw_value = (int32_t)((adc_bytes[0] << 16) | (adc_bytes[1] << 8) | adc_bytes[2]);
  if (raw_value & 0x800000) {
    raw_value |= 0xFF000000;  // sign-extend 24-bit two's complement
  }

  TEST_ASSERT_GREATER_OR_EQUAL_INT32(-0x800000, raw_value);
  TEST_ASSERT_LESS_OR_EQUAL_INT32(0x7FFFFF, raw_value);

  // A second read should report "no new data" (DR_STATUS=1) because the first
  // access already consumed the sample and latched the status bits.
  uint8_t status_after = 0xFFu;
  uint8_t discard_bytes[3] = {0};
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_read_register(MCP356X_REG_ADCDATA, discard_bytes, sizeof(discard_bytes), &status_after));
  TEST_ASSERT_TRUE_MESSAGE((status_after & MCP356X_STATUS_DR_MASK) != 0u,
               "Expected DR_STATUS to flag 'no new data' after consuming sample");

  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_send_fast_command(MCP356X_FASTCMD_STANDBY, &status));
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_send_fast_command(MCP356X_FASTCMD_FULLRESET, &status));
}

static void test_mux_select_single_channel_writes_mux_register(void)
{
  // Arrange: ensure the device starts from a known baseline before applying the helper.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, 500000UL));

  uint8_t status = 0xFFu;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_send_fast_command(MCP356X_FASTCMD_FULLRESET, &status));
  delay(2);

  uint8_t mux_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_read_register(MCP356X_REG_MUX, &mux_before, 1u, NULL));

  // Act: select channel 3 single-ended against AGND and capture the written value.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_select_single_ended_channel(3u));

  uint8_t mux_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_read_register(MCP356X_REG_MUX, &mux_after, 1u, NULL));

  const uint8_t expected_mux = (uint8_t)((MCP356X_MUX_CH3 << 4) | MCP356X_MUX_AGND);
  TEST_ASSERT_EQUAL_HEX8(expected_mux, mux_after);

  // Cleanup: restore the register to its original state so subsequent tests are unaffected.
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_MUX, &mux_before, 1u, NULL));
}

static void test_mux_select_single_channel_rejects_invalid_inputs(void)
{
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, 500000UL));

  uint8_t mux_snapshot = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_read_register(MCP356X_REG_MUX, &mux_snapshot, 1u, NULL));

  // Act: request an out-of-range channel (>=8) and verify the helper refuses to update hardware.
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_select_single_ended_channel(9u));

  uint8_t mux_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_read_register(MCP356X_REG_MUX, &mux_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(mux_snapshot, mux_after);
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
  UNITY_SETUP_SERIAL_DEFAULT();
    RUN_TEST(test_fast_command_start_status);
  RUN_TEST(test_config0_register_roundtrip);
  RUN_TEST(test_single_ended_ch0_conversion);
  RUN_TEST(test_mux_select_single_channel_writes_mux_register);
  RUN_TEST(test_mux_select_single_channel_rejects_invalid_inputs);
    UNITY_END();
}

void loop() {}
