#include <Arduino.h>
#include "unity.h"
#include "Adafruit_TinyUSB.h"
#include "time.hpp"

void test_initTime()
{
    TEST_ASSERT_TRUE(initTime());
}

void test_setTime(void)
{
    setTime(12, 30, 0, 27, 10, 23);
    DateTime now = rtc.now();
    TEST_ASSERT_EQUAL(2023, now.year());
    TEST_ASSERT_EQUAL(10, now.month());
    TEST_ASSERT_EQUAL(27, now.day());
    TEST_ASSERT_EQUAL(12, now.hour());
    TEST_ASSERT_EQUAL(30, now.minute());
    TEST_ASSERT_EQUAL(0, now.second());

    // Checking the time struct
    TEST_ASSERT_EQUAL(2023, time.year);
    TEST_ASSERT_EQUAL(10, time.month);
    TEST_ASSERT_EQUAL(27, time.day);
    TEST_ASSERT_EQUAL(12, time.hour);
    TEST_ASSERT_EQUAL(30, time.minute);
    TEST_ASSERT_EQUAL(0, time.second);
}

void test_setTimeForTest(void)
{
    setTime(12, 30, 0, 27, 10, 23);
    DateTime now = rtc.now();
    TEST_ASSERT_EQUAL(2023, now.year());
    TEST_ASSERT_EQUAL(10, now.month());
    TEST_ASSERT_EQUAL(27, now.day());
    TEST_ASSERT_EQUAL(12, now.hour());
    TEST_ASSERT_EQUAL(30, now.minute());
    TEST_ASSERT_EQUAL(0, now.second());
}

void test_date_time()
{
    updateTime();
    TEST_ASSERT_GREATER_OR_EQUAL(2000, time.year);
    TEST_ASSERT_LESS_OR_EQUAL(2099, time.year);
    TEST_ASSERT_GREATER_OR_EQUAL(1, time.month);
    TEST_ASSERT_LESS_OR_EQUAL(12, time.month);
    TEST_ASSERT_GREATER_OR_EQUAL(1, time.day);
    TEST_ASSERT_LESS_OR_EQUAL(31, time.day);
    TEST_ASSERT_GREATER_OR_EQUAL(0, time.hour);
    TEST_ASSERT_LESS_OR_EQUAL(23, time.hour);
    TEST_ASSERT_GREATER_OR_EQUAL(0, time.minute);
    TEST_ASSERT_LESS_OR_EQUAL(59, time.minute);
    TEST_ASSERT_GREATER_OR_EQUAL(0, time.second);
    TEST_ASSERT_LESS_OR_EQUAL(59, time.second);
}

void setup()
{
    UNITY_BEGIN();
    delay(2000);

    RUN_TEST(test_initTime);
    RUN_TEST(test_setTime);
    RUN_TEST(test_setTimeForTest);
    RUN_TEST(test_date_time);
    UNITY_END();
}

void loop()
{
}