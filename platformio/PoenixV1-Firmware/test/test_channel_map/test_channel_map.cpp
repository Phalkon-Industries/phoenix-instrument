#include "ad524x.hpp"
#include "channel_map/channel_map.hpp"
#include "channel_map/command_parser.hpp"
#include "main.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <cstring>
#include <unity.h>

namespace {

const PhoenixBenchmarkChannelMapDefaults k_defaults = {
    .sweep_count = 100u,
    .dwell_us    = 100u,
    .wiper_code  = 0x10u,
};

void reset_defaults(void) {
  // Step 1. Clear any prior state and reapply the canonical defaults.
  phoenix_benchmark_channel_map_reset_state();
  phoenix_benchmark_channel_map_initialise(k_defaults);
}

}  // namespace

void setUp(void) {
  // Step 1. Ensure each test starts from the shared default configuration.
  reset_defaults();
}

void tearDown(void) {
  // Step 1. Reset residual state so follow-up tests begin cleanly.
  phoenix_benchmark_channel_map_reset_state();
}

static void test_parse_command_accepts_plain_channel_map(void) {
  // Step 1. Parse the bare keyword command with no overrides.
  const PhoenixBenchmarkChannelMapParseResult result = phoenix_benchmark_channel_map_parse_command("channel_map");

  // Step 2. Validate the parser reports success and keeps default options.
  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected bare channel_map command to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(k_defaults.sweep_count, result.options.sweep_count);
  TEST_ASSERT_FALSE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(k_defaults.dwell_us, result.options.dwell_us);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(k_defaults.wiper_code, result.options.wiper_code);
  TEST_ASSERT_FALSE(result.options.has_wiper_override);
}

static void test_parse_command_supports_key_value_overrides(void) {
  // Step 1. Provide overrides using key/value tokens in canonical order.
  const char*                                 command_line = "channel_map sweeps=200 dwell_us=75 wiper=0x2A";
  const PhoenixBenchmarkChannelMapParseResult result       = phoenix_benchmark_channel_map_parse_command(command_line);

  // Step 2. Confirm the parser accepts the overrides and marks each as explicit.
  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected key/value command to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(200u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(75u, result.options.dwell_us);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x2Au, result.options.wiper_code);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
}

static void test_parse_command_supports_key_value_random_order(void) {
  // Step 1. Shuffle the key/value tokens to ensure ordering does not matter.
  const char*                                 command_line = "channel_map wiper=0x33 sweeps=175 dwell_us=65";
  const PhoenixBenchmarkChannelMapParseResult result       = phoenix_benchmark_channel_map_parse_command(command_line);

  // Step 2. Expect the parser to capture identical overrides regardless of token order.
  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected key/value command to handle random argument order");
  TEST_ASSERT_EQUAL_UINT32(175u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(65u, result.options.dwell_us);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x33u, result.options.wiper_code);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
}

static void test_parse_command_accepts_json_payload(void) {
  // Step 1. Provide a JSON payload with numeric overrides encoded directly.
  const char* json_line = "{\"command\":\"channel_map\",\"sweeps\":150,\"dwell_us\":60,\"wiper_code\":0x05}";
  const PhoenixBenchmarkChannelMapParseResult result = phoenix_benchmark_channel_map_parse_command(json_line);

  // Step 2. Confirm the JSON parser recognises each override.
  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected JSON command to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(150u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(60u, result.options.dwell_us);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x05u, result.options.wiper_code);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
}

