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
  adc_hal_reset_for_test();
}

void tearDown(void) {
}

static void test_adc_hal_initialize_rejects_null_config(void) {
  const int return_code = adc_hal_initialize(NULL);
  TEST_ASSERT_EQUAL_INT(ADC_HAL_ERR_INVALID_ARG, return_code);
}

static void test_adc_hal_initialize_programs_backend_defaults(void) {
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());
}

static void test_adc_hal_read_requires_initialisation(void) {
  int32_t   sample_code = 0;
  const int return_code = adc_hal_read_single_ended(AdcHalChannel::ADC_HAL_CHANNEL_4, 200u, &sample_code);
  TEST_ASSERT_EQUAL_INT(ADC_HAL_ERR_NOT_INITIALIZED, return_code);
}

static void test_adc_hal_enter_standby_succeeds_post_initialise(void) {
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_enter_standby());
}

static void test_adc_hal_apply_default_configuration_is_idempotent(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, adc_hal_test_default_config_call_count());
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));

  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());
  TEST_ASSERT_EQUAL_UINT32(1u, adc_hal_test_default_config_call_count());

  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());
  TEST_ASSERT_EQUAL_UINT32(1u, adc_hal_test_default_config_call_count());
}

static void test_adc_hal_apply_default_configuration_propagates_gain(void) {
  AdcHalConfig gain_config = k_valid_config;
  gain_config.default_gain = AdcHalGain::ADC_HAL_GAIN_32;

  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&gain_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());

  TEST_ASSERT_EQUAL_UINT32(1u, adc_hal_test_default_config_call_count());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdcHalGain::ADC_HAL_GAIN_32),
                          static_cast<uint8_t>(adc_hal_test_last_gain_requested()));
}

static void test_adc_hal_read_single_ended_tracks_last_channel(void) {
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());

  int32_t sample_code = 0;
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK,
                        adc_hal_read_single_ended(AdcHalChannel::ADC_HAL_CHANNEL_5, 1000000u, &sample_code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdcHalChannel::ADC_HAL_CHANNEL_5),
                          static_cast<uint8_t>(adc_hal_test_last_channel_requested()));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  UNITY_BEGIN();
  RUN_TEST(test_adc_hal_initialize_rejects_null_config);
  RUN_TEST(test_adc_hal_initialize_programs_backend_defaults);
  RUN_TEST(test_adc_hal_read_requires_initialisation);
  RUN_TEST(test_adc_hal_enter_standby_succeeds_post_initialise);
  RUN_TEST(test_adc_hal_apply_default_configuration_is_idempotent);
  RUN_TEST(test_adc_hal_apply_default_configuration_propagates_gain);
  RUN_TEST(test_adc_hal_read_single_ended_tracks_last_channel);
  UNITY_END();
}

void loop() {
}
