#include "thermistor_reader.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <unity.h>

static AdcHalChannel g_recorded_channels[4] = {};
static size_t        g_recorded_channel_count;
static int32_t       g_stub_reference_code   = 100000;
static int32_t       g_stub_board_code       = 100000;
static int32_t       g_stub_water_code       = 100000;
static int32_t       g_stub_water_resistance = 10000;
static int           g_stub_error_code       = ADC_HAL_OK;
static bool          g_stub_fail_on_reference;
static bool          g_rail_enabled;
static bool          g_adc_should_assert_rail_enabled;

struct GpioEvent {
  int      pin;
  uint32_t value;
};

static GpioEvent g_gpio_events[8] = {};
static size_t    g_gpio_event_count;
static int       g_expected_rail_pin = -1;
static uint32_t  g_delay_call_count;
static uint32_t  g_last_delay_us;

static void reset_adc_stub_state(void) {
  memset(g_recorded_channels, 0, sizeof(g_recorded_channels));
  g_recorded_channel_count         = 0u;
  g_stub_reference_code            = 100000;
  g_stub_board_code                = 100000;
  g_stub_water_code                = 100000;
  g_stub_water_resistance          = 10000;
  g_stub_error_code                = ADC_HAL_OK;
  g_stub_fail_on_reference         = false;
  g_adc_should_assert_rail_enabled = false;
}

static void reset_gpio_stub_state(void) {
  memset(g_gpio_events, 0, sizeof(g_gpio_events));
  g_gpio_event_count               = 0u;
  g_expected_rail_pin              = -1;
  g_rail_enabled                   = false;
  g_delay_call_count               = 0u;
  g_last_delay_us                  = 0u;
  g_adc_should_assert_rail_enabled = false;
}

static int thermistor_reader_adc_stub(AdcHalChannel channel, uint32_t timeout_us, int32_t* code_out) {
  (void) timeout_us;

  if (g_recorded_channel_count < (sizeof(g_recorded_channels) / sizeof(g_recorded_channels[0]))) {
    g_recorded_channels[g_recorded_channel_count++] = channel;
  }

  if (g_stub_error_code != ADC_HAL_OK) {
    const bool is_reference_channel = (channel == AdcHalChannel::ADC_HAL_CHANNEL_0);
    if ((g_stub_fail_on_reference && is_reference_channel) || (!g_stub_fail_on_reference && !is_reference_channel)) {
      return g_stub_error_code;
    }
  }

  if (g_adc_should_assert_rail_enabled) {
    TEST_ASSERT_TRUE_MESSAGE(g_rail_enabled, "Rail must be enabled before sampling thermistors");
  }

  switch (channel) {
    case AdcHalChannel::ADC_HAL_CHANNEL_0:
      *code_out = g_stub_reference_code;
      break;
    case AdcHalChannel::ADC_HAL_CHANNEL_2:
      *code_out = g_stub_board_code;  // Gain stage (beta model)
      break;
    case AdcHalChannel::ADC_HAL_CHANNEL_3:
      *code_out = g_stub_board_code;  // LED drive stage (beta model)
      break;
    case AdcHalChannel::ADC_HAL_CHANNEL_4:
      *code_out = g_stub_water_code;  // Blue LED (Steinhart-Hart)
      break;
    case AdcHalChannel::ADC_HAL_CHANNEL_5:
      *code_out = g_stub_water_code;  // Green LED (Steinhart-Hart)
      break;
    case AdcHalChannel::ADC_HAL_CHANNEL_6:
      *code_out = g_stub_water_code;  // Sample (Steinhart-Hart)
      break;
    default:
      *code_out = 0;
      break;
  }

  return ADC_HAL_OK;
}

