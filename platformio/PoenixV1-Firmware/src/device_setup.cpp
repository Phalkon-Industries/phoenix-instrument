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
    LedRouterState::LED_ROUTER_STATE_DRAIN, LedRouterState::LED_ROUTER_STATE_LED1,
    LedRouterState::LED_ROUTER_STATE_LED2,  AdcHalChannel::ADC_HAL_CHANNEL_0,
    AdcHalChannel::ADC_HAL_CHANNEL_1,       1000000u,
};