static void test_parse_command_accepts_json_with_random_order(void) {
  // Step 1. Shuffle JSON keys to ensure the parser is order agnostic.
  const char* json_line = "{\"wiper_code\":0x09,\"dwell_us\":80,\"command\":\"channel_map\",\"sweeps\":40}";
  const PhoenixBenchmarkChannelMapParseResult result = phoenix_benchmark_channel_map_parse_command(json_line);

  // Step 2. Expect the command to parse successfully with equivalent overrides.
  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected JSON command with shuffled keys to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(40u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(80u, result.options.dwell_us);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(0x09u, result.options.wiper_code);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
}

static void test_parse_command_rejects_json_with_unknown_arguments(void) {
  // Step 1. Include an unexpected field to trigger JSON validation failure.
  const char* json_line = "{\"command\":\"channel_map\",\"sweeps\":10,\"dwell_us\":25,\"wiper_code\":0x04,\"extra\":1}";
  const PhoenixBenchmarkChannelMapParseResult result = phoenix_benchmark_channel_map_parse_command(json_line);

  // Step 2. Confirm the parser reports failure and surfaces an error message.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "JSON with unknown key should be rejected");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_json_with_missing_arguments(void) {
  // Step 1. Omit the wiper code to ensure required fields are enforced.
  const char*                                 json_line = "{\"command\":\"channel_map\",\"sweeps\":25,\"dwell_us\":45}";
  const PhoenixBenchmarkChannelMapParseResult result    = phoenix_benchmark_channel_map_parse_command(json_line);

  // Step 2. Expect the parser to fail and provide diagnostic text.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "JSON missing required wiper_code should fail");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_json_with_invalid_value_types(void) {
  // Step 1. Provide a JSON payload with a non-numeric sweeps value.
  const char* json_line = "{\"command\":\"channel_map\",\"sweeps\":\"NaN\",\"dwell_us\":60,\"wiper_code\":0x03}";
  const PhoenixBenchmarkChannelMapParseResult result = phoenix_benchmark_channel_map_parse_command(json_line);

  // Step 2. Confirm the parser surfaces an error for the invalid type.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "JSON with non-numeric sweeps should fail");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_unknown_arguments(void) {
  // Step 1. Add an unsupported key/value pair to the plain text command.
  const PhoenixBenchmarkChannelMapParseResult result =
      phoenix_benchmark_channel_map_parse_command("channel_map unsupported=1");

  // Step 2. Expect parsing to fail and return an explanatory error message.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "Unknown argument should cause parse to fail");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_malformed_key_value_token(void) {
  // Step 1. Inject an invalid token lacking an equals sign into the command.
  const PhoenixBenchmarkChannelMapParseResult result =
      phoenix_benchmark_channel_map_parse_command("channel_map sweeps=100 invalidtoken dwell_us=40");

  // Step 2. Verify the parser treats the malformed token as a fatal error.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "Malformed key/value token should fail parsing");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_key_value_with_invalid_value(void) {
  // Step 1. Attempt to parse a command where a numeric field is spelled out.
  const PhoenixBenchmarkChannelMapParseResult result =
      phoenix_benchmark_channel_map_parse_command("channel_map sweeps=ten dwell_us=50 wiper=0x05");

  // Step 2. Ensure the parser flags the non-numeric value as invalid.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "Non-numeric value should fail parsing");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_channel_map_exposes_state_descriptors(void) {
  // Step 1. Fetch the descriptor array and confirm basic metadata is present.
  const size_t descriptor_count = k_phoenix_benchmark_channel_map_state_descriptor_count;
  TEST_ASSERT_TRUE_MESSAGE(descriptor_count > 0u, "Expected at least one state descriptor");

  const PhoenixBenchmarkChannelMapStateDescriptor* descriptors = phoenix_benchmark_channel_map_state_descriptors();
  TEST_ASSERT_NOT_NULL(descriptors);

  // Step 2. Verify the drain descriptor exists and reports the expected channel.
  const size_t drain_index = k_phoenix_benchmark_channel_map_drain_state_index;
  TEST_ASSERT_TRUE_MESSAGE(drain_index < descriptor_count, "Drain index must fall within descriptor range");
  TEST_ASSERT_NOT_NULL(descriptors[drain_index].label);
  TEST_ASSERT_EQUAL(PhoenixBenchmarkChannel::kUnknown, descriptors[drain_index].expected_channel);
}

static void test_channel_map_run_executes_full_sequence(void) {
  // Step 1. Start with zeroed options and apply the defaults to populate fields.
  PhoenixBenchmarkChannelMapOptions options = {
      .sweep_count        = 0u,
      .has_sweep_override = false,
      .dwell_us           = 0u,
      .has_dwell_override = false,
      .wiper_code         = 0u,
      .has_wiper_override = false,
  };
  options.apply_defaults(k_defaults);

  // Step 2. Execute the channel map run with null printing callbacks.
  PhoenixBenchmarkStateAccumulator                accumulators[3] = {};
  const PhoenixBenchmarkChannelMapOutputCallbacks callbacks       = {
            .print_line  = nullptr,
            .print_ready = nullptr,
  };

  const PhoenixBenchmarkChannelMapExecutionStatus status =
      phoenix_benchmark_channel_map_run(options, accumulators, callbacks);

  if (!status.success && status.message != nullptr) {
    TEST_MESSAGE(status.message);
  }

  // Step 3. Assert the run succeeded and produced samples for the drain state.
  TEST_ASSERT_TRUE_MESSAGE(status.success, "Channel map run should succeed with default options");
  TEST_ASSERT_EQUAL(PHOENIX_BENCHMARK_OK, status.return_code);
  TEST_ASSERT_TRUE(accumulators[0].channel_a_codes.has_samples());
  TEST_ASSERT_TRUE(accumulators[0].channel_b_codes.has_samples());
}

static void test_channel_map_run_reports_errors(void) {
  // Step 1. Leave the options zeroed to provoke validation failures during execution.
  PhoenixBenchmarkChannelMapOptions options = {
      .sweep_count        = 0u,
      .has_sweep_override = false,
      .dwell_us           = 0u,
      .has_dwell_override = false,
      .wiper_code         = 0u,
      .has_wiper_override = false,
  };

  PhoenixBenchmarkStateAccumulator                accumulators[3] = {};
  const PhoenixBenchmarkChannelMapOutputCallbacks callbacks       = {
            .print_line  = nullptr,
            .print_ready = nullptr,
  };

  // Step 2. Execute the run and confirm the failure propagates descriptive metadata.
  const PhoenixBenchmarkChannelMapExecutionStatus status =
      phoenix_benchmark_channel_map_run(options, accumulators, callbacks);

  TEST_ASSERT_FALSE(status.success);
  TEST_ASSERT_NOT_EQUAL(PHOENIX_BENCHMARK_OK, status.return_code);
  TEST_ASSERT_NOT_NULL(status.message);
  TEST_ASSERT_FALSE(status.has_warnings);
}

static void test_channel_map_run_records_saturation_warning(void) {
  // Step 1. Force saturation to test the warning reporting path.
  phoenix_benchmark_channel_map_set_force_saturation_for_test(true);

  PhoenixBenchmarkChannelMapOptions options = {
      .sweep_count        = 0u,
      .has_sweep_override = false,
      .dwell_us           = 0u,
      .has_dwell_override = false,
      .wiper_code         = 0u,
      .has_wiper_override = false,
  };
  options.apply_defaults(k_defaults);
  options.sweep_count        = 1u;
  options.has_sweep_override = true;

  PhoenixBenchmarkStateAccumulator                accumulators[3] = {};
  const PhoenixBenchmarkChannelMapOutputCallbacks callbacks       = {
            .print_line  = nullptr,
            .print_ready = nullptr,
  };

  // Step 2. Execute the run and capture the saturation warning flags.
  const PhoenixBenchmarkChannelMapExecutionStatus status =
      phoenix_benchmark_channel_map_run(options, accumulators, callbacks);

  // Step 3. Confirm the run succeeds yet reports saturation details and counts.
  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_EQUAL(PHOENIX_BENCHMARK_OK, status.return_code);
  TEST_ASSERT_TRUE(status.has_warnings);
  TEST_ASSERT_NULL(status.message);

  TEST_ASSERT_GREATER_THAN_UINT32(0u, accumulators[1].channel_a_saturation_count);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, accumulators[1].channel_b_saturation_count);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, accumulators[2].channel_a_saturation_count);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, accumulators[2].channel_b_saturation_count);

  phoenix_benchmark_channel_map_set_force_saturation_for_test(false);
}