static int32_t compute_sensor_code_for_resistance(const ThermistorReaderConfig& config, float resistance_ohms,
                                                  int32_t reference_code) {
  const float pullup_ohms       = static_cast<float>(config.pullup_resistance_ohms);
  const float reference_ohms    = static_cast<float>(config.reference_resistance_ohms);
  const float divider_factor    = (pullup_ohms + reference_ohms) / reference_ohms;
  const float sensor_voltage    = (resistance_ohms / (pullup_ohms + resistance_ohms));
  const float reference_voltage = 1.0f / divider_factor;
  const float ratio             = sensor_voltage / reference_voltage;
  return static_cast<int32_t>(ratio * static_cast<float>(reference_code));
}

static void stage_board_resistance_codes(const ThermistorReaderConfig& config, float resistance_ohms) {
  const int32_t reference_code = 100000;
  g_stub_reference_code        = reference_code;
  g_stub_board_code            = compute_sensor_code_for_resistance(config, resistance_ohms, reference_code);
}

static void stage_water_resistance_codes(const ThermistorReaderConfig& config, float resistance_ohms) {
  const int32_t reference_code = 100000;
  g_stub_reference_code        = reference_code;
  g_stub_water_code            = compute_sensor_code_for_resistance(config, resistance_ohms, reference_code);
  g_stub_water_resistance      = static_cast<int32_t>(resistance_ohms);
}

static void thermistor_reader_pin_mode_stub(int pin, uint32_t mode) {
  (void) pin;
  (void) mode;
}

static void record_gpio_event(int pin, uint32_t value) {
  if (g_gpio_event_count < (sizeof(g_gpio_events) / sizeof(g_gpio_events[0]))) {
    g_gpio_events[g_gpio_event_count].pin   = pin;
    g_gpio_events[g_gpio_event_count].value = value;
  }
  g_gpio_event_count++;
}

static void thermistor_reader_digital_write_stub(int pin, uint32_t value) {
  record_gpio_event(pin, value);
  if ((g_expected_rail_pin >= 0) && (pin == g_expected_rail_pin)) {
    g_rail_enabled = (value != LOW);
  }
}

static void thermistor_reader_delay_stub(uint32_t delay_us) {
  g_delay_call_count++;
  g_last_delay_us = delay_us;
}

static float compute_expected_water_temperature(float resistance_ohms) {
  const float r_ratio  = resistance_ohms / 10000.0f;
  const float ln_ratio = logf(r_ratio);
  if (r_ratio >= 3.274f) {
    const float a         = 3.3538646e-03f;
    const float b         = 2.5654090e-04f;
    const float c         = 1.9243889e-06f;
    const float d         = 1.0969244e-07f;
    const float inverse_t = a + b * ln_ratio + c * ln_ratio * ln_ratio + d * ln_ratio * ln_ratio * ln_ratio;
    return (1.0f / inverse_t) - 273.15f;
  }

  if (r_ratio >= 0.36036f) {
    const float a         = 3.3540154e-03f;
    const float b         = 2.5627725e-04f;
    const float c         = 2.0829210e-06f;
    const float d         = 7.3003206e-08f;
    const float inverse_t = a + b * ln_ratio + c * ln_ratio * ln_ratio + d * ln_ratio * ln_ratio * ln_ratio;
    return (1.0f / inverse_t) - 273.15f;
  }

  if (r_ratio >= 0.06831f) {
    const float a         = 3.3539264e-03f;
    const float b         = 2.5609446e-04f;
    const float c         = 1.9621987e-06f;
    const float d         = 4.6045930e-08f;
    const float inverse_t = a + b * ln_ratio + c * ln_ratio * ln_ratio + d * ln_ratio * ln_ratio * ln_ratio;
    return (1.0f / inverse_t) - 273.15f;
  }

  const float a         = 3.3368620e-03f;
  const float b         = 2.4057263e-04f;
  const float c         = -2.6687093e-06f;
  const float d         = -4.0719355e-07f;
  const float inverse_t = a + b * ln_ratio + c * ln_ratio * ln_ratio + d * ln_ratio * ln_ratio * ln_ratio;
  return (1.0f / inverse_t) - 273.15f;
}

