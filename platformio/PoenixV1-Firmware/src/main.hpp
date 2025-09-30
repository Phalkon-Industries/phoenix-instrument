// main.hpp
// Pin definitions for PhoenixV1-Firmware

#ifndef MAIN_HPP
#define MAIN_HPP

#include "mcp356x.hpp"
#include <AD524x.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <SPI.h>
#include <variant.h>  //Pin definitions and stuff

// ADC Chip select (MCP3564)
#define PIN_ADC_CS 13

// Power enable
#define PIN_ENABLE_POWER 12

// Thermistor control
#define PIN_THERMISTOR_ON 11

// IRQ pin
#define PIN_ADC_IRQ 9

// LED path control
#define TS5A3359_IN1 17
#define TS5A3359_IN2 18
/* Function table for TS5A3359
IN2 IN1 OUT
L   L   Off
L   H   NO0(LED1)
H   L   NO1(LED2)
H   H   NO2(Drain)
*/

#endif  // MAIN_HPP