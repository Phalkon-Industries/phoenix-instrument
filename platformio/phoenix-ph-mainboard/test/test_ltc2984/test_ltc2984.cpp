#include <Arduino.h>
#include "unity.h"
#include "ltc2984.hpp"
#include "Adafruit_TinyUSB.h"
#include <SPI.h>

void setUp(void)
{
}

void tearDown(void)
{
    // Clean up any resources used by the tests
}

// Setting up helpers for next test

static void read_ltc2984_multi_size_helper(uint16_t reg, uint8_t *val8, uint16_t *val16, uint32_t *val32)
{
    *val8 = ltc2984_read(reg, 1);
    *val16 = ltc2984_read(reg, 2);
    *val32 = ltc2984_read(reg, 4);
};

/*******************
 * Start of Tests
 **/
void test_read_write_multi_size()
{
    uint16_t reg = 0x0250;
    uint32_t data = 0xDEADBEEF;
    uint32_t clear = 0x0;
    uint8_t val8;
    uint16_t val16;
    uint32_t val32;

    // test clear register
    ltc2984_write(reg, clear, 4);
    read_ltc2984_multi_size_helper(reg, &val8, &val16, &val32);
    TEST_ASSERT_EQUAL_MESSAGE(0x0, val8, "val8, LTC2984 not starting with clear register");
    TEST_ASSERT_EQUAL_MESSAGE(0x0, val16, "val16, LTC2984 not starting with clear register");
    TEST_ASSERT_EQUAL_MESSAGE(0x0, val32, "val32, LTC2984 not starting with clear register");

    // test single byte write and read
    /* Note: Will write only the LSB if datbyte is larger than inputted size*/
    ltc2984_write(reg, data, 1);
    read_ltc2984_multi_size_helper(reg, &val8, &val16, &val32);
    TEST_ASSERT_EQUAL_MESSAGE(0xEF, val8, "val8, Single Byte write/read error");
    TEST_ASSERT_EQUAL_MESSAGE(0xEF00, val16, "val16, Single Byte write/read error");
    TEST_ASSERT_EQUAL_MESSAGE(0xEF000000, val32, "val32, Single Byte write/read error");

    // test 2 byte write and read
    ltc2984_write(reg, data, 2);
    read_ltc2984_multi_size_helper(reg, &val8, &val16, &val32);
    TEST_ASSERT_EQUAL_MESSAGE(0xBE, val8, "val8, Two Byte write/read error");
    TEST_ASSERT_EQUAL_MESSAGE(0xBEEF, val16, "val16, Two Byte write/read error");
    TEST_ASSERT_EQUAL_MESSAGE(0xBEEF0000, val32, "val32, Two Byte write/read error");

    // test 4 byte write and read
    ltc2984_write(reg, data, 4);
    read_ltc2984_multi_size_helper(reg, &val8, &val16, &val32);
    TEST_ASSERT_EQUAL_MESSAGE(0xDE, val8, "val8, Four Byte write/read error");
    TEST_ASSERT_EQUAL_MESSAGE(0xDEAD, val16, "val16, Four Byte write/read error");
    TEST_ASSERT_EQUAL_MESSAGE(0xDEADBEEF, val32, "val32, Four Byte write/read error");
}

void test_read_write_byte()
{
    uint16_t reg = 0x0250;
    ltc2984_write_byte(reg, 0xDE);
    uint16_t val = ltc2984_read_byte(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0xDE, val, "LTC2984 not functioning correctly.");
    ltc2984_write_byte(reg, 0x00);
    val = ltc2984_read_byte(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0x00, val, "LTC2984 not functioning correctly.");

    // test with lower value register below 8bit
    reg = 0x0F0;
    ltc2984_write_byte(reg, 0xDE);
    val = ltc2984_read_byte(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0xDE, val, "LTC2984 not functioning correctly.");
    ltc2984_write_byte(reg, 0x00);
    val = ltc2984_read_byte(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0x00, val, "LTC2984 not functioning correctly.");
}

