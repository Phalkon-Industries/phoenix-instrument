#include "phoenix_settings.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

namespace {

// Test that default values are returned when no settings file exists.
static void test_settings_default_values_on_first_load(void) {
  // Step 1: Reset settings to ensure clean state (deletes any existing file).
  phoenix_settings_deinitialize();

  // Step 2: Initialize settings module; should create defaults since no file exists.
  const int init_result = phoenix_settings_initialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, init_result);

  // Step 3: Retrieve cached settings and verify they match compile-time defaults.
  const PhoenixSettings* settings = phoenix_settings_get();
  TEST_ASSERT_NOT_NULL(settings);
  TEST_ASSERT_EQUAL_UINT8(PHOENIX_SETTINGS_DEFAULT_BLUE_WIPER, settings->blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(PHOENIX_SETTINGS_DEFAULT_GREEN_WIPER, settings->green_wiper_code);
}

// Test that saved settings can be read back correctly after a round trip.
static void test_settings_save_and_load_round_trip(void) {
  // Step 1: Initialize settings module with defaults.
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize());

  // Step 2: Modify settings and save to flash.
  PhoenixSettings modified  = *phoenix_settings_get();
  modified.blue_wiper_code  = 0x42u;
  modified.green_wiper_code = 0x84u;
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_save(&modified));

  // Step 3: Deinitialize and reinitialize to force reload from flash.
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize());

  // Step 4: Verify reloaded settings match the values we saved.
  const PhoenixSettings* reloaded = phoenix_settings_get();
  TEST_ASSERT_NOT_NULL(reloaded);
  TEST_ASSERT_EQUAL_UINT8(0x42u, reloaded->blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(0x84u, reloaded->green_wiper_code);
}

// Test that get returns cached values without requiring file I/O.
static void test_settings_get_cached_values(void) {
  // Step 1: Initialize settings module.
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize());

  // Step 2: Get settings pointer twice and verify they point to same data.
  const PhoenixSettings* first_get  = phoenix_settings_get();
  const PhoenixSettings* second_get = phoenix_settings_get();
  TEST_ASSERT_NOT_NULL(first_get);
  TEST_ASSERT_EQUAL_PTR(first_get, second_get);
}

// Test that save rejects null pointer input.
static void test_settings_save_rejects_null(void) {
  // Step 1: Initialize settings module.
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize());

  // Step 2: Attempt to save null settings and expect error.
  const int result = phoenix_settings_save(NULL);
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_ERR_INVALID_ARG, result);
}

// Test that operations fail gracefully when module is not initialized.
static void test_settings_operations_require_init(void) {
  // Step 1: Ensure module is not initialized.
  phoenix_settings_deinitialize();

  // Step 2: Verify get returns null when not initialized.
  TEST_ASSERT_NULL(phoenix_settings_get());

  // Step 3: Verify save fails when not initialized.
  PhoenixSettings dummy = {};
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_ERR_NOT_INITIALIZED, phoenix_settings_save(&dummy));
}

// Test that reset_to_defaults restores factory values and persists them.
static void test_settings_reset_to_defaults(void) {
  // Step 1: Initialize and modify settings.
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize());

  PhoenixSettings modified  = *phoenix_settings_get();
  modified.blue_wiper_code  = 0x11u;
  modified.green_wiper_code = 0x22u;
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_save(&modified));

  // Step 2: Reset to defaults.
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_reset_to_defaults());

  // Step 3: Verify settings match defaults.
  const PhoenixSettings* settings = phoenix_settings_get();
  TEST_ASSERT_EQUAL_UINT8(PHOENIX_SETTINGS_DEFAULT_BLUE_WIPER, settings->blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(PHOENIX_SETTINGS_DEFAULT_GREEN_WIPER, settings->green_wiper_code);

  // Step 4: Reload and verify defaults persisted.
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize());
  const PhoenixSettings* reloaded = phoenix_settings_get();
  TEST_ASSERT_EQUAL_UINT8(PHOENIX_SETTINGS_DEFAULT_BLUE_WIPER, reloaded->blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(PHOENIX_SETTINGS_DEFAULT_GREEN_WIPER, reloaded->green_wiper_code);
}

}  // namespace

void setUp(void) {
  // Ensure clean state before each test.
}

void tearDown(void) {
  // Clean up after each test.
  phoenix_settings_deinitialize();
}

void setup(void) {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_settings_default_values_on_first_load);
  RUN_TEST(test_settings_save_and_load_round_trip);
  RUN_TEST(test_settings_get_cached_values);
  RUN_TEST(test_settings_save_rejects_null);
  RUN_TEST(test_settings_operations_require_init);
  RUN_TEST(test_settings_reset_to_defaults);
  UNITY_END();
}

void loop(void) {
  // Unity tests run once; leave loop empty.
}
