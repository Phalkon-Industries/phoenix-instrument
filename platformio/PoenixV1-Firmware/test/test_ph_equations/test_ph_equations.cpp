#include <unity.h>

#include "ph_equations.hpp"
#include "unity_config.h"
#include <Arduino.h>

static void test_absorbance_happy_path_calculates_expected_value(void) {
  // Step 1: Arrange inputs for a 10× attenuation (ratio = 10).
  double absorbance = -1.0;

  // Step 2: Act by computing absorbance.
  const int return_code = ph_equations_calc_absorbance(1000.0, 100.0, &absorbance);

  // Step 3: Assert the call succeeded and A == log10(10) == 1.0.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_OK, return_code,
                                "absorbance should succeed for positive inputs");
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-6, 1.0, absorbance,
                                   "absorbance should equal log10(10.0) for 10× attenuation");
}

static void test_absorbance_rejects_invalid_inputs(void) {
  // Step 1: Create an output slot; it must not be written on error.
  double absorbance = 0.0;

  // Step 2: Assert invalid reference intensity is rejected.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_ERR_INVALID_ARG,
                                ph_equations_calc_absorbance(0.0, 100.0, &absorbance),
                                "zero reference intensity should be invalid");
  // Step 3: Assert invalid sample intensity is rejected.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_ERR_INVALID_ARG,
                                ph_equations_calc_absorbance(100.0, 0.0, &absorbance),
                                "zero sample intensity should be invalid");
  // Step 4: Assert null output pointer is rejected.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_ERR_INVALID_ARG,
                                ph_equations_calc_absorbance(100.0, 100.0, NULL),
                                "null out_absorbance should be invalid");
}

static void test_r_ratio_green_over_blue_basic(void) {
  // Step 1: Arrange representative absorbances (A578=0.5, A434=1.0) so R should be 0.5.
  double r = -1.0;

  // Step 2: Act by computing the ratio.
  const int return_code = ph_equations_calc_r_ratio(0.5, 1.0, &r);

  // Step 3: Assert success and the expected ratio value.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_OK, return_code,
                                "r-ratio should succeed when denominator is positive");
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-6, 0.5, r, "R = A578/A434 should equal 0.5/1.0");
}

static void test_r_ratio_rejects_zero_blue(void) {
  // Step 1: Denominator zero should trigger a domain error and leave output untouched.
  double r = 0.0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_ERR_DOMAIN,
                                ph_equations_calc_r_ratio(0.5, 0.0, &r),
                                "denominator <= 0 should be rejected as domain error");
}

static void test_compute_ph_rejects_invalid_inputs(void) {
  // Step 1: Prepare an output slot.
  double ph = 0.0;
  // Step 2: Assert non-positive R is rejected.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_ERR_INVALID_ARG,
                                ph_equations_compute_ph(0.0, 25.0, 35.0, &ph),
                                "non-positive R should be invalid");
  // Step 3: Assert absolute-zero or below temperatures are rejected.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_ERR_INVALID_ARG,
                                ph_equations_compute_ph(1.0, -273.15, 35.0, &ph),
                                "temperature <= -273.15 C should be invalid");
  // Step 4: Assert negative salinity is rejected.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_ERR_INVALID_ARG,
                                ph_equations_compute_ph(1.0, 25.0, -1.0, &ph),
                                "negative salinity should be invalid");
  // Step 5: Assert null out pointer is rejected.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_ERR_INVALID_ARG,
                                ph_equations_compute_ph(1.0, 25.0, 35.0, NULL),
                                "null out_ph should be invalid");
}

static void test_compute_ph_smoke_test_typical_conditions(void) {
  // Step 1: Compute pH for typical oceanic conditions (25 C, S=35, R=0.8).
  double ph = -1.0;
  const int return_code = ph_equations_compute_ph(0.8, 25.0, 35.0, &ph);
  // Step 2: Expect success and a plausible oceanic pH range.
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_OK, return_code, "compute_ph should succeed for typical inputs");
  TEST_ASSERT_TRUE_MESSAGE(ph > 6.5 && ph < 9.0, "pH should fall within a broad oceanic range (6.5–9.0)");
}

static void test_compute_ph_coefficients_and_value_typical_conditions(void) {
  // Step 1: Arrange typical oceanic conditions.
  const double r_ratio = 1.0;
  const double temp_c  = 25.0;
  const double sal_psu = 35.0;

  // Step 2: Compute coefficients explicitly for assertions.
  double e1 = 0.0, e3_over_e2 = 0.0, pk1_over_e2 = 0.0;
  const int rc_coeff = ph_equations_compute_coefficients(temp_c, sal_psu, &e1, &e3_over_e2, &pk1_over_e2);
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_OK, rc_coeff, "coefficients should compute successfully");

  // Step 3: Assert coefficients match expected literature-derived values within tolerance.
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5, 0.00571, e1, "e1 value is not correct for 25C, S=35");
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5, 0.05678, e3_over_e2, "e3/e2 value is not correct for 25C, S=35");
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5, 7.64703, pk1_over_e2, "pK1/e2 value is not correct for 25C, S=35");

  // Step 4: Compute pH for the same inputs and assert expected value.
  double ph = -1.0;
  const int rc_ph = ph_equations_compute_ph(r_ratio, temp_c, sal_psu, &ph);
  TEST_ASSERT_EQUAL_INT_MESSAGE(PH_EQUATIONS_OK, rc_ph, "pH computation should succeed");
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5, 7.66993, ph, "pH value is not correct for 25C, S=35, R=1.0");
}

void setup(void) {
  // Step 1: Prepare the Unity serial interface for log output and start the framework.
  UNITY_SETUP_SERIAL_DEFAULT();
  UNITY_BEGIN();
  // Step 2: Register each test case for the pH equations module.
  RUN_TEST(test_absorbance_happy_path_calculates_expected_value);
  RUN_TEST(test_absorbance_rejects_invalid_inputs);
  RUN_TEST(test_r_ratio_green_over_blue_basic);
  RUN_TEST(test_r_ratio_rejects_zero_blue);
  RUN_TEST(test_compute_ph_rejects_invalid_inputs);
  RUN_TEST(test_compute_ph_smoke_test_typical_conditions);
  RUN_TEST(test_compute_ph_coefficients_and_value_typical_conditions);
  // Step 3: Finalise Unity; loop() will idle.
  UNITY_END();
}

void loop(void) {
}
