// test_mcp356x.cpp (Fast command initial test)
// Goal: verify that sending FAST START command (0b01101000) returns STATUS 0b00010111.

#include <unity.h>
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

void setUp(void) {}
void tearDown(void) {}

void setup()
{
  Serial.begin(115200);
  Serial.flush();
  while (!Serial)
    ;
  UNITY_BEGIN();
  delay(2000);  // service delay
    RUN_TEST(test_fast_command_start_status);
    UNITY_END();
}

void loop() {}