void test_read_write_16()
{
    uint16_t reg = 0x0250;
    ltc2984_write_16(reg, 0xDEAD);
    uint16_t val = ltc2984_read_16(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0xDEAD, val, "LTC2984 not functioning correctly.");
    ltc2984_write_32(reg, 0x00);
    val = ltc2984_read_32(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0x00, val, "LTC2984 not functioning correctly.");

    // test with lower value register below 8bit
    reg = 0x0F0;
    ltc2984_write_16(reg, 0xDEAD);
    val = ltc2984_read_16(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0xDEAD, val, "LTC2984 not functioning correctly.");
    ltc2984_write_16(reg, 0x00);
    val = ltc2984_read_16(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0x00, val, "LTC2984 not functioning correctly.");
}

void test_read_write_32()
{
    uint16_t reg = 0x0250;
    ltc2984_write_32(reg, 0xDEADBEEF);
    uint32_t val = ltc2984_read_32(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0xDEADBEEF, val, "LTC2984 not functioning correctly.");
    ltc2984_write_32(reg, 0x00);
    val = ltc2984_read_32(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0x00, val, "LTC2984 not functioning correctly.");

    // test with lower value register below 8bit
    reg = 0x0F0;
    ltc2984_write_32(reg, 0xDEADBEEF);
    val = ltc2984_read_32(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0xDEADBEEF, val, "LTC2984 not functioning correctly.");
    ltc2984_write_32(reg, 0x00);
    val = ltc2984_read_32(reg);
    TEST_ASSERT_EQUAL_MESSAGE(0x00, val, "LTC2984 not functioning correctly.");
}

void test_channel_config_union()
{

    uint thermistor_test_config_expected_binary = 0b11010000100010101000000000000000;
    ChannelConfig_u channel_config = {.therm = {
                                          // taken from pg75 of datasheet example
                                          .steinhart_length = 0x000000,
                                          .steinhart_address = 0x000000,
                                          .empty = 0b000,
                                          .excitation_current = 0b0101,
                                          .excitation_mode = 0b01,
                                          .single_ended = 0b0,
                                          .r_sense_channel = 0b00010,
                                          .thermistor_type = 0b11010}};
    TEST_ASSERT_EQUAL_MESSAGE(thermistor_test_config_expected_binary, channel_config.raw, "Channel config binary not as expected");

    uint sense_test_config_expected_binary = 0b11101000100111000100000000000000;
    ChannelConfig_u sense_channel_config = {.sense = {
                                                .resistor_value = 0b000100111000100000000000000,
                                                .sense_resistor = 0b11101}};
    TEST_ASSERT_EQUAL_MESSAGE(sense_test_config_expected_binary, sense_channel_config.raw, "Sense config binary not as expected");
}

void test_set_get_channel_config()
{
    // Build a test thermistor config
    uint thermistor_test_config_expected_binary = 0b11010000100010101000000000000000;
    ChannelConfig_u channel_config = {.therm = {
                                          // taken from pg75 of datasheet example
                                          .steinhart_length = 0x000000,
                                          .steinhart_address = 0x000000,
                                          .empty = 0b000,
                                          .excitation_current = 0b0101,
                                          .excitation_mode = 0b01,
                                          .single_ended = 0b0,
                                          .r_sense_channel = 0b00010,
                                          .thermistor_type = 0b11010}};
    uint8_t channel = 4;
    TEST_ASSERT_EQUAL_MESSAGE(thermistor_test_config_expected_binary, channel_config.raw, "Channel config binary not as expected");
    // send the config to the ltc2984
    ltc2984_set_channel_config(channel_config, channel);

    // Check the config is as expected
    ChannelConfig_u read_config = ltc2984_get_channel_config(channel);
    TEST_ASSERT_EQUAL_MESSAGE(channel_config.raw, read_config.raw, "Read thermistor config not as expected");

    ChannelConfig_u sense_channel_config = {.sense = {
                                                .resistor_value = 0b000100111000100000000000000,
                                                .sense_resistor = 0b11101}};
    ltc2984_set_channel_config(sense_channel_config, channel);
    ChannelConfig_u read_sense_config = ltc2984_get_channel_config(channel);
    TEST_ASSERT_EQUAL_MESSAGE(sense_channel_config.raw, read_sense_config.raw, "Read sense config not as expected");
}

