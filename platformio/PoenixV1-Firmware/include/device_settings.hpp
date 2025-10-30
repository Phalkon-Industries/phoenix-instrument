#ifndef DEVICE_SETTINGS_HPP
#define DEVICE_SETTINGS_HPP

#include "adc_hal.hpp"
#include "led_router.hpp"
#include "light_readings.hpp"
#include "main.hpp"

// Shared wiring definitions used by firmware modules and tests.
inline constexpr LedRouterConfig k_device_led_router_config = {
    TS5A3359_IN1,
    TS5A3359_IN2,
};

// Default ADC HAL settings matching the Phoenix benchmark baseline configuration.
inline constexpr AdcHalConfig k_device_adc_hal_config = {
    .chip_select_pin = PIN_ADC_CS,
    .spi_clock_hz    = 500000UL,
    .irq_pin         = PIN_ADC_IRQ,
};

// Canonical light readings configuration reusing the project-wide LED routing and ADC mapping.
inline constexpr LightReadingsConfig k_device_light_readings_config = {
    .drain_state     = LedRouterState::LED_ROUTER_STATE_DRAIN,
    .channel_a_state = LedRouterState::LED_ROUTER_STATE_LED1,
    .channel_b_state = LedRouterState::LED_ROUTER_STATE_LED2,
    .channel_a_adc   = AdcHalChannel::ADC_HAL_CHANNEL_0,
    .channel_b_adc   = AdcHalChannel::ADC_HAL_CHANNEL_1,
    .adc_timeout_us  = 1000000u,
};

#endif  // DEVICE_SETTINGS_HPP
