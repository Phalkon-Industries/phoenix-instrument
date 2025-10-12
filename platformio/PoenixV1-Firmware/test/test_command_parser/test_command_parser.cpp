#include "channel_map/command_parser.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <unity.h>

namespace {

constexpr const char* k_expected_command = "channel_map";

void assert_no_overrides(const PhoenixBenchmarkChannelMapCommandArguments& arguments) {
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
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line("channel_map", k_expected_command);

  TEST_ASSERT_TRUE_MESSAGE(outcome.success, "Expected bare command to parse successfully");
  assert_no_overrides(outcome.arguments);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_parse_command_supports_key_value_overrides(void) {
  const char* line = "channel_map sweeps=200 dwell_us=75 wiper=0x2A";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  TEST_ASSERT_TRUE_MESSAGE(outcome.success, "Expected key/value command to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(200u, outcome.arguments.sweep_count);
  TEST_ASSERT_TRUE(outcome.arguments.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(75u, outcome.arguments.dwell_us);
  TEST_ASSERT_TRUE(outcome.arguments.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x2Au, outcome.arguments.wiper_code);
  TEST_ASSERT_TRUE(outcome.arguments.has_wiper_override);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_parse_command_supports_key_value_random_order(void) {
  const char* line = "channel_map wiper=0x33 sweeps=175 dwell_us=65";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  TEST_ASSERT_TRUE_MESSAGE(outcome.success, "Expected key/value command with random order to succeed");
  TEST_ASSERT_EQUAL_UINT32(175u, outcome.arguments.sweep_count);
  TEST_ASSERT_TRUE(outcome.arguments.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(65u, outcome.arguments.dwell_us);
  TEST_ASSERT_TRUE(outcome.arguments.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x33u, outcome.arguments.wiper_code);
  TEST_ASSERT_TRUE(outcome.arguments.has_wiper_override);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_parse_command_accepts_json_payload(void) {
  const char* line = "{\"command\":\"channel_map\",\"sweeps\":150,\"dwell_us\":60,\"wiper_code\":0x05}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  TEST_ASSERT_TRUE_MESSAGE(outcome.success, "Expected JSON payload to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(150u, outcome.arguments.sweep_count);
  TEST_ASSERT_TRUE(outcome.arguments.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(60u, outcome.arguments.dwell_us);
  TEST_ASSERT_TRUE(outcome.arguments.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x05u, outcome.arguments.wiper_code);
  TEST_ASSERT_TRUE(outcome.arguments.has_wiper_override);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_parse_command_accepts_json_with_random_order(void) {
  const char* line = "{\"wiper_code\":0x09,\"dwell_us\":80,\"command\":\"channel_map\",\"sweeps\":40}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  TEST_ASSERT_TRUE_MESSAGE(outcome.success, "Expected JSON with shuffled keys to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(40u, outcome.arguments.sweep_count);
  TEST_ASSERT_TRUE(outcome.arguments.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(80u, outcome.arguments.dwell_us);
  TEST_ASSERT_TRUE(outcome.arguments.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x09u, outcome.arguments.wiper_code);
  TEST_ASSERT_TRUE(outcome.arguments.has_wiper_override);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_parse_command_rejects_json_with_unknown_arguments(void) {
  const char* line = "{\"command\":\"channel_map\",\"sweeps\":10,\"dwell_us\":25,\"wiper_code\":0x04,\"extra\":1}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "JSON with unknown key should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_unknown_argument, outcome.error_message);
}

static void test_parse_command_rejects_json_with_missing_arguments(void) {
  const char* line = "{\"command\":\"channel_map\",\"sweeps\":25,\"dwell_us\":45}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "JSON missing required fields should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_missing_argument, outcome.error_message);
}

static void test_parse_command_rejects_json_with_invalid_value_types(void) {
  const char* line = "{\"command\":\"channel_map\",\"sweeps\":\"NaN\",\"dwell_us\":60,\"wiper_code\":0x03}";

  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(line, k_expected_command);

  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "JSON with non-numeric sweeps should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_value, outcome.error_message);
}

static void test_parse_command_rejects_unknown_arguments(void) {
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line("channel_map unsupported=1", k_expected_command);

  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Unknown argument should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_unknown_argument, outcome.error_message);
}

static void test_parse_command_rejects_malformed_key_value_token(void) {
  const PhoenixBenchmarkChannelMapParseOutcome outcome = phoenix_benchmark_channel_map_parse_command_line(
      "channel_map sweeps=100 invalidtoken dwell_us=40", k_expected_command);

  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Malformed key/value token should fail");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_value, outcome.error_message);
}

static void test_parse_command_rejects_key_value_with_invalid_value(void) {
  const PhoenixBenchmarkChannelMapParseOutcome outcome = phoenix_benchmark_channel_map_parse_command_line(
      "channel_map sweeps=ten dwell_us=50 wiper=0x05", k_expected_command);

  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Non-numeric value should fail parsing");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_value, outcome.error_message);
}

static void test_parse_command_rejects_empty_input(void) {
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line("   \t\n", k_expected_command);

  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Whitespace-only input should be rejected");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_command, outcome.error_message);
}

static void test_parse_command_rejects_null_input(void) {
  const PhoenixBenchmarkChannelMapParseOutcome outcome =
      phoenix_benchmark_channel_map_parse_command_line(nullptr, k_expected_command);

  TEST_ASSERT_FALSE_MESSAGE(outcome.success, "Null input should be rejected");
  TEST_ASSERT_NOT_NULL(outcome.error_message);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_channel_map_error_invalid_command, outcome.error_message);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();

  RUN_TEST(test_parse_command_accepts_plain_command);
  RUN_TEST(test_parse_command_supports_key_value_overrides);
  RUN_TEST(test_parse_command_supports_key_value_random_order);
  RUN_TEST(test_parse_command_accepts_json_payload);
  RUN_TEST(test_parse_command_accepts_json_with_random_order);
  RUN_TEST(test_parse_command_rejects_json_with_unknown_arguments);
  RUN_TEST(test_parse_command_rejects_json_with_missing_arguments);
  RUN_TEST(test_parse_command_rejects_json_with_invalid_value_types);
  RUN_TEST(test_parse_command_rejects_unknown_arguments);
  RUN_TEST(test_parse_command_rejects_malformed_key_value_token);
  RUN_TEST(test_parse_command_rejects_key_value_with_invalid_value);
  RUN_TEST(test_parse_command_rejects_empty_input);
  RUN_TEST(test_parse_command_rejects_null_input);

  UNITY_END();
}

void loop() {
}
