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
    500000UL,
    PIN_ADC_IRQ,
};

const LightReadingsConfig g_device_light_readings_config = {
    LedRouterState::LED_ROUTER_STATE_DRAIN,                                                   // Drain
    {LedRouterState::LED_ROUTER_STATE_GREEN, AdcHalChannel::ADC_HAL_CHANNEL_4, 100u, 0xDEu},  // Blue LED
    {LedRouterState::LED_ROUTER_STATE_BLUE, AdcHalChannel::ADC_HAL_CHANNEL_5, 100u, 0xB0u},   // Green LED
    1000000u,
    {true, NRF_PWM3, TS5A3359_IN1, TS5A3359_IN2, 30000u, 100000u},  // PWM configuration
};

const PowerControlConfig g_device_power_control_config = {
    &g_device_led_router_config, &g_device_adc_hal_config, &Wire, AD5242_I2C_ADDRESS, PIN_ENABLE_POWER, -1, -1,
};

int device_setup_initialize(void) {
  static bool light_readings_ready = false;

  if (light_readings_ready) {
    return LIGHT_READINGS_OK;
  }

  // Step 1: Energise shared power domains and initialise peripheral drivers.
  GUARD(power_control_prepare_power_domains(&g_device_power_control_config));

  // Step 2: Bring the light readings helper online so batches can run immediately.
  GUARD(light_readings_initialize(&g_device_light_readings_config));

  light_readings_ready = true;
  return LIGHT_READINGS_OK;
}
