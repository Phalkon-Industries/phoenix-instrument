// test_mcp356x.cpp (Fast command initial test)
// Goal: verify that sending FAST START command (0b01101000) returns STATUS 0b00010111.

#include "main.hpp"
#include "unity_config.h"
#include <limits.h>
#include <unity.h>

// Expected status byte when issuing FAST START command (device address bits = 0b01)
#define MCP356X_EXPECTED_STATUS_START 0x17u

static const uint32_t k_spi_clock_hz           = 500000UL;
static const uint8_t  k_config1_prescaler_mask = 0xC0u;

static void test_fast_command_start_status(void) {
  // Step 1. Trigger a full reset so the ADC starts from power-on defaults.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  delay(2);

  // Step 2. Issue the FAST START command and capture the returned status byte.
  uint8_t status = 0xFFu;
  TEST_ASSERT_EQUAL_MESSAGE(MCP356X_OK, mcp356x_start_conversion(&status), "Fast command send failed");

  // Step 3. Confirm the status flags match the expected bit pattern for FAST START.
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(MCP356X_EXPECTED_STATUS_START, status,
                                 "Unexpected STATUS byte from FAST START command");
}

static void test_config0_register_roundtrip(void) {
  // Step 1. Snapshot CONFIG0 before making any modifications.
  uint8_t config0_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG0, &config0_before, 1u, NULL));

  // Step 2. Flip a safe OSR bit to guarantee the register value changes.
  const uint8_t config0_flip_mask = 0x04u;
  uint8_t       config0_modified  = (uint8_t) (config0_before ^ config0_flip_mask);
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_CONFIG0, &config0_modified, 1u, NULL));

  // Step 3. Read the register back to verify the write took effect.
  uint8_t config0_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG0, &config0_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(config0_modified, config0_after);
  TEST_ASSERT_NOT_EQUAL_HEX8(config0_before, config0_after);

  // Step 4. Restore the original value so later tests see the baseline configuration.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_CONFIG0, &config0_before, 1u, NULL));
}

static void test_full_reset_restores_por_defaults(void) {
  // Step 1. Assert a full reset to force the ADC back to its POR image.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  delay(2);

  // Step 2. Verify CONFIG0-3 match the documented power-on reset values.
  uint8_t config_value = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG0, &config_value, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(MCP356X_CONFIG0_POR, config_value);

  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config_value, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(MCP356X_CONFIG1_POR, config_value);

  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG2, &config_value, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(MCP356X_CONFIG2_POR, config_value);

  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG3, &config_value, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(MCP356X_CONFIG3_POR, config_value);
}

static void test_single_ended_ch0_conversion(void) {
  // Step 1. Reset the ADC to a clean baseline before programming registers.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  delay(2);

  // Step 2. Configure CONFIG0-3 for single-ended conversions.
  static const uint8_t config0 = 0b10110011;
  static const uint8_t config1 = 0b00001100;
  static const uint8_t config2 = 0b10001011;
  static const uint8_t config3 = 0b00000000;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_CONFIG0, &config0, 1u, NULL));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_CONFIG1, &config1, 1u, NULL));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_CONFIG2, &config2, 1u, NULL));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_CONFIG3, &config3, 1u, NULL));

  // Step 3. Route channel 0 against AGND via the MUX register.
  const uint8_t mux_single_ch0 = (uint8_t) ((MCP356X_MUX_CH0 << 4) | MCP356X_MUX_AGND);
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_MUX, &mux_single_ch0, 1u, NULL));

  // Step 4. Kick off a conversion and poll the data register for fresh data.
  uint8_t status = 0xFFu;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_start_conversion(&status));

  uint8_t adc_bytes[3]      = {0};
  bool    data_ready        = false;
  bool    read_back_nonzero = false;
  for (int attempt = 0; attempt < 10 && !data_ready; ++attempt) {
    TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_ADCDATA, adc_bytes, sizeof(adc_bytes), &status));
    data_ready        = ((status & MCP356X_STATUS_DR_MASK) == 0u);
    read_back_nonzero = read_back_nonzero || (adc_bytes[0] | adc_bytes[1] | adc_bytes[2]);
    if (!data_ready) {
      delay(10);
    }
  }

  // Step 5. Fail the test if new data never arrived or remained zeroed.
  TEST_ASSERT_TRUE_MESSAGE(data_ready, "ADC did not present fresh data within timeout");
  TEST_ASSERT_TRUE_MESSAGE(read_back_nonzero, "ADC data register remained zero; conversion likely failed");

  // Step 6. Sign-extend the 24-bit result and validate it sits within the expected range.
  int32_t raw_value = (int32_t) ((adc_bytes[0] << 16) | (adc_bytes[1] << 8) | adc_bytes[2]);
  if (raw_value & 0x800000) {
    raw_value |= 0xFF000000;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_INT32(-0x800000, raw_value);
  TEST_ASSERT_LESS_OR_EQUAL_INT32(0x7FFFFF, raw_value);

  // Step 7. Read the data register again to ensure DR_STATUS flags "no new data" after consumption.
  uint8_t status_after     = 0xFFu;
  uint8_t discard_bytes[3] = {0};
  TEST_ASSERT_EQUAL(MCP356X_OK,
                    mcp356x_read_register(MCP356X_REG_ADCDATA, discard_bytes, sizeof(discard_bytes), &status_after));
  TEST_ASSERT_TRUE_MESSAGE((status_after & MCP356X_STATUS_DR_MASK) != 0u,
                           "Expected DR_STATUS to flag 'no new data' after consuming sample");

  // Step 8. Park the device in standby and reset it for subsequent tests.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_enter_standby(NULL));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
}

