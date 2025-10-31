#include "channel_map/command_parser.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <unity.h>

namespace {

constexpr const char* k_expected_command = "channel_map";

void assert_no_overrides(const PhoenixBenchmarkChannelMapCommandArguments& arguments) {
  // Step 1. Verify that the parser left all override indicators unset.
  TEST_ASSERT_EQUAL_UINT32(0u, arguments.sweep_count);
  TEST_ASSERT_FALSE(arguments.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(0u, arguments.dwell_us);
  TEST_ASSERT_FALSE(arguments.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0u, arguments.wiper_code);
  TEST_ASSERT_FALSE(arguments.has_wiper_override);
}

}  // namespace

void setUp(void) {
}

void tearDown(void) {
}

static void test_parse_command_accepts_plain_command(void) {
  // Step 1. Parse the bare command with no arguments.
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line("channel_map", k_expected_command);

  // Step 2. Confirm success, default arguments, and a null error message.
  TEST_ASSERT_TRUE_MESSAGE(outcome.success, "Expected bare command to parse successfully");
  assert_no_overrides(outcome.arguments);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_parse_command_supports_key_value_overrides(void) {
  // Step 1. Provide a sweep override using the key/value syntax.
  const char* line = "channel_map sweeps=200";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  // Step 2. Confirm the parser captures the sweep override and leaves other flags untouched.
  TEST_ASSERT_TRUE_MESSAGE(outcome.success, "Expected sweep override to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(200u, outcome.arguments.sweep_count);
  TEST_ASSERT_TRUE(outcome.arguments.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(0u, outcome.arguments.dwell_us);
  TEST_ASSERT_FALSE(outcome.arguments.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0u, outcome.arguments.wiper_code);
  TEST_ASSERT_FALSE(outcome.arguments.has_wiper_override);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_parse_command_rejects_key_value_dwell_override(void) {
  // Step 1. Attempt to provide a dwell override which is no longer supported.
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line("channel_map dwell_us=50", k_expected_command);

  // Step 2. Expect the parser to reject the command and surface the unknown argument error.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Dwell override should be rejected");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_unknown_argument, outcome.error_message);
}

static void test_parse_command_rejects_key_value_wiper_override(void) {
  // Step 1. Attempt to provide a wiper override which is no longer supported.
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line("channel_map wiper=0x20", k_expected_command);

  // Step 2. Expect the parser to reject the command and surface the unknown argument error.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Wiper override should be rejected");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_unknown_argument, outcome.error_message);
}

static void test_parse_command_accepts_json_payload(void) {
  // Step 1. Supply a JSON payload containing the sweep override.
  const char* line = "{\"command\":\"channel_map\",\"sweeps\":150}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  // Step 2. Verify the JSON path decodes the sweep override only.
  TEST_ASSERT_TRUE_MESSAGE(outcome.success, "Expected JSON payload to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(150u, outcome.arguments.sweep_count);
  TEST_ASSERT_TRUE(outcome.arguments.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(0u, outcome.arguments.dwell_us);
  TEST_ASSERT_FALSE(outcome.arguments.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0u, outcome.arguments.wiper_code);
  TEST_ASSERT_FALSE(outcome.arguments.has_wiper_override);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_parse_command_rejects_json_with_dwell_override(void) {
  // Step 1. Include a dwell override in the JSON payload which should be rejected.
  const char* line = "{\"command\":\"channel_map\",\"sweeps\":10,\"dwell_us\":25}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  // Step 2. Expect a failure and confirm the error message references unknown arguments.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "JSON dwell override should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_unknown_argument, outcome.error_message);
}

static void test_parse_command_rejects_json_with_wiper_override(void) {
  // Step 1. Include a wiper override in the JSON payload which should be rejected.
  const char* line = "{\"command\":\"channel_map\",\"sweeps\":10,\"wiper_code\":0x2A}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  // Step 2. Expect a failure and confirm the error message references unknown arguments.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "JSON wiper override should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_unknown_argument, outcome.error_message);
}

static void test_parse_command_rejects_json_with_missing_arguments(void) {
  // Step 1. Remove the required sweeps field from the JSON payload.
  const char* line = "{\"command\":\"channel_map\"}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  // Step 2. Confirm the parser falls back to defaults when sweeps are omitted.
  TEST_ASSERT_TRUE_MESSAGE(outcome.success, "JSON without sweeps should use defaults");
  assert_no_overrides(outcome.arguments);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_parse_command_rejects_json_with_invalid_value_types(void) {
  // Step 1. Encode a non-numeric sweeps value in the JSON payload.
  const char* line = "{\"command\":\"channel_map\",\"sweeps\":\"NaN\"}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  // Step 2. Expect the parser to report an invalid value error.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "JSON with non-numeric sweeps should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_value, outcome.error_message);
}

static void test_parse_command_rejects_unknown_arguments(void) {
  // Step 1. Append an unsupported key/value pair to the plain text command.
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line("channel_map unsupported=1", k_expected_command);

  // Step 2. Confirm the parser flags the argument as unknown.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Unknown argument should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_unknown_argument, outcome.error_message);
}

static void test_parse_command_rejects_malformed_key_value_token(void) {
  // Step 1. Insert an invalid token lacking an equals sign into the command.
  const PhoenixBenchmarkChannelMapParseOutcome outcome = phoenix_benchmark_channel_map_parse_command_line(
      "channel_map sweeps=100 invalidtoken dwell_us=40", k_expected_command);

  // Step 2. Expect the parser to return an invalid value error.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Malformed key/value token should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_value, outcome.error_message);
}

static void test_parse_command_rejects_key_value_with_invalid_value(void) {
  // Step 1. Provide a non-numeric value for a numeric override.
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line("channel_map sweeps=ten", k_expected_command);

  // Step 2. Confirm the parser rejects the command and reports the invalid value.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Non-numeric value should fail parsing");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_value, outcome.error_message);
}

static void test_parse_command_rejects_empty_input(void) {
  // Step 1. Attempt to parse a whitespace-only string.
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line("   \t\n", k_expected_command);

  // Step 2. Expect the helper to reject the command as invalid.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Whitespace-only input should be rejected");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_command, outcome.error_message);
}

static void test_parse_command_rejects_null_input(void) {
  // Step 1. Pass a null pointer to confirm guard behaviour.
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(nullptr, k_expected_command);

  // Step 2. Verify the parser reports the invalid command error code.
  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Null input should be rejected");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_command, outcome.error_message);
}

void setup() {
  // Step 1. Initialise Unity's serial output for the parser tests.
  UNITY_SETUP_SERIAL_DEFAULT();

  // Step 2. Execute all command parser scenarios.
  RUN_TEST(test_parse_command_accepts_plain_command);
  RUN_TEST(test_parse_command_supports_key_value_overrides);
  RUN_TEST(test_parse_command_rejects_key_value_dwell_override);
  RUN_TEST(test_parse_command_rejects_key_value_wiper_override);
  RUN_TEST(test_parse_command_accepts_json_payload);
  RUN_TEST(test_parse_command_rejects_json_with_dwell_override);
  RUN_TEST(test_parse_command_rejects_json_with_wiper_override);
  RUN_TEST(test_parse_command_rejects_json_with_missing_arguments);
  RUN_TEST(test_parse_command_rejects_json_with_invalid_value_types);
  RUN_TEST(test_parse_command_rejects_unknown_arguments);
  RUN_TEST(test_parse_command_rejects_malformed_key_value_token);
  RUN_TEST(test_parse_command_rejects_key_value_with_invalid_value);
  RUN_TEST(test_parse_command_rejects_empty_input);
  RUN_TEST(test_parse_command_rejects_null_input);

  // Step 3. Close the Unity session before entering the idle loop.
  UNITY_END();
}

void loop() {
}
