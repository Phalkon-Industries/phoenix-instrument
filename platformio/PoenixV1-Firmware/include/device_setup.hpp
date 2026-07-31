#ifndef DEVICE_SETTINGS_HPP
#define DEVICE_SETTINGS_HPP

#include "ad524x.hpp"
#include "adc_hal.hpp"
#include "led_router.hpp"
#include "light_readings.hpp"
#include "mcp356x.hpp"
#include "phoenix_settings.hpp"
#include "power_control.hpp"
#include "thermistor_reader.hpp"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <variant.h>  //Pin definitions and stuff

// AD5242 address pins: AD1 (A1) strapped low, AD0 (A0) strapped high on Tornado hardware,
// yielding the 7-bit I2C address 0x2D (binary 0101101). The MCP41U83T occupies 0x2C
// (A1=0, A0=0) on the same bus.
#define AD5242_I2C_ADDRESS 0x2Du

// MCP41U83T address pins (A1/A0) are both strapped low on Tornado hardware,
// yielding the 7-bit I2C address 0x2C (binary 0101100).
#define MCP41U83_I2C_ADDRESS 0x2Cu

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

// ===================== MCP3564 Channel Map (V1.1.0 Tornado) =====================
// ADC channel assignments for the MCP3564 on the Tornado board
// ch0 = blue LED photodiode signal
// ch1 = green LED photodiode signal
// ch2 = gain-stage thermistor
// ch3 = LED-drive-stage thermistor
// ch4 = blue LED thermistor
// ch5 = green LED thermistor
// ch6 = experimental sample thermistor (measured first in sweep to limit self-heating)
// ch7 = reference signal (10k/10k voltage divider for 3.3V rail drift tracking)

// LED path control
#define TS5A3359_IN1 17
#define TS5A3359_IN2 18
/* Function table for TS5A3359 (IN2, IN1 order)
L   L   Off
L   H   NO0 (Green LED)
H   L   NO1 (Blue LED)
H   H   NO2 (Drain)
*/

// ===================== Default Settings ======================================
// Factory default settings used at boot when flash is empty or corrupt.
// These values are persisted to flash by phoenix_settings on first run.
#define PHOENIX_DEFAULT_BLUE_WIPER 0xFFu
#define PHOENIX_DEFAULT_GREEN_WIPER 0xD3u

extern const LedRouterConfig        g_device_led_router_config;
extern const LedRouterPwmConfig     g_device_led_router_pwm_backend;
extern const AdcHalConfig           g_device_adc_hal_config;
extern mcp356x_settings             g_device_mcp356x_settings;
extern const LightReadingsConfig    g_device_light_readings_config;
extern const PowerControlConfig     g_device_power_control_config;
extern const ThermistorReaderConfig g_device_thermistor_reader_config;

int device_setup_initialize(void);

#endif  // DEVICE_SETTINGS_HPP