static void test_apply_default_config_writes_expected_values(void) {
  // Step 1. Apply the canned default configuration to the device.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Verify each CONFIG register matches its documented default.
  uint8_t config_value = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG0, &config_value, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(MCP356X_CONFIG0_DEFAULT, config_value);

  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config_value, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(MCP356X_CONFIG1_DEFAULT, config_value);

  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG2, &config_value, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(MCP356X_CONFIG2_DEFAULT, config_value);

  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG3, &config_value, 1u, NULL));
  const uint8_t expected_config3 =
      (uint8_t) (((static_cast<uint8_t>(mcp356x_conversion_mode::oneshot_shutdown) & 0x03u) << 6) |
                 ((static_cast<uint8_t>(mcp356x_data_format::data24) & 0x03u) << 4) |
                 (MCP356X_CONFIG3_DEFAULT & 0x0Fu));
  TEST_ASSERT_EQUAL_HEX8(expected_config3, config_value);
}

static void test_apply_settings_programs_requested_fields(void) {
  // Step 0. Reset to the default configuration to ensure a known baseline.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 1. Apply the configuration using the unified helper.
  const mcp356x_settings settings = {
      mcp356x_gain::gain_x32,
      mcp356x_osr::osr_2048,
      mcp356x_prescaler::mclk_div4,
      mcp356x_conversion_mode::continuous,
      mcp356x_data_format::data32_signed,
  };
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_settings(&settings));

  // Step 2. Verify CONFIG0-3 reflect the requested combination.
  uint8_t config_value = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG0, &config_value, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(MCP356X_CONFIG0_DEFAULT, config_value);

  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config_value, 1u, NULL));
  const uint8_t expected_config1 = (uint8_t) (((static_cast<uint8_t>(mcp356x_prescaler::mclk_div4) & 0x03u) << 6) |
                                              (static_cast<uint8_t>(mcp356x_osr::osr_2048) << 2));
  TEST_ASSERT_EQUAL_HEX8(expected_config1, config_value);

  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG2, &config_value, 1u, NULL));
  const uint8_t expected_config2 = (uint8_t) ((MCP356X_CONFIG2_DEFAULT & 0xC7u) | (0b110u << 3) | 0x01u);
  TEST_ASSERT_EQUAL_HEX8(expected_config2, config_value);

  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG3, &config_value, 1u, NULL));
  const uint8_t expected_config3 =
      (uint8_t) (((static_cast<uint8_t>(mcp356x_conversion_mode::continuous) & 0x03u) << 6) |
                 ((static_cast<uint8_t>(mcp356x_data_format::data32_signed) & 0x03u) << 4) |
                 (MCP356X_CONFIG3_DEFAULT & 0x0Fu));
  TEST_ASSERT_EQUAL_HEX8(expected_config3, config_value);
}

