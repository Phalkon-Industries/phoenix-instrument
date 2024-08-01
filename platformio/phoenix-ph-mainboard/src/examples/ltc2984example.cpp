/**
 * @file ltc2984example.cpp
 * @author Jonathan Pfeifer
 * @brief Example code for the LTC2984 which measures thermister on first channel and prints to serial.
 * @version 0.1
 * @date 2023-12-23
 *
 * @copyright Copyright (c) 2023
 *
 * Expects a Differential input setup with 10k sense resistor on CH1,2 and 10k thermistor on CH3,4 with CH2 and CH3 tied together as per the datasheet example.
 * Assuming an MC65F thermistor from Amphenol is being used
 */
#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_TinyUSB.h"
#include "ltc2984.hpp"
#define LTC2984_CS_PIN 11
CustomSteinhart custom_steinhart = {

    // Each coefficient is in 32-bit float format
    .A = 0.00113812515718026,
    .B = 0.000228147998658418,
    .C = 9.92548258325898E-07,
    .D = 3.88732553543688E-08};

ChannelConfig_u sense_channel_config = {.sense = {
                                            .resistor_value = 0b000100111000100000000000000,
                                            .sense_resistor = 0b11101}};
ChannelConfig_u therm_channel_config = {.therm = {
                                            .steinhart_length = 0x000000,
                                            .steinhart_address = 0x000000,
                                            .empty = 0b000,
                                            .excitation_current = 0b0101,
                                            .excitation_mode = 0b01,
                                            .single_ended = 0b0,
                                            .r_sense_channel = 0b00011,
                                            .thermistor_type = 0b11010}};
uint8_t therm_channel = 5;
uint8_t sense_channel = 3;

void setup()
{

    // Start up Serial and SPI
    SPI.begin();
    Serial.begin(115200);
    pinMode(LTC2984_CS_PIN, OUTPUT);
    digitalWrite(LTC2984_CS_PIN, HIGH);
    // Config LTC2984
    ltc2984_init(therm_channel_config, sense_channel_config, therm_channel, sense_channel, custom_steinhart);
    ltc2984_get_temperature(therm_channel);
}
void loop()
{
    // get temperature every second and print to serial
    float read_temp = ltc2984_get_temperature(therm_channel);
    Serial.print("Temperature: ");
    Serial.println(read_temp);
    delay(1000);
}