void test_set_get_steinhart()
{
    // set up a steinhart config
    CustomSteinhart custom_steinhart = {

        // Each coefficient is in 32-bit float format
        .A = 3.3540154E-03,
        .B = 2.5627725E-04,
        .C = 2.0829210E-06,
        .D = 7.3003206E-08};

    // write the config
    ltc2984_set_steinhart(custom_steinhart, 0);

    // read the config and make sure it matches
    CustomSteinhart read_steinhart = ltc2984_get_steinhart(0);

    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(custom_steinhart.A, read_steinhart.A, "Steinhart A not as expected");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(custom_steinhart.B, read_steinhart.B, "Steinhart B not as expected");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(custom_steinhart.C, read_steinhart.C, "Steinhart C not as expected");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(custom_steinhart.D, read_steinhart.D, "Steinhart D not as expected");
}

void test_ltc2984_init()
{
    CustomSteinhart custom_steinhart = {

        // Each coefficient is in 32-bit float format
        .A = 3.3540154E-03,
        .B = 2.5627725E-04,
        .C = 2.0829210E-06,
        .D = 7.3003206E-08};

    ChannelConfig_u sense_channel_config = {.sense = {
                                                .resistor_value = 0b000100111000100000000000000,
                                                .sense_resistor = 0b11101}};
    ChannelConfig_u therm_channel_config = {.therm = {
                                                // taken from pg75 of datasheet example
                                                .steinhart_length = 0x000000,
                                                .steinhart_address = 0x000000,
                                                .empty = 0b000,
                                                .excitation_current = 0b0101,
                                                .excitation_mode = 0b01,
                                                .single_ended = 0b0,
                                                .r_sense_channel = 0b00010,
                                                .thermistor_type = 0b11010}};
    uint8_t therm_channel = 3;
    uint8_t sense_channel = 1;
    ltc2984_init(therm_channel_config, sense_channel_config, therm_channel, sense_channel, custom_steinhart);
    CustomSteinhart read_steinhart = ltc2984_get_steinhart(0);
    ChannelConfig_u read_therm_config = ltc2984_get_channel_config(therm_channel);
    ChannelConfig_u read_sense_config = ltc2984_get_channel_config(sense_channel);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(custom_steinhart.A, read_steinhart.A, "Steinhart A not as expected");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(custom_steinhart.B, read_steinhart.B, "Steinhart B not as expected");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(custom_steinhart.C, read_steinhart.C, "Steinhart C not as expected");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(custom_steinhart.D, read_steinhart.D, "Steinhart D not as expected");
    TEST_ASSERT_EQUAL_MESSAGE(therm_channel_config.raw, read_therm_config.raw, "Read thermistor config not as expected");
    TEST_ASSERT_EQUAL_MESSAGE(sense_channel_config.raw, read_sense_config.raw, "Read sense config not as expected");
}
void test_get_temp()
{

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
    ltc2984_init(therm_channel_config, sense_channel_config, therm_channel, sense_channel, custom_steinhart);
    float read_temp = ltc2984_get_temperature(therm_channel);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(10, 20, read_temp, "Temperature not as expected");
}
void setup()
{
    SPI.begin();
    pinMode(LTC2984_CS_PIN, OUTPUT);
    digitalWrite(LTC2984_CS_PIN, HIGH);
    UNITY_BEGIN();
    delay(2000); // service delay

    RUN_TEST(test_get_temp);
    RUN_TEST(test_read_write_multi_size);
    RUN_TEST(test_read_write_byte);
    RUN_TEST(test_read_write_16);
    RUN_TEST(test_read_write_32);
    RUN_TEST(test_channel_config_union);
    RUN_TEST(test_set_get_channel_config);
    RUN_TEST(test_set_get_steinhart);
    RUN_TEST(test_ltc2984_init);

    UNITY_END();
}

void loop()
{
    // Do nothing
}