static void test_set_gain_updates_config2_gain_bits(void) {
  // Step 1. Load the default configuration to ensure a known starting point.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Snapshot CONFIG2 before changing the gain.
  uint8_t config2_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_before, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(MCP356X_CONFIG2_DEFAULT, config2_before);

  // Step 3. Request an 8× gain and then read CONFIG2 again.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_gain(mcp356x_gain::gain_x8));

  uint8_t config2_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_after, 1u, NULL));
  const uint8_t expected_config2 = (uint8_t) ((config2_before & 0xC7u) | (0b100u << 3) | 0x01u);
  TEST_ASSERT_EQUAL_HEX8(expected_config2, config2_after);

  // Step 4. Ensure the helper also updates the cached gain value.
  mcp356x_gain reported_gain = mcp356x_gain::gain_x1;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_get_gain(&reported_gain));
  TEST_ASSERT_EQUAL(mcp356x_gain::gain_x8, reported_gain);
}

static void test_set_osr_updates_config1_bits(void) {
  // Step 1. Apply the baseline configuration to establish known CONFIG1 bits.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Snapshot CONFIG1 before toggling the oversampling ratio.
  uint8_t config1_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_before, 1u, NULL));

  // Step 3. Request a higher OSR level and then read CONFIG1 back.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_osr(mcp356x_osr::osr_8192));

  uint8_t config1_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_after, 1u, NULL));

  // Step 4. Verify PRE bits are preserved, OSR bits match the request, and reserved bits stay cleared.
  const uint8_t expected_config1 =
      (uint8_t) ((config1_before & 0xC0u) | (static_cast<uint8_t>(mcp356x_osr::osr_8192) << 2));
  TEST_ASSERT_EQUAL_HEX8(expected_config1, config1_after);
}

static void test_get_osr_reads_current_setting(void) {
  // Step 1. Load the default configuration and then select an alternate OSR.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_osr(mcp356x_osr::osr_2048));

  // Step 2. Retrieve the cached OSR and confirm it matches the setting.
  mcp356x_osr reported_osr = mcp356x_osr::osr_4096;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_get_osr(&reported_osr));
  TEST_ASSERT_EQUAL(mcp356x_osr::osr_2048, reported_osr);
}

static void test_default_config_helpers_program_expected_osr(void) {
  // Step 1. Apply the legacy default configuration and confirm the OSR field is 4096.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  mcp356x_osr default_osr = mcp356x_osr::osr_32;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_get_osr(&default_osr));
  TEST_ASSERT_EQUAL(mcp356x_osr::osr_4096, default_osr);

  // Step 2. Apply the settings helper with both gain and OSR overrides and inspect CONFIG1/CONFIG2.
  const mcp356x_settings settings = {
      mcp356x_gain::gain_x8,        mcp356x_osr::osr_8192,
      mcp356x_prescaler::mclk_div1, mcp356x_conversion_mode::continuous,
      mcp356x_data_format::data24,
  };
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_settings(&settings));

  uint8_t config1_value = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL));
  const uint8_t expected_config1 = (uint8_t) ((static_cast<uint8_t>(mcp356x_prescaler::mclk_div1) << 6) |
                                              (static_cast<uint8_t>(mcp356x_osr::osr_8192) << 2));
  TEST_ASSERT_EQUAL_HEX8(expected_config1, config1_value);

  uint8_t config2_value = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL));
  const uint8_t expected_config2 = (uint8_t) ((MCP356X_CONFIG2_DEFAULT & 0xC7u) | (0b100u << 3) | 0x01u);
  TEST_ASSERT_EQUAL_HEX8(expected_config2, config2_value);
}

static void test_apply_settings_rejects_invalid_arguments(void) {
  // Step 0. Ensure a known baseline image before exercising guard rails.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 1. Reject NULL pointers.
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_apply_settings(NULL));

  // Step 2. Reject invalid OSR encodings.
  const mcp356x_settings invalid_osr_settings = {
      mcp356x_gain::gain_x1,        static_cast<mcp356x_osr>(0x10u),
      mcp356x_prescaler::mclk_div1, mcp356x_conversion_mode::continuous,
      mcp356x_data_format::data24,
  };
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_apply_settings(&invalid_osr_settings));

  // Step 3. Reject invalid prescaler encodings while leaving CONFIG1 unchanged.
  uint8_t config1_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_before, 1u, NULL));

  const mcp356x_settings invalid_prescaler_settings = {
      mcp356x_gain::gain_x1,
      mcp356x_osr::osr_4096,
      static_cast<mcp356x_prescaler>(0x04u),
      mcp356x_conversion_mode::continuous,
      mcp356x_data_format::data24,
  };
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_apply_settings(&invalid_prescaler_settings));

  uint8_t config1_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(config1_before, config1_after);

  // Step 4. Reject invalid conversion mode encodings and ensure CONFIG3 remains untouched.
  uint8_t config3_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG3, &config3_before, 1u, NULL));

  const mcp356x_settings invalid_mode_settings = {
      mcp356x_gain::gain_x1,        mcp356x_osr::osr_4096,
      mcp356x_prescaler::mclk_div1, static_cast<mcp356x_conversion_mode>(0x03u),
      mcp356x_data_format::data24,
  };
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_apply_settings(&invalid_mode_settings));

  uint8_t config3_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG3, &config3_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(config3_before, config3_after);
}

