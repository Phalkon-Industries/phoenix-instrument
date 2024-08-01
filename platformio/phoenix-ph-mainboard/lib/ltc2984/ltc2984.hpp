#ifndef LTC1984_HPP
#define LTC2984_HPP
#define LTC2984_CS_PIN 11

/* --------------
 * Struct Definitions
 * ---------------
 */

typedef struct
{
    uint steinhart_length : 6;
    uint steinhart_address : 6;
    uint empty : 3;
    uint excitation_current : 4;
    uint excitation_mode : 2;
    uint single_ended : 1;
    uint r_sense_channel : 5;
    uint thermistor_type : 5;
} ThermChannelConfig;

typedef struct
{
    uint resistor_value : 27;
    uint sense_resistor : 5;
} SenseChannelConfig;

typedef struct
{
    float A;
    float B;
    float C;
    float D;
} CustomSteinhart;

typedef union
{
    ThermChannelConfig therm;
    SenseChannelConfig sense;
    uint32_t raw;
} ChannelConfig_u;

typedef struct
{
    uint address;
    ChannelConfig_u config;
} ChannelConfig;

/* --------------
 * Function Definitions
 * ---------------
 */
void ltc2984_write(uint16_t reg, uint32_t databyte, uint size);
uint32_t ltc2984_read(uint16_t reg, uint size);

// Writes the byte of data to the register using the SPI bus
void ltc2984_write_byte(uint16_t reg, uint8_t databyte);
uint8_t ltc2984_read_byte(uint16_t reg);

// Writes the byte of data to the register using the SPI bus
void ltc2984_write_16(uint16_t reg, uint16_t databyte);
uint16_t ltc2984_read_16(uint16_t reg);

// Writes the byte of data to the register using the SPI bus
void ltc2984_write_32(uint16_t reg, uint32_t databyte);
uint32_t ltc2984_read_32(uint16_t reg);

// Writes channel config data to the channel register
void ltc2984_set_channel_config(ChannelConfig_u channel_config, uint8_t channel);

// Reads and returns the bytes read from the channel config register of the ltc2984
ChannelConfig_u ltc2984_get_channel_config(uint8_t channel);

// write the config
void ltc2984_set_steinhart(CustomSteinhart custom_steinhart, uint8_t offset);

// read the config and make sure it matches
CustomSteinhart ltc2984_get_steinhart(uint8_t offset);

// set up the ltc2984 device
void ltc2984_init(ChannelConfig_u thermistor_config, ChannelConfig_u sense_config, uint8_t thermistor_channel, uint8_t sense_channel, CustomSteinhart thermistor_steinhart);

// Get temperature reading
float ltc2984_get_temperature(uint8_t channel);

#endif