#include <Arduino.h>
#include "unity.h"
#include "adpd4101.hpp"
#include "Adafruit_TinyUSB.h"

void setUp(void)
{
    Wire.begin();
}

void tearDown(void)
{
    // Clean up any resources used by the tests
}

void test_read_write()
{
    uint8_t reg = 0x000D;
    adpd4101_write(reg, 0xDEAD);
    uint16_t val = adpd4101_read(reg);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0xFFFF, val, "ADPD4101 not connected or not responding.");
    TEST_ASSERT_EQUAL_MESSAGE(0xDEAD, val, "ADPD4101 not functioning correctly.");
}
/*TODO: Write a test to make sure the bytes out are correct. (Had an issue before with them being off by one)*/

void setup()
{

    UNITY_BEGIN();
    delay(2000); // service delay

    RUN_TEST(test_read_write);

    UNITY_END();
}

void loop()
{
    // Do nothing
}