static void test_set_conversion_config_updates_config3_bits(void) {
  // Step 1. Apply the baseline configuration so CONFIG3 starts from a known state.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Prime CONFIG3 with CRC enable so we can verify the helper preserves existing flags.
  const uint8_t config3_seed = 0x04u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_CONFIG3, &config3_seed, 1u, NULL));

  // Step 3. Request one-shot standby conversions with signed 32-bit data output.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_conversion_config(mcp356x_conversion_mode::oneshot_standby,
                                                              mcp356x_data_format::data32_signed));

  // Step 4. Read CONFIG3 back and ensure mode/data bits match while lower flags stay intact.
  uint8_t config3_value = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG3, &config3_value, 1u, NULL));
  const uint8_t expected_config3 =
      (uint8_t) (((static_cast<uint8_t>(mcp356x_conversion_mode::oneshot_standby) & 0x03u) << 6) |
                 ((static_cast<uint8_t>(mcp356x_data_format::data32_signed) & 0x03u) << 4) | (config3_seed & 0x0Fu));
  TEST_ASSERT_EQUAL_HEX8(expected_config3, config3_value);
}

static void test_get_conversion_config_reads_current_settings(void) {
  // Step 1. Apply defaults to guarantee writes below start from POR values.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Program CONFIG3 with an explicit combination of conversion mode and data format.
  const uint8_t programmed_config3 = (uint8_t) (((uint8_t) 0x02u << 6) | ((uint8_t) 0x03u << 4));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_CONFIG3, &programmed_config3, 1u, NULL));

  // Step 3. Retrieve the configuration via the helper and confirm enum decoding.
  mcp356x_conversion_mode reported_mode   = mcp356x_conversion_mode::continuous;
  mcp356x_data_format     reported_format = mcp356x_data_format::data24;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_get_conversion_config(&reported_mode, &reported_format));
  TEST_ASSERT_EQUAL(mcp356x_conversion_mode::oneshot_shutdown, reported_mode);
  TEST_ASSERT_EQUAL(mcp356x_data_format::data32_signed_chid, reported_format);
}

static void test_set_conversion_config_rejects_invalid_mode(void) {
  // Step 1. Capture the CONFIG3 baseline before exercising invalid enums.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());
  uint8_t config3_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG3, &config3_before, 1u, NULL));

  // Step 2. Pass a reserved CONV_MODE value and expect the helper to reject it without writes.
  const mcp356x_conversion_mode invalid_mode = static_cast<mcp356x_conversion_mode>(0x03u);
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_set_conversion_config(invalid_mode, mcp356x_data_format::data24));

  // Step 3. Confirm CONFIG3 remains unchanged after the rejected call.
  uint8_t config3_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG3, &config3_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(config3_before, config3_after);
}

static void test_get_conversion_config_rejects_null_pointers(void) {
  // Step 1. Apply defaults so we can focus on pointer validation paths.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Reject a missing mode pointer while leaving CONFIG3 untouched.
  mcp356x_data_format format = mcp356x_data_format::data24;
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_get_conversion_config(NULL, &format));

  // Step 3. Reject a missing format pointer as well.
  mcp356x_conversion_mode mode = mcp356x_conversion_mode::continuous;
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_get_conversion_config(&mode, NULL));
}

static void test_conversion_config_helpers_require_initialization(void) {
  // Step 1. Force the driver into an uninitialised state to trip guard rails.
  mcp356x_force_uninitialized_for_test();

  // Step 2. Attempt to set and get the conversion configuration while expecting not-initialised errors.
  TEST_ASSERT_EQUAL(MCP356X_ERR_NOT_INITIALIZED,
                    mcp356x_set_conversion_config(mcp356x_conversion_mode::continuous, mcp356x_data_format::data24));

  mcp356x_conversion_mode mode   = mcp356x_conversion_mode::continuous;
  mcp356x_data_format     format = mcp356x_data_format::data24;
  TEST_ASSERT_EQUAL(MCP356X_ERR_NOT_INITIALIZED, mcp356x_get_conversion_config(&mode, &format));

  // Step 3. Reinitialise the driver so subsequent tests run under the normal configuration.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, k_spi_clock_hz));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  delay(2);
}