static void test_channel_map_run_applies_wiper_code(void) {
  // Step 1. Override the defaults with a specific wiper code to propagate.
  PhoenixBenchmarkChannelMapOptions options = {
      .sweep_count        = 0u,
      .has_sweep_override = false,
      .dwell_us           = 0u,
      .has_dwell_override = false,
      .wiper_code         = 0u,
      .has_wiper_override = false,
  };
  options.apply_defaults(k_defaults);
  options.wiper_code         = 0x37u;
  options.has_wiper_override = true;

  PhoenixBenchmarkStateAccumulator                accumulators[3] = {};
  const PhoenixBenchmarkChannelMapOutputCallbacks callbacks       = {
            .print_line  = nullptr,
            .print_ready = nullptr,
  };

  // Step 2. Run the sweep and check the saturation path for diagnostic messages.
  const PhoenixBenchmarkChannelMapExecutionStatus status =
      phoenix_benchmark_channel_map_run(options, accumulators, callbacks);

  if (!status.success && status.message != nullptr) {
    TEST_MESSAGE(status.message);
  }
  TEST_ASSERT_TRUE(status.success);

  // Step 3. Read the wiper positions from both channels to confirm the override applied.
  uint8_t wiper_a = 0u;
  uint8_t wiper_b = 0u;
  TEST_ASSERT_EQUAL(AD524X_OK, ad524x_get_wiper(0u, &wiper_a));
  TEST_ASSERT_EQUAL(AD524X_OK, ad524x_get_wiper(1u, &wiper_b));
  TEST_ASSERT_EQUAL_UINT8(options.wiper_code, wiper_a);
  TEST_ASSERT_EQUAL_UINT8(options.wiper_code, wiper_b);
}

void setup() {
  // Step 1. Initialise Unity's serial bridge for the channel map tests.
  UNITY_SETUP_SERIAL_DEFAULT();

  // Step 2. Execute every test case covering parsing and runtime behaviour.
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
  RUN_TEST(test_channel_map_run_records_saturation_warning);
  RUN_TEST(test_channel_map_run_applies_wiper_code);

  // Step 3. Finish the Unity session prior to entering the idle loop.
  UNITY_END();
}

void loop() {
}
