#ifndef DEVICE_SETTINGS_HPP
#define DEVICE_SETTINGS_HPP

#include "ad524x.hpp"
#include "adc_hal.hpp"
#include "led_router.hpp"
#include "light_readings.hpp"
#include "mcp356x.hpp"
#include "power_control.hpp"
#include "thermistor_reader.hpp"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <variant.h>  //Pin definitions and stuff

// AD5242 address pins (AD0/AD1) are strapped low on Phoenix hardware, yielding
// the 7-bit I2C address 0x2C (binary 0101100). Future variants should update the
// constant and associated documentation if the strap configuration changes.
#define AD5242_I2C_ADDRESS 0x2Cu

// ADC Chip select (MCP3564)
#define PIN_ADC_CS 13

// 5V Power enable
#define PIN_ENABLE_5V_POWER 12

// Negative Bias Shutdown
#define PIN_NEG_BIAS_SHUTDOWN 10

// Thermistor control
#define PIN_THERMISTOR_ON 11

// IRQ pin
#define PIN_ADC_IRQ 9

// LED path control
#define TS5A3359_IN1 17
#define TS5A3359_IN2 18
/* Function table for TS5A3359 (IN2, IN1 order)
L   L   Off
L   H   NO0 (Green LED)
H   L   NO1 (Blue LED)
H   H   NO2 (Drain)
*/

extern const LedRouterConfig        g_device_led_router_config;
extern const LedRouterPwmConfig     g_device_led_router_pwm_backend;
extern const AdcHalConfig           g_device_adc_hal_config;
extern mcp356x_settings             g_device_mcp356x_settings;
extern const LightReadingsConfig    g_device_light_readings_config;
extern const PowerControlConfig     g_device_power_control_config;
extern const ThermistorReaderConfig g_device_thermistor_reader_config;

int device_setup_initialize(void);

#endif  // DEVICE_SETTINGS_HPP
