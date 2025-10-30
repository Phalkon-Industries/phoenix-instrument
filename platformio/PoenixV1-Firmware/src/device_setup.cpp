#include "device_setup.hpp"

#include "main.hpp"

const LedRouterConfig g_device_led_router_config = {
    TS5A3359_IN1,
    TS5A3359_IN2,
};

const AdcHalConfig g_device_adc_hal_config = {
    PIN_ADC_CS,
    500000UL,
    PIN_ADC_IRQ,
};

const LightReadingsConfig g_device_light_readings_config = {
    LedRouterState::LED_ROUTER_STATE_DRAIN,                                                  // Drain
    {LedRouterState::LED_ROUTER_STATE_LED1, AdcHalChannel::ADC_HAL_CHANNEL_0, 100u, 0xD8u},  // Blue LED
    {LedRouterState::LED_ROUTER_STATE_LED2, AdcHalChannel::ADC_HAL_CHANNEL_1, 100u, 0xA7u},  // Green LED
    1000000u,
};
