#include "cli.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <unity.h>

static void test_cli_dispatch_rejects_empty_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::empty_command, (int) cli_dispatch_command(""));
}

static void test_cli_dispatch_rejects_unknown_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::unknown_command, (int) cli_dispatch_command("unknown"));
}

static void test_cli_dispatch_accepts_baseline_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("b"));
}

static void test_cli_dispatch_accepts_help_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CliDispatchResult::ok, (int) cli_dispatch_command("help"));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  UNITY_BEGIN();

  cli_initialize();

  RUN_TEST(test_cli_dispatch_rejects_empty_command);
  RUN_TEST(test_cli_dispatch_rejects_unknown_command);
  RUN_TEST(test_cli_dispatch_accepts_baseline_command);
  RUN_TEST(test_cli_dispatch_accepts_help_command);

  UNITY_END();
}

void loop() {
}
