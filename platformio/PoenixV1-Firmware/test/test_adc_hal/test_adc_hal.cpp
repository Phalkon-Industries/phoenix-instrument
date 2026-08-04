#include "adc_hal.hpp"
#include "device_setup.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <limits.h>
#include <unity.h>

static const AdcHalConfig k_valid_config = {
    .chip_select_pin = PIN_ADC_CS,
    .spi_clock_hz    = 500000UL,
    .irq_pin         = PIN_ADC_IRQ,
};

static uint32_t g_irq_stub_threshold_polls = 0u;
static uint32_t g_irq_stub_poll_count      = 0u;

// Helper: emulate the DRDY pin by asserting it once a configurable poll budget is consumed.
static bool adc_hal_irq_stub_reader(void) {
  g_irq_stub_poll_count += 1u;
  return g_irq_stub_poll_count >= g_irq_stub_threshold_polls;
}

// Helper: reset stub state and inject the fake pin reader so tests can drive busy-wait logic.
static void adc_hal_configure_irq_stub(uint32_t polls_before_assert) {
  g_irq_stub_poll_count      = 0u;
  g_irq_stub_threshold_polls = polls_before_assert;
  adc_hal_test_set_irq_pin_reader(adc_hal_irq_stub_reader);
}

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

static void test_adc_hal_read_channel_irq_requires_initialisation(void) {
  int32_t   sample_code = 0;
  const int return_code = adc_hal_read_channel_irq(AdcHalChannel::ADC_HAL_CHANNEL_1, 200u, &sample_code);
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

static void test_adc_hal_read_channel_irq_times_out_without_interrupt(void) {
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());

  adc_hal_configure_irq_stub(UINT32_MAX);
  int32_t sample_code = 0;
  TEST_ASSERT_EQUAL_INT(ADC_HAL_ERR_TIMEOUT,
                        adc_hal_read_channel_irq(AdcHalChannel::ADC_HAL_CHANNEL_3, 1000u, &sample_code));
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, g_irq_stub_poll_count, "Expected busy-wait loop to poll the IRQ stub");
}

static void test_adc_hal_read_channel_irq_returns_sample_when_isr_fires(void) {
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());

  adc_hal_test_stage_irq_sample(0x0055AA, 0u);
  adc_hal_configure_irq_stub(5u);

  int32_t sample_code = 0;
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_read_channel_irq(AdcHalChannel::ADC_HAL_CHANNEL_2, 5000u, &sample_code));
  TEST_ASSERT_EQUAL_INT32(0x0055AA, sample_code);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(5u, g_irq_stub_poll_count);
}

static void test_adc_hal_read_channel_irq_returns_sample_from_hardware(void) {
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_initialize(&k_valid_config));
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_apply_default_configuration());

  adc_hal_test_set_irq_pin_reader(NULL);

  int32_t sample_code = 0;
  TEST_ASSERT_EQUAL_INT(ADC_HAL_OK, adc_hal_read_channel_irq(AdcHalChannel::ADC_HAL_CHANNEL_1, 5000u, &sample_code));

  TEST_ASSERT_GREATER_OR_EQUAL_INT32(-0x800000, sample_code);
  TEST_ASSERT_LESS_OR_EQUAL_INT32(0x7FFFFF, sample_code);
}

void setup() {
  // Step 1. Prepare the Unity serial transport shared across firmware tests.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2. Run production bring-up so the analog rails energise and the powered ADC responds.
  power_control_prepare_power_domains(&g_device_power_control_config);

  // Step 3. Start Unity and register each ADC HAL test case.
  UNITY_BEGIN();
  RUN_TEST(test_adc_hal_initialize_rejects_null_config);
  RUN_TEST(test_adc_hal_initialize_programs_backend_defaults);
  RUN_TEST(test_adc_hal_read_requires_initialisation);
  RUN_TEST(test_adc_hal_read_channel_irq_requires_initialisation);
  RUN_TEST(test_adc_hal_enter_standby_succeeds_post_initialise);
  RUN_TEST(test_adc_hal_apply_default_configuration_is_idempotent);
  RUN_TEST(test_adc_hal_read_single_ended_tracks_last_channel);
  RUN_TEST(test_adc_hal_read_channel_irq_times_out_without_interrupt);
  RUN_TEST(test_adc_hal_read_channel_irq_returns_sample_when_isr_fires);
  RUN_TEST(test_adc_hal_read_channel_irq_returns_sample_from_hardware);
  // Step 4. Signal Unity to flush results before yielding to loop().
  UNITY_END();
}

void loop() {
}
