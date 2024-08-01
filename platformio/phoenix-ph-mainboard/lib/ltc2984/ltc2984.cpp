#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_TinyUSB.h"
#include "ltc2984.hpp"
#include <string.h>
#define LTC2984_WRITE 0x02
#define LTC2984_READ 0x03

SPISettings ltc2984SPI(1000000, MSBFIRST, SPI_MODE0); // Adjust as needed

uint8_t initialized_therm_channel;
uint8_t initialized_sense_channel;

void ltc2984_write(uint16_t reg, uint32_t databyte, uint size)
{
    // Write to LTC2984 config register
    digitalWrite(LTC2984_CS_PIN, LOW);
    SPI.beginTransaction(ltc2984SPI);
    SPI.transfer(0x02);  // Write instruction
    SPI.transfer16(reg); // Address
    for (uint i = 0; i < size; i++)
    {
        SPI.transfer((databyte >> 8 * (size - i - 1)) & 0xFF);
    }

    SPI.endTransaction();
    digitalWrite(LTC2984_CS_PIN, HIGH);
};
uint32_t ltc2984_read(uint16_t reg, uint size)
{

    uint32_t return_data = 0;
    // read from LTC2984 config register
    digitalWrite(LTC2984_CS_PIN, LOW);
    SPI.beginTransaction(ltc2984SPI);
    SPI.transfer(0x03); // Read instruction
    SPI.transfer16(reg);
    for (uint8_t i = 0; i < size; i++)
    {

        return_data |= (SPI.transfer(0xFF) << 8 * (size - i - 1));
    }
    SPI.endTransaction();
    digitalWrite(LTC2984_CS_PIN, HIGH);
    return return_data;
};

// Writes the byte of data to the register using the SPI bus
void ltc2984_write_byte(uint16_t reg, uint8_t databyte)
{
    ltc2984_write(reg, databyte, 1);
};

// Reads and returns the byte read  from the register of the ltc2984
uint8_t ltc2984_read_byte(uint16_t reg)
{
    return ltc2984_read(reg, 1);
};

// Writes the byte of data to the register using the SPI bus
void ltc2984_write_16(uint16_t reg, uint16_t databyte)
{
    ltc2984_write(reg, databyte, 2);
};

// Reads and returns the byte read  from the register of the ltc2984
uint16_t ltc2984_read_16(uint16_t reg)
{
    return ltc2984_read(reg, 2);
};

// Writes the byte of data to the register using the SPI bus
void ltc2984_write_32(uint16_t reg, uint32_t databyte)
{
    ltc2984_write(reg, databyte, 4);
};

// Reads and returns the byte read  from the register of the ltc2984
uint32_t ltc2984_read_32(uint16_t reg)
{
    return ltc2984_read(reg, 4);
};
// Writes channel config data to the channel register
void ltc2984_set_channel_config(ChannelConfig_u channel_config, uint8_t channel)
{
    ltc2984_write_32((channel - 1) * 0x4 + 0x200, channel_config.raw);
};

// Reads and returns the bytes read from the channel config register of the ltc2984
ChannelConfig_u ltc2984_get_channel_config(uint8_t channel)
{
    ChannelConfig_u return_config = {.raw = ltc2984_read_32((channel - 1) * 0x4 + 0x200)};
    return return_config;
};

// write the config
#include <string.h>

// write the config
void ltc2984_set_steinhart(CustomSteinhart custom_steinhart, uint8_t offset)
{
    uint32_t value;
    memcpy(&value, &custom_steinhart.A, sizeof value);
    ltc2984_write_32(0x250 + offset, value);
    memcpy(&value, &custom_steinhart.B, sizeof value);
    ltc2984_write_32(0x254 + offset, value);
    memcpy(&value, &custom_steinhart.C, sizeof value);
    ltc2984_write_32(0x258 + offset, value);
    memcpy(&value, &custom_steinhart.D, sizeof value);
    ltc2984_write_32(0x25C + offset, value);
};

// read the config and make sure it matches
CustomSteinhart ltc2984_get_steinhart(uint8_t offset)
{
    CustomSteinhart return_steinhart;
    uint32_t value;
    value = ltc2984_read_32(0x250 + offset);
    memcpy(&return_steinhart.A, &value, sizeof return_steinhart.A);
    value = ltc2984_read_32(0x254 + offset);
    memcpy(&return_steinhart.B, &value, sizeof return_steinhart.B);
    value = ltc2984_read_32(0x258 + offset);
    memcpy(&return_steinhart.C, &value, sizeof return_steinhart.C);
    value = ltc2984_read_32(0x25C + offset);
    memcpy(&return_steinhart.D, &value, sizeof return_steinhart.D);
    return return_steinhart;
};

void ltc2984_init(ChannelConfig_u thermistor_config, ChannelConfig_u sense_config, uint8_t thermistor_channel, uint8_t sense_channel, CustomSteinhart thermistor_steinhart)
{
    // Write to LTC2984 config register
    initialized_therm_channel = thermistor_channel;
    initialized_sense_channel = sense_channel;
    ltc2984_set_channel_config(thermistor_config, thermistor_channel);
    ltc2984_set_channel_config(sense_config, sense_channel);
    ltc2984_set_steinhart(thermistor_steinhart, 0);
};

// Get temperature reading
float ltc2984_get_temperature(uint8_t channel)
{
    // Write to
    uint8_t initiate_conversion_byte = 0b10000000 + initialized_therm_channel;
    ltc2984_write_byte(0x0, initiate_conversion_byte);
    delay(200);

    uint8_t check_done = ltc2984_read_byte(0x0) >> 6;
    if (check_done != 0b01)
    {
        // Serial.println("Temp Timeout");
        return -1;
    }

    uint32_t temp_data = ltc2984_read_32(0x010 + (channel - 1) * 0x4);
    uint8_t temp_valid = temp_data >> 24;
    if (temp_valid != 0x01)
    {
        //  Serial.print("Temp Error: ");
        // Serial.println(temp_valid, BIN);
        return -1;
    }

    uint32_t raw_temp = temp_data & 0xFFFFFF;
    // Serial.print("Raw Temp: ");
    // Serial.println(raw_temp, BIN);
    float celsius = (float)raw_temp / 1024;

    return celsius;
}