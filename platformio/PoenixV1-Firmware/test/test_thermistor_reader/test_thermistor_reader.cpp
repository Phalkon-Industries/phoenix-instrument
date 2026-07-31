// test_thermistor_reader.cpp
// Goal: exercise the thermistor reader against real ADC, GPIO, and thermistor hardware
// to validate the full measurement pipeline end-to-end.

#include "device_setup.hpp"
#include "power_control.hpp"
#include "thermistor_reader.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <math.h>
#include <unity.h>

// Plausible temperature bounds for board-mounted thermistors at room temperature.
// These are intentionally wide to accommodate unheated / slightly-warm boards.
static const float k_min_plausible_c = 10.0f;
static const float k_max_plausible_c = 50.0f;

// All five ThermistorIds in enum order for iteration.
static const ThermistorId k_all_ids[] = {
    ThermistorId::THERMISTOR_ID_SAMPLE,          ThermistorId::THERMISTOR_ID_BLUE_LED,
    ThermistorId::THERMISTOR_ID_GREEN_LED,       ThermistorId::THERMISTOR_ID_GAIN_STAGE,
    ThermistorId::THERMISTOR_ID_LED_DRIVE_STAGE,
};

void setUp(void) {
  // Step 1. Re-initialise the thermistor reader with the real board config so
  //         default hooks (real pinMode, digitalWrite, ADC reads) are active.
  thermistor_reader_reset_for_test();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&g_device_thermistor_reader_config));
}

void tearDown(void) {
  // Step 1. Clear thermistor reader state so follow-up tests start fresh.
  thermistor_reader_reset_for_test();
}

// Minimal smoke test: read a single thermistor and print the raw result so
// the serial log shows exactly what the hardware returned.
static void test_hardware_single_thermistor_read(void) {
  float     temperature_c = 0.0f;
  const int return_code   = thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c);

  // Step 1. Print the raw return code and temperature for diagnosis.
  UnityPrint("  return_code=");
  UnityPrintNumber(return_code);
  UnityPrint("  temperature_c=");
  UnityPrintFloat(temperature_c);
  UNITY_OUTPUT_CHAR('\n');

  // Step 2. Assert the call succeeded.
  TEST_ASSERT_EQUAL_INT_MESSAGE(THERMISTOR_READER_OK, return_code, "Single thermistor measurement should succeed");

  // Step 3. Confirm the returned temperature is not NaN or infinite.
  TEST_ASSERT_TRUE_MESSAGE(!isnan(temperature_c), "Temperature must not be NaN");
  TEST_ASSERT_TRUE_MESSAGE(!isinf(temperature_c), "Temperature must not be infinite");

  // Step 4. Verify the temperature falls within a plausible room-temperature range.
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE((k_max_plausible_c - k_min_plausible_c), k_min_plausible_c, temperature_c,
                                   "Temperature outside plausible range");
}

// Verify that measure_celsius returns OK and a plausible temperature for every
// thermistor on the board.
static void test_hardware_measure_celsius_all_sensors(void) {
  for (size_t i = 0u; i < (sizeof(k_all_ids) / sizeof(k_all_ids[0])); ++i) {
    float     temperature_c = 0.0f;
    const int return_code   = thermistor_reader_measure_celsius(k_all_ids[i], &temperature_c);

    TEST_ASSERT_EQUAL_INT_MESSAGE(THERMISTOR_READER_OK, return_code, "measure_celsius should succeed on real hardware");

    // Step 1. Confirm the returned temperature is not NaN or infinite.
    TEST_ASSERT_TRUE_MESSAGE(!isnan(temperature_c), "Temperature must not be NaN");
    TEST_ASSERT_TRUE_MESSAGE(!isinf(temperature_c), "Temperature must not be infinite");

    // Step 2. Verify the temperature falls within a plausible room-temperature range.
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE((k_max_plausible_c - k_min_plausible_c), k_min_plausible_c, temperature_c,
                                     "Temperature outside plausible range");
  }
}

// Verify that measure_all populates all five sensor slots with valid,
// plausible temperatures and captures a non-zero reference code.
static void test_hardware_measure_all_returns_valid_results(void) {
  ThermistorSweepResult result = {};
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_measure_all(&result));

  // Step 1. The reference divider must produce a non-zero code on real hardware.
  TEST_ASSERT_NOT_EQUAL_INT32_MESSAGE(0, result.reference_code, "Reference code must be non-zero on real hardware");

  // Step 2. Every sensor slot must be marked valid.
  for (size_t i = 0u; i < 5u; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(result.valid[i], "All five thermistor slots must be valid");
  }

  // Step 3. Every temperature must be plausible.
  for (size_t i = 0u; i < 5u; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(!isnan(result.temperatures_c[i]), "Temperature must not be NaN");
    TEST_ASSERT_TRUE_MESSAGE(!isinf(result.temperatures_c[i]), "Temperature must not be infinite");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE((k_max_plausible_c - k_min_plausible_c), k_min_plausible_c,
                                     result.temperatures_c[i], "Temperature outside plausible range");
  }
}

// Verify that calling measure_celsius twice on the same sensor returns
// consistent results (within a few degrees — thermistors don't change
// temperature instantly).
static void test_hardware_measure_celsius_is_consistent(void) {
  float first_c  = 0.0f;
  float second_c = 0.0f;

  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &first_c));
  // Brief pause to let any self-heating dissipate.
  delay(10);
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &second_c));

  // Step 1. Two consecutive readings of the same thermistor should agree within 2 °C.
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(2.0f, first_c, second_c,
                                   "Consecutive readings of the same thermistor should be consistent");
}

// Verify that the last-resistance accessor returns a plausible value after a
// successful measurement.
static void test_hardware_last_resistance_is_populated(void) {
  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_measure_celsius(ThermistorId::THERMISTOR_ID_GAIN_STAGE, &temperature_c));

  float resistance_ohms = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_get_last_resistance_for_test(
                                                  ThermistorId::THERMISTOR_ID_GAIN_STAGE, &resistance_ohms));

  // Step 1. A 10 kΩ NTC at room temperature should read between 1 kΩ and 100 kΩ.
  TEST_ASSERT_TRUE_MESSAGE(resistance_ohms > 1000.0f, "Resistance should be above 1 kΩ at room temperature");
  TEST_ASSERT_TRUE_MESSAGE(resistance_ohms < 100000.0f, "Resistance should be below 100 kΩ at room temperature");
}

void setup(void) {
  // Step 1. Initialise Unity's serial transport for logging.
  UNITY_SETUP_SERIAL_DEFAULT();
  delay(200);

  // Step 2. Bring up all shared hardware once (power rails, ADC, I2C, etc.).
  //         device_setup_initialize handles adc_hal_initialize,
  //         adc_hal_apply_default_configuration, and thermistor_reader_initialize
  //         in the correct order.
  TEST_ASSERT_EQUAL_INT(PHX_OK, device_setup_initialize());
  // Step 3. Run each hardware-integration test case in sequence.
  RUN_TEST(test_hardware_single_thermistor_read);
  RUN_TEST(test_hardware_measure_celsius_all_sensors);
  RUN_TEST(test_hardware_measure_all_returns_valid_results);
  RUN_TEST(test_hardware_measure_celsius_is_consistent);
  RUN_TEST(test_hardware_last_resistance_is_populated);

  // Step 4. Finalise Unity before handing control back to loop().
  UNITY_END();
}

void loop(void) {
}