static ThermistorReaderConfig make_test_config(void) {
  ThermistorReaderConfig config = {};
  config.reference_channel      = AdcHalChannel::ADC_HAL_CHANNEL_0;

  // Configure sensors array for the 5 Tornado thermistors
  // Sample thermistor (ch6) - Steinhart-Hart
  config.sensors[0].channel              = AdcHalChannel::ADC_HAL_CHANNEL_6;
  config.sensors[0].model                = ThermistorModel::THERMISTOR_MODEL_STEINHART_HART;
  config.sensors[0].r25_ohms             = 10000.0f;
  config.sensors[0].calibration_offset_c = 0.0f;

  // Blue LED thermistor (ch4) - Steinhart-Hart
  config.sensors[1].channel              = AdcHalChannel::ADC_HAL_CHANNEL_4;
  config.sensors[1].model                = ThermistorModel::THERMISTOR_MODEL_STEINHART_HART;
  config.sensors[1].r25_ohms             = 10000.0f;
  config.sensors[1].calibration_offset_c = 0.0f;

  // Green LED thermistor (ch5) - Steinhart-Hart
  config.sensors[2].channel              = AdcHalChannel::ADC_HAL_CHANNEL_5;
  config.sensors[2].model                = ThermistorModel::THERMISTOR_MODEL_STEINHART_HART;
  config.sensors[2].r25_ohms             = 10000.0f;
  config.sensors[2].calibration_offset_c = 0.0f;

  // Gain stage thermistor (ch2) - Beta
  config.sensors[3].channel              = AdcHalChannel::ADC_HAL_CHANNEL_2;
  config.sensors[3].model                = ThermistorModel::THERMISTOR_MODEL_BETA;
  config.sensors[3].r25_ohms             = 10000.0f;
  config.sensors[3].calibration_offset_c = 0.0f;

  // LED drive stage thermistor (ch3) - Beta
  config.sensors[4].channel              = AdcHalChannel::ADC_HAL_CHANNEL_3;
  config.sensors[4].model                = ThermistorModel::THERMISTOR_MODEL_BETA;
  config.sensors[4].r25_ohms             = 10000.0f;
  config.sensors[4].calibration_offset_c = 0.0f;

  config.rail_enable_pin           = 11;
  config.pullup_resistance_ohms    = 10000u;
  config.reference_resistance_ohms = 10000u;
  config.adc_timeout_us            = 1000u;
  config.settle_time_us            = 100u;
  config.beta_constant             = 3380.0f;
  return config;
}

void setUp(void) {
  thermistor_reader_reset_for_test();
  reset_adc_stub_state();
  reset_gpio_stub_state();
}

void tearDown(void) {
  thermistor_reader_reset_for_test();
  reset_adc_stub_state();
  reset_gpio_stub_state();
}

static void test_thermistor_reader_initialize_rejects_null_config(void) {
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG, thermistor_reader_initialize(NULL));
}

static void test_thermistor_reader_initialize_caches_config_values(void) {
  ThermistorReaderConfig config = make_test_config();

  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));

  ThermistorReaderConfig cached = {};
  thermistor_reader_get_config_for_test(&cached);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.reference_channel), static_cast<int>(cached.reference_channel));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[0].channel), static_cast<int>(cached.sensors[0].channel));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[1].channel), static_cast<int>(cached.sensors[1].channel));
  TEST_ASSERT_EQUAL_INT(config.rail_enable_pin, cached.rail_enable_pin);
  TEST_ASSERT_EQUAL_UINT32(config.adc_timeout_us, cached.adc_timeout_us);
}

static void test_thermistor_reader_measure_rejects_null_output(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));

  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, NULL));
}

static void test_thermistor_reader_measure_errors_when_uninitialized(void) {
  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_NOT_INITIALIZED,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));
}

