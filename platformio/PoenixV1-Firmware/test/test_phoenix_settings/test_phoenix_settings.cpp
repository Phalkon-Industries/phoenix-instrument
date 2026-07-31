#include "device_setup.hpp"
#include "digipot_hal.hpp"
#include "phoenix_settings.hpp"
#include "unity_config.h"
#include <Adafruit_LittleFS.h>
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <InternalFileSystem.h>
#include <Wire.h>
#include <unity.h>

namespace {

// Path to the settings file - must match the path in phoenix_settings.cpp.
static const char* k_test_settings_file_path = "/phoenix_settings.dat";

// Test default wiper codes used when flash is empty or corrupt.
static const uint8_t k_test_default_blue_wiper  = 0xFFu;
static const uint8_t k_test_default_green_wiper = 0xD3u;

// Default settings struct passed to initialize.
static const PhoenixSettings k_test_defaults = {
    k_test_default_blue_wiper,
    k_test_default_green_wiper,
    {0},  // reserved
};

// Test that default values are returned when no settings file exists.
static void test_settings_default_values_on_first_load(void) {
  // Step 1: Reset settings to ensure clean state.
  phoenix_settings_deinitialize();

  // Step 2: Delete the settings file from flash to simulate first boot.
  InternalFS.begin();
  if (InternalFS.exists(k_test_settings_file_path)) {
    InternalFS.remove(k_test_settings_file_path);
  }

  // Step 3: Initialize settings module; should create defaults since no file exists.
  const int init_result = phoenix_settings_initialize(&k_test_defaults);
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, init_result);

  // Step 4: Retrieve cached settings and verify they match the provided defaults.
  const PhoenixSettings* settings = phoenix_settings_get();
  TEST_ASSERT_NOT_NULL(settings);
  TEST_ASSERT_EQUAL_UINT8(k_test_default_blue_wiper, settings->blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(k_test_default_green_wiper, settings->green_wiper_code);
}

// Test that saved settings can be read back correctly after a round trip.
static void test_settings_save_and_load_round_trip(void) {
  // Step 1: Initialize settings module with defaults.
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize(&k_test_defaults));

  // Step 2: Modify settings and save to flash.
  PhoenixSettings modified  = *phoenix_settings_get();
  modified.blue_wiper_code  = 0x42u;
  modified.green_wiper_code = 0x84u;
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_save(&modified));

  // Step 3: Deinitialize and reinitialize to force reload from flash.
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize(&k_test_defaults));

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
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize(&k_test_defaults));

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
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize(&k_test_defaults));

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
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize(&k_test_defaults));

  PhoenixSettings modified  = *phoenix_settings_get();
  modified.blue_wiper_code  = 0x11u;
  modified.green_wiper_code = 0x22u;
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_save(&modified));

  // Step 2: Reset to defaults.
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_reset_to_defaults());

  // Step 3: Verify settings match defaults.
  const PhoenixSettings* settings = phoenix_settings_get();
  TEST_ASSERT_EQUAL_UINT8(k_test_default_blue_wiper, settings->blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(k_test_default_green_wiper, settings->green_wiper_code);

  // Step 4: Reload and verify defaults persisted.
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize(&k_test_defaults));
  const PhoenixSettings* reloaded = phoenix_settings_get();
  TEST_ASSERT_EQUAL_UINT8(k_test_default_blue_wiper, reloaded->blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(k_test_default_green_wiper, reloaded->green_wiper_code);
}

// Test that apply_wiper_codes writes blue to channel 0 and green to channel 1.
static void test_settings_apply_wiper_codes_writes_to_digipot(void) {
  // Step 1: Initialize digipot driver (requires Wire bus already started).
  int result = digipot_blue_initialize(MCP41U83_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);
  result = digipot_green_initialize(AD5242_I2C_ADDRESS, 1, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, result);
  // Step 2: Initialize settings module (loads from flash or creates defaults).
  phoenix_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_initialize(&k_test_defaults));

  // Step 3: Save distinctive wiper codes to settings so we can verify channel mapping.
  const uint16_t  test_blue_wiper  = 0x3Au;  // Distinctive value for blue.
  const uint16_t  test_green_wiper = 0x5Cu;  // Distinctive value for green.
  PhoenixSettings test_settings    = *phoenix_settings_get();
  test_settings.blue_wiper_code    = test_blue_wiper;
  test_settings.green_wiper_code   = test_green_wiper;
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_save(&test_settings));

  // Step 4: Apply wiper codes to hardware.
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_OK, phoenix_settings_apply_wiper_codes());

  // Step 5: Read back wiper codes from digipot and verify channel mapping.
  // Blue LED uses digipot channel 0, green LED uses digipot channel 1.
  uint16_t blue_reading;
  uint16_t green_reading;
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, digipot_blue_read_wiper(&blue_reading));
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, digipot_green_read_wiper(&green_reading));

  // Step 6: Assert correct channel mapping: blue->channel 0, green->channel 1.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(test_blue_wiper, blue_reading, "Blue wiper should be written to channel 0");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(test_green_wiper, green_reading, "Green wiper should be written to channel 1");

  // Step 7: Clean up digipot state.
  ad524x_deinitialize();
}

// Test that apply_wiper_codes requires settings to be initialized.
static void test_settings_apply_wiper_codes_requires_initialization(void) {
  // Step 1: Ensure settings module is not initialized.
  phoenix_settings_deinitialize();

  // Step 2: Initialize digipot driver so we can distinguish settings error from digipot error.
  int err = digipot_blue_initialize(MCP41U83_I2C_ADDRESS, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, err);
  err = digipot_green_initialize(AD5242_I2C_ADDRESS, 1, &Wire);
  TEST_ASSERT_EQUAL_INT(DIGIPOT_HAL_OK, err);

  // Step 3: Attempt to apply wiper codes without initializing settings.
  const int result = phoenix_settings_apply_wiper_codes();
  TEST_ASSERT_EQUAL_INT(PHOENIX_SETTINGS_ERR_NOT_INITIALIZED, result);
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

  // Step 1: Run production bring-up so the analog rails energise and the powered digipot
  // responds on the I2C bus. Settings tests re-initialise phoenix_settings themselves.
  power_control_prepare_power_domains(&g_device_power_control_config);
  Wire.begin();

  RUN_TEST(test_settings_default_values_on_first_load);
  RUN_TEST(test_settings_save_and_load_round_trip);
  RUN_TEST(test_settings_get_cached_values);
  RUN_TEST(test_settings_save_rejects_null);
  RUN_TEST(test_settings_operations_require_init);
  RUN_TEST(test_settings_reset_to_defaults);
  RUN_TEST(test_settings_apply_wiper_codes_writes_to_digipot);
  RUN_TEST(test_settings_apply_wiper_codes_requires_initialization);
  UNITY_END();
}

void loop(void) {
  // Unity tests run once; leave loop empty.
}