static void test_conversion_config_updates_cached_data_format(void) {
  // Step 1. Apply the baseline configuration and confirm the cached format reflects the default.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());
  TEST_ASSERT_EQUAL(mcp356x_data_format::data24, mcp356x_test_cached_data_format());

  // Step 2. Request a 32-bit signed data format and ensure the cached state tracks the change.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_conversion_config(mcp356x_conversion_mode::continuous,
                                                              mcp356x_data_format::data32_signed));
  TEST_ASSERT_EQUAL(mcp356x_data_format::data32_signed, mcp356x_test_cached_data_format());
}

static void test_read_single_ended_respects_all_data_formats(void) {
  // Sweep each DATA_FORMAT encoding to ensure both the decoded sample and
  // captured diagnostic word match the datasheet's on-wire representation.
  const mcp356x_data_format formats[] = {
      mcp356x_data_format::data24,
      mcp356x_data_format::data32_left,
      mcp356x_data_format::data32_signed,
      mcp356x_data_format::data32_signed_chid,
  };

  for (size_t i = 0; i < (sizeof formats / sizeof formats[0]); ++i) {
    TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

    if (formats[i] != mcp356x_data_format::data24) {
      TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_conversion_config(mcp356x_conversion_mode::continuous, formats[i]));
    }

    mcp356x_test_reset_diagnostics();

    int32_t       code    = INT32_MIN;
    const uint8_t channel = 0u;  // Test board only routes channel 0; reuse it for all format sweeps.
    TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_single_ended_channel(channel, 200u, &code));

    const uint8_t expected_length = (formats[i] == mcp356x_data_format::data24) ? 3u : 4u;
    TEST_ASSERT_EQUAL_UINT8(expected_length, mcp356x_test_last_data_length());

    const uint32_t raw_word = mcp356x_test_last_raw_word();

    switch (formats[i]) {
      case mcp356x_data_format::data24: {
        const uint32_t raw24          = raw_word & 0xFFFFFFu;
        const int32_t  expected_value = (raw24 & 0x800000u) ? (int32_t) (raw24 | 0xFF000000u) : (int32_t) raw24;
        TEST_ASSERT_EQUAL_INT32(expected_value, code);
        break;
      }
      case mcp356x_data_format::data32_left: {
        TEST_ASSERT_EQUAL_HEX8(0u, raw_word & 0xFFu);
        const int32_t reconstructed = (int32_t) ((int32_t) raw_word >> 8);
        TEST_ASSERT_EQUAL_INT32(reconstructed, code);
        break;
      }
      case mcp356x_data_format::data32_signed: {
        TEST_ASSERT_EQUAL_HEX32((uint32_t) code, raw_word);
        break;
      }
      case mcp356x_data_format::data32_signed_chid: {
        const int32_t reconstructed = (int32_t) ((int32_t) raw_word >> 8);
        TEST_ASSERT_EQUAL_INT32(reconstructed, code);
        break;
      }
      default:
        TEST_FAIL_MESSAGE("Unexpected data format encountered");
    }
  }
}

static void test_osr_helpers_require_initialization(void) {
  // Step 1. Force the driver into an uninitialised state for guard validation.
  mcp356x_force_uninitialized_for_test();

  // Step 2. Attempt to set and get the OSR while expecting not-initialised errors.
  int         return_code = mcp356x_set_osr(mcp356x_osr::osr_512);
  mcp356x_osr out_osr     = mcp356x_osr::osr_256;
  TEST_ASSERT_EQUAL(MCP356X_ERR_NOT_INITIALIZED, return_code);
  TEST_ASSERT_EQUAL(MCP356X_ERR_NOT_INITIALIZED, mcp356x_get_osr(&out_osr));

  // Step 3. Reinitialise the driver so subsequent tests observe the normal state.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, k_spi_clock_hz));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  delay(2);
}

