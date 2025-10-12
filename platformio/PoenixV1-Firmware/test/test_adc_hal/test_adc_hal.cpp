#include "adc_hal.hpp"
#include "main.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <unity.h>

static const AdcHalConfig k_valid_config = {
    .chip_select_pin = PIN_ADC_CS,
    .spi_clock_hz    = 500000UL,
    .default_gain    = AdcHalGain::ADC_HAL_GAIN_1,
};

void setUp(void) {
  // Step 1. Clear any prior driver state so tests remain isolated.
  adc_hal_reset_for_test();
}

void tearDown(void) {
}

static void test_adc_hal_initialize_rejects_null_config(void) {
  const int return_code = adc_hal_initialize(NULL);
  TEST_ASSERT_EQUAL_INT(ADC_HAL_ERR_INVALID_ARG, return_code);
}

static void test_adc_hal_initialize_programs_backend_defaults(void) {
  // Step 1. Bring the HAL online using the validated configuration.
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));
  // Step 2. Apply the defaults and ensure they succeed after initialisation.
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());
}

static void test_adc_hal_read_requires_initialisation(void) {
  // Step 1. Attempt a conversion before calling initialise to confirm the guard.
  int32_t   sample_code = 0;
  const int return_code = adc_hal_read_single_ended(AdcHalChannel::ADC_HAL_CHANNEL_4, 200u, &sample_code);
  TEST_ASSERT_EQUAL_INT(ADC_HAL_ERR_NOT_INITIALIZED, return_code);
}

static void test_adc_hal_enter_standby_succeeds_post_initialise(void) {
  // Step 1. Initialise the HAL so standby mode becomes available.
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));
  // Step 2. Request standby and verify the call propagates through the backend.
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_enter_standby());
}

static void test_adc_hal_apply_default_configuration_is_idempotent(void) {
  // Step 1. Confirm the helper did not run prior to this test.
  TEST_ASSERT_EQUAL_UINT32(0u, adc_hal_test_default_config_call_count());
  // Step 2. Initialise and apply the defaults once to seed internal state.
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));

  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());
  TEST_ASSERT_EQUAL_UINT32(1u, adc_hal_test_default_config_call_count());

  // Step 3. Reapply defaults and ensure the backend does not double-program.
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());
  TEST_ASSERT_EQUAL_UINT32(1u, adc_hal_test_default_config_call_count());
}

static void test_adc_hal_apply_default_configuration_propagates_gain(void) {
  // Step 1. Clone the baseline config so we can modify only the gain field.
  AdcHalConfig gain_config = k_valid_config;
  gain_config.default_gain = AdcHalGain::ADC_HAL_GAIN_32;

  // Step 2. Initialise using the modified gain and apply defaults.
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&gain_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());

  // Step 3. Verify the shim recorded the gain request exactly once.
  TEST_ASSERT_EQUAL_UINT32(1u, adc_hal_test_default_config_call_count());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdcHalGain::ADC_HAL_GAIN_32),
                          static_cast<uint8_t>(adc_hal_test_last_gain_requested()));
}

static void test_adc_hal_read_single_ended_tracks_last_channel(void) {
  // Step 1. Bring the HAL online and push the canned default configuration.
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());

  // Step 2. Issue a single-ended conversion and confirm the shim records it.
  int32_t sample_code = 0;
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK,
                        adc_hal_read_single_ended(AdcHalChannel::ADC_HAL_CHANNEL_5, 1000000u, &sample_code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdcHalChannel::ADC_HAL_CHANNEL_5),
                          static_cast<uint8_t>(adc_hal_test_last_channel_requested()));
}

void setup() {
  // Step 1. Prepare the Unity serial transport shared across firmware tests.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2. Start Unity and register each ADC HAL test case.
  UNITY_BEGIN();
  RUN_TEST(test_adc_hal_initialize_rejects_null_config);
  RUN_TEST(test_adc_hal_initialize_programs_backend_defaults);
  RUN_TEST(test_adc_hal_read_requires_initialisation);
  RUN_TEST(test_adc_hal_enter_standby_succeeds_post_initialise);
  RUN_TEST(test_adc_hal_apply_default_configuration_is_idempotent);
  RUN_TEST(test_adc_hal_apply_default_configuration_propagates_gain);
  RUN_TEST(test_adc_hal_read_single_ended_tracks_last_channel);
  // Step 3. Signal Unity to flush results before yielding to loop().
  UNITY_END();
}

void loop() {
}