static void test_thermistor_reader_measure_reads_reference_before_sensor(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_SAMPLE, &temperature_c));

  TEST_ASSERT_EQUAL_UINT32(2u, g_recorded_channel_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.reference_channel), static_cast<int>(g_recorded_channels[0]));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[0].channel), static_cast<int>(g_recorded_channels[1]));

  g_recorded_channel_count = 0u;
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));

  TEST_ASSERT_EQUAL_UINT32(2u, g_recorded_channel_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.reference_channel), static_cast<int>(g_recorded_channels[0]));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[3].channel), static_cast<int>(g_recorded_channels[1]));
}

static void test_thermistor_reader_measure_propagates_adc_failures(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  g_stub_error_code        = ADC_HAL_ERR_TIMEOUT;
  g_stub_fail_on_reference = true;

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(ADC_HAL_ERR_TIMEOUT,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));
}

static void test_thermistor_reader_measure_computes_resistance_from_ratio(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  stage_board_resistance_codes(config, 10000.0f);

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));

  float resistance_ohms = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_get_last_resistance_for_test(
                                                  ThermistorId::THERMISTOR_ID_GAIN_STAGE, &resistance_ohms));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 10000.0f, resistance_ohms);
}

static void test_gain_stage_thermistor_reports_expected_temperature_at_25c(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  stage_board_resistance_codes(config, config.sensors[3].r25_ohms);

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 25.0f, temperature_c);
}

static void test_gain_stage_thermistor_reports_expected_temperature_above_room(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  stage_board_resistance_codes(config, 6500.0f);

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 36.8f, temperature_c);
}

static void test_gain_stage_thermistor_rejects_non_positive_resistance(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  g_stub_reference_code = 100000;
  g_stub_board_code     = 0;

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_COMPUTE_FAILURE,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));
}

static void test_led_drive_stage_thermistor_reports_expected_temperature_at_25c(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  stage_board_resistance_codes(config, config.sensors[4].r25_ohms);

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_LED_DRIVE_STAGE, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 25.0f, temperature_c);
}

static void test_sample_thermistor_reports_expected_temperature_in_high_range(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  stage_water_resistance_codes(config, 50000.0f);  // Use a ratio within the highest range

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_SAMPLE, &temperature_c));
  const float expected_temperature = compute_expected_water_temperature(50000.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, expected_temperature, temperature_c);
}

static void test_sample_thermistor_reports_expected_temperature_in_mid_range(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  stage_water_resistance_codes(config, 2000.0f);  // falls in second table row

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_SAMPLE, &temperature_c));
  const float expected_temperature = compute_expected_water_temperature(2000.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, expected_temperature, temperature_c);
}

static void test_sample_thermistor_validates_ratio_bounds(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  g_stub_reference_code = 100000;
  g_stub_water_code     = 0;

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_COMPUTE_FAILURE,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_SAMPLE, &temperature_c));
}

static void test_blue_led_thermistor_reports_expected_temperature(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  stage_water_resistance_codes(config, 10000.0f);  // R25 value

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_BLUE_LED, &temperature_c));
  const float expected_temperature = compute_expected_water_temperature(10000.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, expected_temperature, temperature_c);
}

static void test_green_led_thermistor_reports_expected_temperature(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  stage_water_resistance_codes(config, 10000.0f);  // R25 value

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GREEN_LED, &temperature_c));
  const float expected_temperature = compute_expected_water_temperature(10000.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, expected_temperature, temperature_c);
}

static void test_measure_asserts_power_enable_before_sampling(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);
  thermistor_reader_set_gpio_hooks_for_test(thermistor_reader_pin_mode_stub, thermistor_reader_digital_write_stub,
                                            thermistor_reader_delay_stub);

  g_expected_rail_pin              = config.rail_enable_pin;
  g_adc_should_assert_rail_enabled = true;
  stage_board_resistance_codes(config, config.sensors[3].r25_ohms);

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));

  TEST_ASSERT_TRUE(g_gpio_event_count > 0u);
  TEST_ASSERT_EQUAL_INT(g_expected_rail_pin, g_gpio_events[0].pin);
  TEST_ASSERT_EQUAL_UINT32(HIGH, g_gpio_events[0].value);
  const size_t last_index = g_gpio_event_count - 1u;
  TEST_ASSERT_EQUAL_INT(g_expected_rail_pin, g_gpio_events[last_index].pin);
  TEST_ASSERT_EQUAL_UINT32(LOW, g_gpio_events[last_index].value);
  TEST_ASSERT_FALSE(g_rail_enabled);
}

