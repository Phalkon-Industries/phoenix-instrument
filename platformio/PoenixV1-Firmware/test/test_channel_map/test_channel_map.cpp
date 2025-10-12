#include "channel_map.hpp"
#include "main.hpp"
#include "phoenix_benchmark_support.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <unity.h>

namespace {

const phoenix_benchmark::ChannelMapDefaults k_defaults = {
    .sweep_count         = 100u,
    .dwell_us            = 100u,
    .wiper_code          = 0x10u,
    .include_drain_state = true,
};

void reset_defaults(void) {
  phoenix_benchmark::channel_map::reset_state();
  phoenix_benchmark::channel_map::initialise(k_defaults);
}

}  // namespace

void setUp(void) {
  reset_defaults();
}

void tearDown(void) {
  phoenix_benchmark::channel_map::reset_state();
}

static void test_parse_command_accepts_plain_channel_map(void) {
  const phoenix_benchmark::ParseResult result = phoenix_benchmark::channel_map::parse_command("channel_map");

  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected bare channel_map command to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(k_defaults.sweep_count, result.options.sweep_count);
  TEST_ASSERT_FALSE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(k_defaults.dwell_us, result.options.dwell_us);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(k_defaults.wiper_code, result.options.wiper_code);
  TEST_ASSERT_FALSE(result.options.has_wiper_override);
}

static void test_parse_command_supports_key_value_overrides(void) {
  const char*                          command_line = "channel_map sweeps=200 dwell_us=75 wiper=0x2A";
  const phoenix_benchmark::ParseResult result       = phoenix_benchmark::channel_map::parse_command(command_line);

  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected key/value command to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(200u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(75u, result.options.dwell_us);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x2Au, result.options.wiper_code);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
}

static void test_parse_command_supports_key_value_random_order(void) {
  const char*                          command_line = "channel_map wiper=0x33 sweeps=175 dwell_us=65";
  const phoenix_benchmark::ParseResult result       = phoenix_benchmark::channel_map::parse_command(command_line);

  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected key/value command to handle random argument order");
  TEST_ASSERT_EQUAL_UINT32(175u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(65u, result.options.dwell_us);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x33u, result.options.wiper_code);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
}

static void test_parse_command_accepts_json_payload(void) {
  const char* json_line = "{\"command\":\"channel_map\",\"sweeps\":150,\"dwell_us\":60,\"wiper_code\":0x05}";
  const phoenix_benchmark::ParseResult result = phoenix_benchmark::channel_map::parse_command(json_line);

  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected JSON command to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(150u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(60u, result.options.dwell_us);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x05u, result.options.wiper_code);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
}

static void test_parse_command_accepts_json_with_random_order(void) {
  const char* json_line = "{\"wiper_code\":0x09,\"dwell_us\":80,\"command\":\"channel_map\",\"sweeps\":40}";
  const phoenix_benchmark::ParseResult result = phoenix_benchmark::channel_map::parse_command(json_line);

  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected JSON command with shuffled keys to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(40u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(80u, result.options.dwell_us);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x09u, result.options.wiper_code);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
}

