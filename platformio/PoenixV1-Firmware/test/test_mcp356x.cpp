// test_mcp356x.cpp (Fast command initial test)
// Goal: verify that sending FAST START command (0b01101000) returns STATUS 0b00010111.

#include <unity.h>
#include "unity_config.h"
#include "main.hpp"

#ifndef MCP356X_TEST_CS_PIN
#define MCP356X_TEST_CS_PIN 13
#endif
#ifndef MCP356X_TEST_DRDY_PIN
#define MCP356X_TEST_DRDY_PIN 41
#endif

// Expected status byte when issuing FAST START command (device address bits = 0b01)
#define MCP356X_EXPECTED_STATUS_START 0x17u

static void test_fast_command_start_status(void)
{
    TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(MCP356X_TEST_CS_PIN, MCP356X_TEST_DRDY_PIN, 500000UL));

    uint8_t status = 0xFF;
    int rc = mcp356x_send_fast_command(MCP356X_FASTCMD_START, &status);
    TEST_ASSERT_EQUAL_MESSAGE(MCP356X_OK, rc, "Fast command send failed");

    TEST_ASSERT_EQUAL_HEX8_MESSAGE(MCP356X_EXPECTED_STATUS_START, status, "Unexpected STATUS byte from FAST START command");
}

static void test_config0_register_roundtrip(void)
{
  // Arrange: start the driver and capture the current CONFIG0 byte.
  TEST_ASSERT_EQUAL(MCP356X_OK, mcp356x_initialize(MCP356X_TEST_CS_PIN, MCP356X_TEST_DRDY_PIN, 500000UL));

  uint8_t config0_before = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_read_register(MCP356X_REG_CONFIG0, &config0_before, 1u, NULL));

  // Act: flip a safe OSR bit (bit 2) so the value definitely changes.
  const uint8_t config0_flip_mask = 0x04u;
  uint8_t config0_modified = (uint8_t)(config0_before ^ config0_flip_mask);
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_CONFIG0, &config0_modified, 1u, NULL));

  // Assert: the new value is observable via another read.
  uint8_t config0_after = 0u;
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_read_register(MCP356X_REG_CONFIG0, &config0_after, 1u, NULL));
  TEST_ASSERT_EQUAL_HEX8(config0_modified, config0_after);
  TEST_ASSERT_NOT_EQUAL_HEX8(config0_before, config0_after);

  // Cleanup: restore the original CONFIG0 settings.
  TEST_ASSERT_EQUAL(MCP356X_OK,
            mcp356x_write_register(MCP356X_REG_CONFIG0, &config0_before, 1u, NULL));
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
  UNITY_SETUP_SERIAL_DEFAULT();
    RUN_TEST(test_fast_command_start_status);
  RUN_TEST(test_config0_register_roundtrip);
    UNITY_END();
}

void loop() {}
