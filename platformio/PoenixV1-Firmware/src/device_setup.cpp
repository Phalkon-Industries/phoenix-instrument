#include "device_setup.hpp"

const LedRouterConfig g_device_led_router_config = {
    TS5A3359_IN1,
    TS5A3359_IN2,
    {false, nullptr},
};

const AdcHalConfig g_device_adc_hal_config = {
    PIN_ADC_CS,
    500000UL,
    PIN_ADC_IRQ,
};

const LightReadingsConfig g_device_light_readings_config = {
    LedRouterState::LED_ROUTER_STATE_DRAIN,                                                  // Drain
    {LedRouterState::LED_ROUTER_STATE_LED1, AdcHalChannel::ADC_HAL_CHANNEL_4, 100u, 0xDEu},  // Blue LED
    {LedRouterState::LED_ROUTER_STATE_LED2, AdcHalChannel::ADC_HAL_CHANNEL_5, 100u, 0xB0u},  // Green LED
    1000000u,
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
  int return_code = power_control_prepare_power_domains(&g_device_power_control_config);
  if (return_code != POWER_CONTROL_OK) {
    return return_code;
  }

  // Step 2: Bring the light readings helper online so batches can run immediately.
  return_code = light_readings_initialize(&g_device_light_readings_config);
  if (return_code != LIGHT_READINGS_OK) {
    return return_code;
  }

  light_readings_ready = true;
  return LIGHT_READINGS_OK;
}