static void test_measure_respects_settle_delay_configuration(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);
  thermistor_reader_set_gpio_hooks_for_test(thermistor_reader_pin_mode_stub, thermistor_reader_digital_write_stub,
                                            thermistor_reader_delay_stub);

  g_expected_rail_pin = config.rail_enable_pin;
  stage_board_resistance_codes(config, config.sensors[3].r25_ohms);

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));

  TEST_ASSERT_EQUAL_UINT32(1u, g_delay_call_count);
  TEST_ASSERT_EQUAL_UINT32(config.settle_time_us, g_last_delay_us);
}

static void test_measure_powers_down_on_reference_failure(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);
  thermistor_reader_set_gpio_hooks_for_test(thermistor_reader_pin_mode_stub, thermistor_reader_digital_write_stub,
                                            thermistor_reader_delay_stub);

  g_expected_rail_pin              = config.rail_enable_pin;
  g_adc_should_assert_rail_enabled = true;
  g_stub_error_code                = ADC_HAL_ERR_TIMEOUT;
  g_stub_fail_on_reference         = true;

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(ADC_HAL_ERR_TIMEOUT,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));

  TEST_ASSERT_TRUE(g_gpio_event_count > 0u);
  const size_t last_index = g_gpio_event_count - 1u;
  TEST_ASSERT_EQUAL_UINT32(LOW, g_gpio_events[last_index].value);
  TEST_ASSERT_FALSE(g_rail_enabled);
}

static void test_measure_all_samples_sample_thermistor_first(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  g_stub_reference_code = 100000;
  g_stub_water_code     = 100000;
  g_stub_board_code     = 100000;

  ThermistorSweepResult result = {};
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_measure_all(&result));

  // Verify sample thermistor (ch6) was sampled first after reference
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2u, g_recorded_channel_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.reference_channel), static_cast<int>(g_recorded_channels[0]));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[0].channel), static_cast<int>(g_recorded_channels[1]));
}

static void test_measure_all_reads_reference_channel_once_per_cycle(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  g_stub_reference_code = 100000;
  g_stub_water_code     = 100000;
  g_stub_board_code     = 100000;

  ThermistorSweepResult result = {};
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_measure_all(&result));

  // Count reference channel samples - should be exactly 1
  size_t reference_count = 0u;
  for (size_t i = 0u; i < g_recorded_channel_count; ++i) {
    if (g_recorded_channels[i] == config.reference_channel) {
      reference_count++;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, reference_count);
  TEST_ASSERT_EQUAL_INT32(100000, result.reference_code);
}

static void test_measure_all_covers_every_sensor(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  g_stub_reference_code = 100000;
  g_stub_water_code     = 100000;
  g_stub_board_code     = 100000;

  ThermistorSweepResult result = {};
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_measure_all(&result));

  // All 5 sensors should be valid
  for (size_t i = 0u; i < 5u; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(result.valid[i], "All sensors should be valid");
  }
}

static void test_measure_all_pulses_rail_once(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);
  thermistor_reader_set_gpio_hooks_for_test(thermistor_reader_pin_mode_stub, thermistor_reader_digital_write_stub,
                                            thermistor_reader_delay_stub);

  g_expected_rail_pin   = config.rail_enable_pin;
  g_stub_reference_code = 100000;
  g_stub_water_code     = 100000;
  g_stub_board_code     = 100000;

  ThermistorSweepResult result = {};
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_measure_all(&result));

  // Should have exactly 2 GPIO events: HIGH then LOW
  TEST_ASSERT_EQUAL_UINT32(2u, g_gpio_event_count);
  TEST_ASSERT_EQUAL_INT(g_expected_rail_pin, g_gpio_events[0].pin);
  TEST_ASSERT_EQUAL_UINT32(HIGH, g_gpio_events[0].value);
  TEST_ASSERT_EQUAL_INT(g_expected_rail_pin, g_gpio_events[1].pin);
  TEST_ASSERT_EQUAL_UINT32(LOW, g_gpio_events[1].value);
}

