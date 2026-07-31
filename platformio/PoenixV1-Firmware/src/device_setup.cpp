#include "device_setup.hpp"

#include <nrf_pwm.h>

const LedRouterPwmConfig g_device_led_router_pwm_backend = {
    true,
    NRF_PWM3,
};
const LedRouterConfig g_device_led_router_config = {
    TS5A3359_IN1,
    TS5A3359_IN2,
    g_device_led_router_pwm_backend,
};

const AdcHalConfig g_device_adc_hal_config = {
    PIN_ADC_CS,
    1000000UL,
    PIN_ADC_IRQ,
};

mcp356x_settings g_device_mcp356x_settings = {
    mcp356x_gain::gain_x1,
    mcp356x_osr::osr_32,
    mcp356x_prescaler::mclk_div1,
    mcp356x_conversion_mode::oneshot_shutdown,
    mcp356x_data_format::data24,
    mcp356x_irq_mode::irq_push_pull,
    true,
    false,
};

static const uint32_t k_light_readings_pwm_minimum_period_us = 10000u;
static const uint32_t k_light_readings_pwm_period_timeout_us = 100000u;

const LightReadingsConfig g_device_light_readings_config = {
    LedRouterState::LED_ROUTER_STATE_DRAIN,
    {LedRouterState::LED_ROUTER_STATE_GREEN, AdcHalChannel::ADC_HAL_CHANNEL_4, 100u, PHOENIX_DEFAULT_GREEN_WIPER},
    {LedRouterState::LED_ROUTER_STATE_BLUE, AdcHalChannel::ADC_HAL_CHANNEL_5, 100u, PHOENIX_DEFAULT_BLUE_WIPER},
    1000000u,
    {true, NRF_PWM3, TS5A3359_IN1, TS5A3359_IN2, k_light_readings_pwm_minimum_period_us,
     k_light_readings_pwm_period_timeout_us},
};

const PowerControlConfig g_device_power_control_config = {
    &g_device_led_router_config, &g_device_adc_hal_config, &Wire, AD5242_I2C_ADDRESS,
    PIN_ENABLE_5V_POWER,         PIN_NEG_BIAS_SHUTDOWN,    -1,    -1,
};

const ThermistorReaderConfig g_device_thermistor_reader_config = {
    AdcHalChannel::ADC_HAL_CHANNEL_7,  // reference divider (10k/10k)
    {
        // THERMISTOR_ID_SAMPLE (ch6) - Steinhart-Hart
        {AdcHalChannel::ADC_HAL_CHANNEL_6, ThermistorModel::THERMISTOR_MODEL_STEINHART_HART, 10000.0f, 0.0f},
        // THERMISTOR_ID_BLUE_LED (ch4) - Steinhart-Hart
        {AdcHalChannel::ADC_HAL_CHANNEL_4, ThermistorModel::THERMISTOR_MODEL_STEINHART_HART, 10000.0f, 0.0f},
        // THERMISTOR_ID_GREEN_LED (ch5) - Steinhart-Hart
        {AdcHalChannel::ADC_HAL_CHANNEL_5, ThermistorModel::THERMISTOR_MODEL_STEINHART_HART, 10000.0f, 0.0f},
        // THERMISTOR_ID_GAIN_STAGE (ch2) - Beta
        {AdcHalChannel::ADC_HAL_CHANNEL_2, ThermistorModel::THERMISTOR_MODEL_BETA, 10000.0f, 0.0f},
        // THERMISTOR_ID_LED_DRIVE_STAGE (ch3) - Beta
        {AdcHalChannel::ADC_HAL_CHANNEL_3, ThermistorModel::THERMISTOR_MODEL_BETA, 10000.0f, 0.0f},
    },
    PIN_THERMISTOR_ON,
    10000u,
    10000u,
    100000u,
    2000u,
    3380.0f,
};

// Default settings applied when flash is empty or corrupt.
static const PhoenixSettings k_default_settings = {
    PHOENIX_DEFAULT_BLUE_WIPER,
    PHOENIX_DEFAULT_GREEN_WIPER,
    {0},  // reserved
};

int device_setup_initialize(void) {
  static bool g_device_setup_ready = false;

  if (g_device_setup_ready) {
    return LIGHT_READINGS_OK;
  }

  // Step 1: Energise shared power domains and initialise peripheral drivers.
  GUARD(power_control_prepare_power_domains(&g_device_power_control_config));

  // Step 2: Initialize settings storage and load calibrated wiper codes from flash.
  GUARD(phoenix_settings_initialize(&k_default_settings));

  // Step 3: Apply the caller-configurable MCP356x settings so ADC timing reflects the requested profile.
  GUARD(mcp356x_apply_settings(&g_device_mcp356x_settings));

  // Step 4: Bring the light readings helper online so batches can run immediately.
  GUARD(light_readings_initialize(&g_device_light_readings_config));

  // Step 5: Apply calibrated wiper codes from settings to the digipot hardware.
  GUARD(phoenix_settings_apply_wiper_codes());

  // Step 6: Stage the thermistor reader so sample commands can capture enclosure and water temperatures.
  GUARD(thermistor_reader_initialize(&g_device_thermistor_reader_config));

  g_device_setup_ready = true;
  return LIGHT_READINGS_OK;
}