static void test_set_osr_rejects_invalid_enums(void) {
  // Step 1. Establish the default configuration to snapshot CONFIG1.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  uint8_t config1_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_before, 1u, NULL));

  // Step 2. Pass an invalid enum value and expect an invalid-argument error without altering CONFIG1.
  const mcp356x_osr invalid_osr = static_cast<mcp356x_osr>(0x10u);
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_set_osr(invalid_osr));

  uint8_t config1_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(config1_before, config1_after);
}

static void test_set_prescaler_updates_config1_bits(void) {
  // Step 1. Apply the default configuration to establish the CONFIG1 baseline.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Capture CONFIG1 prior to changing the prescaler.
  uint8_t config1_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_before, 1u, NULL));

  // Step 3. Request an MCLK/4 prescaler and read CONFIG1 back.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_prescaler(mcp356x_prescaler::mclk_div4));

  uint8_t config1_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_after, 1u, NULL));

  // Step 4. Verify OSR bits remain untouched and prescaler bits match the request.
  const uint8_t expected_config1 = (uint8_t) ((config1_before & (uint8_t) (~k_config1_prescaler_mask)) |
                                              (static_cast<uint8_t>(mcp356x_prescaler::mclk_div4) << 6));
  TEST_ASSERT_EQUAL_HEX8(expected_config1, config1_after);
}

static void test_get_prescaler_reads_current_setting(void) {
  // Step 1. Apply the default configuration then select a new prescaler value.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_prescaler(mcp356x_prescaler::mclk_div8));

  // Step 2. Query the prescaler and confirm it reflects the updated setting.
  mcp356x_prescaler reported_prescaler = mcp356x_prescaler::mclk_div1;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_get_prescaler(&reported_prescaler));
  TEST_ASSERT_EQUAL(mcp356x_prescaler::mclk_div8, reported_prescaler);
}

static void test_default_config_preserves_por_prescaler(void) {
  // Step 1. Apply the standard default configuration.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Confirm the prescaler remains at the POR default (MCLK/1).
  mcp356x_prescaler reported_prescaler = mcp356x_prescaler::mclk_div2;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_get_prescaler(&reported_prescaler));
  TEST_ASSERT_EQUAL(mcp356x_prescaler::mclk_div1, reported_prescaler);
}

static void test_prescaler_helpers_require_initialization(void) {
  // Step 1. Force the driver into an uninitialised state for guard testing.
  mcp356x_force_uninitialized_for_test();

  // Step 2. Attempt to set and get the prescaler, expecting not-initialised errors.
  int               return_code      = mcp356x_set_prescaler(mcp356x_prescaler::mclk_div2);
  mcp356x_prescaler reported_setting = mcp356x_prescaler::mclk_div1;
  TEST_ASSERT_EQUAL(MCP356X_ERR_NOT_INITIALIZED, return_code);
  TEST_ASSERT_EQUAL(MCP356X_ERR_NOT_INITIALIZED, mcp356x_get_prescaler(&reported_setting));

  // Step 3. Reinitialise for downstream tests.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, k_spi_clock_hz));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  delay(2);
}

static void test_set_prescaler_rejects_invalid_enums(void) {
  // Step 1. Capture CONFIG1 before issuing an invalid prescaler request.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  uint8_t config1_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_before, 1u, NULL));

  // Step 2. Invoke the setter with an out-of-range enum value and expect rejection.
  const mcp356x_prescaler invalid_prescaler = static_cast<mcp356x_prescaler>(0x04u);
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_set_prescaler(invalid_prescaler));

  // Step 3. Confirm CONFIG1 remains unchanged.
  uint8_t config1_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(config1_before, config1_after);
}

static void test_get_gain_rejects_null_pointer(void) {
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_get_gain(NULL));
}

static void test_gain_set_get_roundtrip_sequence(void) {
  // Step 1. Apply the defaults and capture CONFIG2 as a baseline.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  uint8_t config2_snapshot = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_snapshot, 1u, NULL));

  // Step 2. Set the gain to 16× and verify both the register and getter reflect the change.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_gain(mcp356x_gain::gain_x16));

  uint8_t config2_after_first = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_after_first, 1u, NULL));
  const uint8_t expected_first = (uint8_t) ((config2_snapshot & 0xC7u) | (0b101u << 3) | 0x01u);
  TEST_ASSERT_EQUAL_HEX8(expected_first, config2_after_first);

  mcp356x_gain reported_gain = mcp356x_gain::gain_x1;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_get_gain(&reported_gain));
  TEST_ASSERT_EQUAL(mcp356x_gain::gain_x16, reported_gain);

  // Step 3. Drop the gain to 8× and repeat the verification.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_set_gain(mcp356x_gain::gain_x8));

  uint8_t config2_after_second = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_after_second, 1u, NULL));
  const uint8_t expected_second = (uint8_t) ((config2_after_first & 0xC7u) | (0b100u << 3) | 0x01u);
  TEST_ASSERT_EQUAL_HEX8(expected_second, config2_after_second);

  reported_gain = mcp356x_gain::gain_x1;  // Reset before reading back.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_get_gain(&reported_gain));
  TEST_ASSERT_EQUAL(mcp356x_gain::gain_x8, reported_gain);
}