static void test_parse_command_rejects_json_with_unknown_arguments(void) {
  const char* json_line = "{\"command\":\"channel_map\",\"sweeps\":10,\"dwell_us\":25,\"wiper_code\":0x04,\"extra\":1}";
  const phoenix_benchmark::ParseResult result = phoenix_benchmark::channel_map::parse_command(json_line);

  TEST_ASSERT_FALSE_MESSAGE(result.success, "JSON with unknown key should be rejected");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_json_with_missing_arguments(void) {
  const char*                          json_line = "{\"command\":\"channel_map\",\"sweeps\":25,\"dwell_us\":45}";
  const phoenix_benchmark::ParseResult result    = phoenix_benchmark::channel_map::parse_command(json_line);

  TEST_ASSERT_FALSE_MESSAGE(result.success, "JSON missing required wiper_code should fail");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_json_with_invalid_value_types(void) {
  const char* json_line = "{\"command\":\"channel_map\",\"sweeps\":\"NaN\",\"dwell_us\":60,\"wiper_code\":0x03}";
  const phoenix_benchmark::ParseResult result = phoenix_benchmark::channel_map::parse_command(json_line);

  TEST_ASSERT_FALSE_MESSAGE(result.success, "JSON with non-numeric sweeps should fail");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_unknown_arguments(void) {
  const phoenix_benchmark::ParseResult result =
      phoenix_benchmark::channel_map::parse_command("channel_map unsupported=1");

  TEST_ASSERT_FALSE_MESSAGE(result.success, "Unknown argument should cause parse to fail");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_malformed_key_value_token(void) {
  const phoenix_benchmark::ParseResult result =
      phoenix_benchmark::channel_map::parse_command("channel_map sweeps=100 invalidtoken dwell_us=40");

  TEST_ASSERT_FALSE_MESSAGE(result.success, "Malformed key/value token should fail parsing");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_key_value_with_invalid_value(void) {
  const phoenix_benchmark::ParseResult result =
      phoenix_benchmark::channel_map::parse_command("channel_map sweeps=ten dwell_us=50 wiper=0x05");

  TEST_ASSERT_FALSE_MESSAGE(result.success, "Non-numeric value should fail parsing");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_channel_map_exposes_state_descriptors(void) {
  const size_t descriptor_count = phoenix_benchmark::channel_map::k_state_descriptor_count;
  TEST_ASSERT_TRUE_MESSAGE(descriptor_count > 0u, "Expected at least one state descriptor");

  const phoenix_benchmark::ChannelMapStateDescriptor* descriptors = phoenix_benchmark::channel_map::state_descriptors();
  TEST_ASSERT_NOT_NULL(descriptors);

  const size_t drain_index = phoenix_benchmark::channel_map::k_drain_state_index;
  TEST_ASSERT_TRUE_MESSAGE(drain_index < descriptor_count, "Drain index must fall within descriptor range");
  TEST_ASSERT_NOT_NULL(descriptors[drain_index].label);
  TEST_ASSERT_EQUAL(phoenix_benchmark_support::BenchmarkChannel::kUnknown, descriptors[drain_index].expected_channel);
}

static void test_channel_map_run_executes_full_sequence(void) {
  phoenix_benchmark::ChannelMapOptions options = {
      .sweep_count        = 0u,
      .has_sweep_override = false,
      .dwell_us           = 0u,
      .has_dwell_override = false,
      .wiper_code         = 0u,
      .has_wiper_override = false,
  };
  options.apply_defaults(k_defaults);

  phoenix_benchmark_support::StateAccumulator accumulators[3] = {};
  const phoenix_benchmark::OutputCallbacks    callbacks       = {
               .print_line  = nullptr,
               .print_ready = nullptr,
  };

  const phoenix_benchmark::ExecutionStatus status =
      phoenix_benchmark::channel_map::run(options, accumulators, callbacks);

  if (!status.success && status.message != nullptr) {
    TEST_MESSAGE(status.message);
  }

  TEST_ASSERT_TRUE_MESSAGE(status.success, "Channel map run should succeed with default options");
  TEST_ASSERT_EQUAL(phoenix_benchmark::PHOENIX_BENCHMARK_OK, status.return_code);

  TEST_ASSERT_TRUE(accumulators[0].channel_a_codes.has_samples());
  TEST_ASSERT_TRUE(accumulators[0].channel_b_codes.has_samples());
}

static void test_channel_map_run_reports_errors(void) {
  phoenix_benchmark::ChannelMapOptions options = {
      .sweep_count        = 0u,
      .has_sweep_override = false,
      .dwell_us           = 0u,
      .has_dwell_override = false,
      .wiper_code         = 0u,
      .has_wiper_override = false,
  };

  phoenix_benchmark_support::StateAccumulator accumulators[3] = {};
  const phoenix_benchmark::OutputCallbacks    callbacks       = {
               .print_line  = nullptr,
               .print_ready = nullptr,
  };

  const phoenix_benchmark::ExecutionStatus status =
      phoenix_benchmark::channel_map::run(options, accumulators, callbacks);

  TEST_ASSERT_FALSE(status.success);
  TEST_ASSERT_NOT_EQUAL(phoenix_benchmark::PHOENIX_BENCHMARK_OK, status.return_code);
  TEST_ASSERT_NOT_NULL(status.message);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();

  RUN_TEST(test_parse_command_accepts_plain_channel_map);
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
  RUN_TEST(test_channel_map_exposes_state_descriptors);
  RUN_TEST(test_channel_map_run_executes_full_sequence);
  RUN_TEST(test_channel_map_run_reports_errors);

  UNITY_END();
}

void loop() {
}
