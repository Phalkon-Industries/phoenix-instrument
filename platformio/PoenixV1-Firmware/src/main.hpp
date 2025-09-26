// main.hpp
// Pin definitions for PhoenixV1-Firmware

#ifndef MAIN_HPP
#define MAIN_HPP

#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <variant.h> //Pin definitions and stuff

// ADC Chip select (MCP3564)
#define PIN_ADC_CS 13

// Power enable
#define PIN_ENABLE_POWER 12

// Thermistor control
#define PIN_THERMISTOR_ON 11

// IRQ pin
#define PIN_IRQ 9

// LED path control
#define PIN_IN1 17
#define PIN_IN2 18

#endif  // MAIN_HPP