static void test_gain_helpers_require_initialization(void) {
  // Step 1. Force the driver into an uninitialised state to exercise guards.
  mcp356x_force_uninitialized_for_test();

  // Step 2. Attempt to set and get the gain, verifying both calls report not-initialised.
  int          return_code = mcp356x_set_gain(mcp356x_gain::gain_x4);
  mcp356x_gain out_gain    = mcp356x_gain::gain_x1;
  TEST_ASSERT_EQUAL(MCP356X_ERR_NOT_INITIALIZED, return_code);
  TEST_ASSERT_EQUAL(MCP356X_ERR_NOT_INITIALIZED, mcp356x_get_gain(&out_gain));

  // Step 3. Reinitialise and reset the ADC so subsequent tests operate normally.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, k_spi_clock_hz));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  delay(2);
}

static void test_enter_standby_wrapper_matches_fast_command_status(void) {
  // Step 1. Use the helper to place the device into standby and capture the status byte.
  uint8_t helper_status = 0xFFu;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_enter_standby(&helper_status));

  // Step 2. Send the raw FAST STANDBY command and capture its status response.
  uint8_t direct_status = 0xFFu;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_send_fast_command(MCP356X_FASTCMD_STANDBY, &direct_status));

  // Step 3. Compare the two status bytes to ensure they match exactly.
  TEST_ASSERT_EQUAL_HEX8(direct_status, helper_status);
}

static void test_fast_command_wrappers_return_success(void) {
  // Step 1. Reset the device so FAST command behaviour starts from POR defaults.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  delay(2);

  // Step 2. Exercise the start and shutdown helpers without inspecting the status byte.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_start_conversion(NULL));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_enter_adc_shutdown(NULL));
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_enter_full_shutdown(NULL));

  // Step 3. Reset again and confirm the standby helper returns success.
  uint8_t status = 0xFFu;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(&status));
  delay(2);
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_enter_standby(NULL));
}

static void test_read_single_ended_channel_returns_sample(void) {
  // Step 1. Apply the default configuration so the helper can trigger a conversion.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Request a single-ended conversion on channel zero and verify range.
  int32_t conversion = INT32_MIN;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_single_ended_channel(0u, 200u, &conversion));
  TEST_ASSERT_GREATER_OR_EQUAL_INT32(-0x800000, conversion);
  TEST_ASSERT_LESS_OR_EQUAL_INT32(0x7FFFFF, conversion);

  // Step 3. Confirm a subsequent raw register read reports no new data.
  uint8_t status_after     = 0xFFu;
  uint8_t discard_bytes[3] = {0};
  TEST_ASSERT_EQUAL(MCP356X_OK,
                    mcp356x_read_register(MCP356X_REG_ADCDATA, discard_bytes, sizeof(discard_bytes), &status_after));
  TEST_ASSERT_TRUE_MESSAGE((status_after & MCP356X_STATUS_DR_MASK) != 0u,
                           "Expected DR_STATUS to flag 'no new data' after helper read");
}

static void test_mux_select_single_channel_writes_mux_register(void) {
  // Step 1. Reset the ADC and capture the initial MUX register value.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  delay(2);

  uint8_t mux_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_MUX, &mux_before, 1u, NULL));

  // Step 2. Select channel three single-ended against AGND via the helper.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_select_single_ended_channel(3u));

  // Step 3. Read the MUX register and confirm it matches the expected encoding.
  uint8_t mux_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_MUX, &mux_after, 1u, NULL));
  const uint8_t expected_mux = (uint8_t) ((MCP356X_MUX_CH3 << 4) | MCP356X_MUX_AGND);
  TEST_ASSERT_EQUAL_HEX8(expected_mux, mux_after);

  // Step 4. Restore the original value so other tests see the same baseline.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_write_register(MCP356X_REG_MUX, &mux_before, 1u, NULL));
}