static void test_measure_celsius_supports_new_ids(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  g_stub_reference_code = 100000;
  g_stub_water_code     = 100000;
  g_stub_board_code     = 100000;

  // Test each new ThermistorId
  float temperature_c = 0.0f;

  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_SAMPLE, &temperature_c));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[0].channel), static_cast<int>(g_recorded_channels[1]));

  g_recorded_channel_count = 0u;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_BLUE_LED, &temperature_c));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[1].channel), static_cast<int>(g_recorded_channels[1]));

  g_recorded_channel_count = 0u;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GREEN_LED, &temperature_c));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[2].channel), static_cast<int>(g_recorded_channels[1]));

  g_recorded_channel_count = 0u;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[3].channel), static_cast<int>(g_recorded_channels[1]));

  g_recorded_channel_count = 0u;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_LED_DRIVE_STAGE, &temperature_c));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(config.sensors[4].channel), static_cast<int>(g_recorded_channels[1]));
}

static void test_reference_divider_ratio_near_half_scale(void) {
  ThermistorReaderConfig config = make_test_config();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_stub);

  // Set reference code to simulate 10k/10k divider (should be ~half scale)
  g_stub_reference_code = 50000;  // Half of full scale
  g_stub_water_code     = 50000;

  ThermistorSweepResult result = {};
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_measure_all(&result));

  // Reference code should be captured
  TEST_ASSERT_EQUAL_INT32(50000, result.reference_code);
}

int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_thermistor_reader_initialize_rejects_null_config);
  RUN_TEST(test_thermistor_reader_initialize_caches_config_values);
  RUN_TEST(test_thermistor_reader_measure_rejects_null_output);
  RUN_TEST(test_thermistor_reader_measure_errors_when_uninitialized);
  RUN_TEST(test_thermistor_reader_measure_reads_reference_before_sensor);
  RUN_TEST(test_thermistor_reader_measure_propagates_adc_failures);
  RUN_TEST(test_thermistor_reader_measure_computes_resistance_from_ratio);
  RUN_TEST(test_gain_stage_thermistor_reports_expected_temperature_at_25c);
  RUN_TEST(test_gain_stage_thermistor_reports_expected_temperature_above_room);
  RUN_TEST(test_gain_stage_thermistor_rejects_non_positive_resistance);
  RUN_TEST(test_led_drive_stage_thermistor_reports_expected_temperature_at_25c);
  RUN_TEST(test_measure_asserts_power_enable_before_sampling);
  RUN_TEST(test_measure_respects_settle_delay_configuration);
  RUN_TEST(test_measure_powers_down_on_reference_failure);
  RUN_TEST(test_sample_thermistor_reports_expected_temperature_in_high_range);
  RUN_TEST(test_sample_thermistor_reports_expected_temperature_in_mid_range);
  RUN_TEST(test_sample_thermistor_validates_ratio_bounds);
  RUN_TEST(test_blue_led_thermistor_reports_expected_temperature);
  RUN_TEST(test_green_led_thermistor_reports_expected_temperature);
  RUN_TEST(test_measure_all_samples_sample_thermistor_first);
  RUN_TEST(test_measure_all_reads_reference_channel_once_per_cycle);
  RUN_TEST(test_measure_all_covers_every_sensor);
  RUN_TEST(test_measure_all_pulses_rail_once);
  RUN_TEST(test_measure_celsius_supports_new_ids);
  RUN_TEST(test_reference_divider_ratio_near_half_scale);
  return UNITY_END();
}

void setup(void) {
  UNITY_SETUP_SERIAL_DEFAULT();
  delay(200);
  runUnityTests();
}

void loop(void) {
}
