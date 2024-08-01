#include <Arduino.h>
#include "unity.h"
#include "Adafruit_TinyUSB.h"
#include "ph-measurement.hpp"
#include "logging.hpp"

void test_findpH()
{
    double R_ratio = 1.;
    double temp = 25.0;
    double salinity = 35.0;
    double calculated_ph = find_ph(R_ratio, temp, salinity);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.00001, 0.00571, e1, "e1 value is not correct");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.00001, 0.05678, e3_e2, "e3/e2 value is not correct");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.00001, 7.64703, pk1e2, "pk1e2 value is not correct");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.00001, 7.66993, calculated_ph, "pH value is not correct");
}

void test_findpH_rratio_zero()
{
    double R_ratio = 0.;
    double temp = 25.0;
    double salinity = 35.0;
    double calculated_ph = find_ph(R_ratio, temp, salinity);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-1.0, calculated_ph, "zero r_ration not erroring");
}
void test_findpH_zero_salinity()
{
    double R_ratio = 1.;
    double temp = 25.0;
    double salinity = 0;
    double calculated_ph = find_ph(R_ratio, temp, salinity);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.00001, 8.319275, calculated_ph, "pH value is not correct");
}
void test_findpH_negative_salinity()
{
    double R_ratio = 1.;
    double temp = 25.0;
    double salinity = -1;
    double calculated_ph = find_ph(R_ratio, temp, salinity);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-1.0, calculated_ph, "pH value is not erroring");
}
void test_findpH_zero_kelvin()
{
    double R_ratio = 1.;
    double temp = -273.15;
    double salinity = 35;
    double calculated_ph = find_ph(R_ratio, temp, salinity);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-1.0, calculated_ph, "pH value is not erroring");
}
void test_findpH_negative_kelvin()
{
    double R_ratio = 1.;
    double temp = -300;
    double salinity = 35;
    double calculated_ph = find_ph(R_ratio, temp, salinity);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-1.0, calculated_ph, "pH value is not erroring");
}

void setup()
{
    UNITY_BEGIN();
    delay(2000);

    RUN_TEST(test_findpH);
    RUN_TEST(test_findpH_rratio_zero);
    RUN_TEST(test_findpH_zero_salinity);
    RUN_TEST(test_findpH_negative_salinity);
    RUN_TEST(test_findpH_zero_kelvin);
    RUN_TEST(test_findpH_negative_kelvin);

    UNITY_END();
}

void loop()
{
}