static void test_mux_select_single_channel_rejects_invalid_inputs(void) {
  // Step 1. Snapshot the current MUX register before exercising invalid input paths.
  uint8_t mux_snapshot = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_MUX, &mux_snapshot, 1u, NULL));

  // Step 2. Request an out-of-range channel index and expect an invalid-argument error.
  TEST_ASSERT_EQUAL(MCP356X_ERR_INVALID_ARG, mcp356x_select_single_ended_channel(9u));

  // Step 3. Confirm the MUX register remains unchanged after the rejected call.
  uint8_t mux_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_read_register(MCP356X_REG_MUX, &mux_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(mux_snapshot, mux_after);
}

static void test_read_single_ended_channel_times_out_when_data_stalls(void) {
  // Step 1. Apply the default configuration so only the timeout parameter causes failure.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_apply_default_config());

  // Step 2. Request a conversion with a zero-microsecond timeout and expect a timeout error.
  int32_t conversion = INT32_MIN;
  TEST_ASSERT_EQUAL(MCP356X_ERR_TIMEOUT, mcp356x_read_single_ended_channel(0u, 0u, &conversion));
}

void setUp(void) {
  // Step 1. Initialise the MCP356x driver under test before each case runs.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(PIN_ADC_CS, k_spi_clock_hz));

  // Step 2. Reset the ADC so each test begins from the datasheet power-on defaults.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  // Step 3. Wait for the reset sequence to complete before proceeding.
  delay(2);
}

void tearDown(void) {
  // Step 1. Park the ADC so the next test does not inherit an active conversion.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_enter_standby(NULL));
  // Step 2. Restore POR defaults to leave hardware neutral for subsequent runs.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_full_reset(NULL));
  // Step 3. Wait for the reset to take effect prior to exiting.
  delay(2);
}

void setup() {
  // Step 1. Initialise Unity's serial transport for logging.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2. Execute each MCP356x test case in sequence.
  RUN_TEST(test_fast_command_start_status);
  RUN_TEST(test_config0_register_roundtrip);
  RUN_TEST(test_full_reset_restores_por_defaults);
  RUN_TEST(test_single_ended_ch0_conversion);
  RUN_TEST(test_mux_select_single_channel_writes_mux_register);
  RUN_TEST(test_mux_select_single_channel_rejects_invalid_inputs);
  RUN_TEST(test_apply_default_config_writes_expected_values);
  RUN_TEST(test_apply_settings_programs_requested_fields);
  RUN_TEST(test_set_gain_updates_config2_gain_bits);
  RUN_TEST(test_set_osr_updates_config1_bits);
  RUN_TEST(test_get_osr_reads_current_setting);
  RUN_TEST(test_default_config_helpers_program_expected_osr);
  RUN_TEST(test_apply_settings_rejects_invalid_arguments);
  RUN_TEST(test_set_conversion_config_updates_config3_bits);
  RUN_TEST(test_get_conversion_config_reads_current_settings);
  RUN_TEST(test_set_conversion_config_rejects_invalid_mode);
  RUN_TEST(test_get_conversion_config_rejects_null_pointers);
  RUN_TEST(test_conversion_config_helpers_require_initialization);
  RUN_TEST(test_conversion_config_updates_cached_data_format);
  RUN_TEST(test_read_single_ended_respects_all_data_formats);
  RUN_TEST(test_osr_helpers_require_initialization);
  RUN_TEST(test_set_osr_rejects_invalid_enums);
  RUN_TEST(test_set_prescaler_updates_config1_bits);
  RUN_TEST(test_get_prescaler_reads_current_setting);
  RUN_TEST(test_default_config_preserves_por_prescaler);
  RUN_TEST(test_prescaler_helpers_require_initialization);
  RUN_TEST(test_set_prescaler_rejects_invalid_enums);
  RUN_TEST(test_get_gain_rejects_null_pointer);
  RUN_TEST(test_gain_set_get_roundtrip_sequence);
  RUN_TEST(test_gain_helpers_require_initialization);
  RUN_TEST(test_enter_standby_wrapper_matches_fast_command_status);
  RUN_TEST(test_fast_command_wrappers_return_success);
  RUN_TEST(test_read_single_ended_channel_returns_sample);
  RUN_TEST(test_read_single_ended_channel_times_out_when_data_stalls);
  // Step 3. Finalise Unity before handing control back to loop().
  UNITY_END();
}

void loop() {
}
