/* this is where the calculations for pH live. */

// Func: calc absorbance. Takes initial and current value and calculates absorbance. Takes an optional correction
// argument as well which would correct LED nonliniearity as well. The exact form of the argument is currently unknown
// but we should have a placeholder for it.

// Func calc R_ratio. Just A434/A575

// Func calc pH. This will be the equation from Byrn 2017 that we will be using. It takes r_ratio, temperature, and
// salinity. Then calculates the output pH.
/*
Here is an example code
#include <math.h>
#include "ph-measurement.hpp"

double e1 = 0;
double e3_e2 = 0;
double pk1e2 = 0;
double salinity = 0;
double find_ph(double r_ratio, double temp, double salinity)
{
    if (temp <= -273.15 || salinity < 0 || r_ratio <= 0)
    {
        return -1.0;
    }
    double kelvin = temp + 273.15;
    e1 = -0.007762 + 4.5174 * 0.00001 * kelvin;
    e3_e2 = -0.020813 + 2.60262 * (0.0001 * kelvin) + 1.0436 * (0.0001 * (salinity - 35));
    pk1e2 = (5.561224 - 0.547716 * pow(salinity, 0.5) + 0.123791 * salinity - 0.0280156 * pow(salinity, 1.5) +
0.00344940 * pow(salinity, 2) - 0.000167297 * pow(salinity, 2.5) + 52.640726 * pow(salinity, 0.5) * pow(kelvin, -1) +
815.984591 * pow(kelvin, -1)); double log_argument = (r_ratio - e1) / (1 - r_ratio * e3_e2); if (log_argument < 0)
    {
        return -1.0;
    }
    double ph_value = pk1e2 + log10(log_argument);
    return ph_value;
}

And here are some example tests

#include <Arduino.h>
#include "unity.h"
#include "Adafruit_TinyUSB.h"
#include "ph-measurement.hpp"
#include "logging.hpp"
#include "readings.hpp"

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

void test_calculateAbsorbances(){
    sampleBlockReadings.absorbance_blue = 0;
    sampleBlockReadings.absorbance_green = 0;
    sampleBlockReadings.blue_reading_avg = 103839;
    sampleBlockReadings.green_reading_avg = 2679781;
    sampleBlockReferenceReadings.blue_reading_avg = 3841410;
    sampleBlockReferenceReadings.green_reading_avg = 4478836l;

    calculateAbsorbance(&sampleBlockReferenceReadings, &sampleBlockReadings);

    TEST_ASSERT_EQUAL(1.568, sampleBlockReadings.absorbance_blue);
    TEST_ASSERT_EQUAL(0.223, sampleBlockReadings.absorbance_green);
}

void test_calculateRRatio(){
    sampleBlockReadings.r_ratio = 0;
    sampleBlockReadings.absorbance_blue = 1.568;
    sampleBlockReadings.absorbance_green = 0.223;

    calculateRRatio();

    TEST_ASSERT_EQUAL(0.142, sampleBlockReadings.r_ratio);
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
    RUN_TEST(test_calculateAbsorbances);
    RUN_TEST(test_calculateRRatio);

    UNITY_END();
}

void loop()
{
}

Be sure to rewrite all of this because this is just example, we don't have rights to reuse this code

In the future we will need to include impurity correction, but just write that as a comment in the pH equation stuff for
now. It is currently unimplemented. The R value is calculated R = 578A/434A eg green/blue*/
