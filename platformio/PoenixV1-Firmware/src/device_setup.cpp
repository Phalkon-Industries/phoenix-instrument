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
    {LedRouterState::LED_ROUTER_STATE_LED1, AdcHalChannel::ADC_HAL_CHANNEL_4, 100u, 0xDEu},  // Blue LED
    {LedRouterState::LED_ROUTER_STATE_LED2, AdcHalChannel::ADC_HAL_CHANNEL_5, 100u, 0xB0u},  // Green LED
    1000000u,
};
