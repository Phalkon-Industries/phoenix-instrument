#include "ad524x.hpp"
#include "channel_map/channel_map.hpp"
#include "device_setup.hpp"
#include "light_readings.hpp"
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
  light_readings_reset_for_test();
  light_readings_force_saturation_for_test(false);
}

}  // namespace

void setUp(void) {
  // Step 1. Ensure each test starts from the shared default configuration.
  device_setup_initialize();
  reset_defaults();
}

void tearDown(void) {
  // Step 1. Reset residual state so follow-up tests begin cleanly.
  phoenix_benchmark_channel_map_reset_state();
  light_readings_reset_for_test();
  light_readings_force_saturation_for_test(false);
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

static void test_parse_command_supports_sweep_override(void) {
  // Step 1. Provide a sweep override using the key/value syntax.
  const char*                                 command_line = "channel_map sweeps=200";
  const PhoenixBenchmarkChannelMapParseResult result       = phoenix_benchmark_channel_map_parse_command(command_line);

  // Step 2. Confirm the parser accepts the sweep override and leaves other fields untouched.
  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected sweep override to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(200u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(k_defaults.dwell_us, result.options.dwell_us);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(k_defaults.wiper_code, result.options.wiper_code);
  TEST_ASSERT_FALSE(result.options.has_wiper_override);
}

static void test_parse_command_rejects_dwell_override(void) {
  // Step 1. Attempt to provide a dwell override which should be unsupported.
  const PhoenixBenchmarkChannelMapParseResult result =
      phoenix_benchmark_channel_map_parse_command("channel_map dwell_us=75");

  // Step 2. Expect the parser to reject the unsupported argument.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "Dwell overrides should no longer be accepted");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_wiper_override(void) {
  // Step 1. Attempt to provide a wiper override which should be unsupported.
  const PhoenixBenchmarkChannelMapParseResult result =
      phoenix_benchmark_channel_map_parse_command("channel_map wiper=0x2A");

  // Step 2. Expect the parser to reject the unsupported argument.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "Wiper overrides should no longer be accepted");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_accepts_json_payload(void) {
  // Step 1. Provide a JSON payload with an explicit sweep override.
  const char*                                 json_line = "{\"command\":\"channel_map\",\"sweeps\":150}";
  const PhoenixBenchmarkChannelMapParseResult result    = phoenix_benchmark_channel_map_parse_command(json_line);

  // Step 2. Confirm the JSON parser recognises the sweep override while ignoring absent fields.
  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected JSON command with sweeps to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(150u, result.options.sweep_count);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(k_defaults.dwell_us, result.options.dwell_us);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(k_defaults.wiper_code, result.options.wiper_code);
  TEST_ASSERT_FALSE(result.options.has_wiper_override);
}

static void test_parse_command_accepts_minimal_json_payload(void) {
  // Step 1. Provide a JSON payload containing only the command token.
  const char*                                 json_line = "{\"command\":\"channel_map\"}";
  const PhoenixBenchmarkChannelMapParseResult result    = phoenix_benchmark_channel_map_parse_command(json_line);

  // Step 2. Expect the parser to fall back to defaults without marking overrides.
  TEST_ASSERT_TRUE_MESSAGE(result.success, "Expected minimal JSON payload to parse successfully");
  TEST_ASSERT_EQUAL_UINT32(k_defaults.sweep_count, result.options.sweep_count);
  TEST_ASSERT_FALSE(result.options.has_sweep_override);
  TEST_ASSERT_EQUAL_UINT32(k_defaults.dwell_us, result.options.dwell_us);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT8(k_defaults.wiper_code, result.options.wiper_code);
  TEST_ASSERT_FALSE(result.options.has_wiper_override);
}

static void test_parse_command_rejects_json_with_unknown_arguments(void) {
  // Step 1. Include an unexpected field to trigger JSON validation failure.
  const char*                                 json_line = "{\"command\":\"channel_map\",\"sweeps\":10,\"extra\":1}";
  const PhoenixBenchmarkChannelMapParseResult result    = phoenix_benchmark_channel_map_parse_command(json_line);

  // Step 2. Confirm the parser reports failure and surfaces an error message.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "JSON with unknown key should be rejected");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_json_with_invalid_value_types(void) {
  // Step 1. Provide a JSON payload with a non-numeric sweeps value.
  const char*                                 json_line = "{\"command\":\"channel_map\",\"sweeps\":\"NaN\"}";
  const PhoenixBenchmarkChannelMapParseResult result    = phoenix_benchmark_channel_map_parse_command(json_line);

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
      phoenix_benchmark_channel_map_parse_command("channel_map sweeps=100 invalidtoken");

  // Step 2. Verify the parser treats the malformed token as a fatal error.
  TEST_ASSERT_FALSE_MESSAGE(result.success, "Malformed key/value token should fail parsing");
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_parse_command_rejects_key_value_with_invalid_value(void) {
  // Step 1. Attempt to parse a command where a numeric field is spelled out.
  const PhoenixBenchmarkChannelMapParseResult result =
      phoenix_benchmark_channel_map_parse_command("channel_map sweeps=ten");

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
  light_readings_force_saturation_for_test(true);

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

  TEST_ASSERT_EQUAL_UINT32(0u, accumulators[1].channel_a_saturation_count);
  TEST_ASSERT_EQUAL_UINT32(0u, accumulators[1].channel_b_saturation_count);
  TEST_ASSERT_EQUAL_UINT32(0u, accumulators[2].channel_a_saturation_count);
  TEST_ASSERT_EQUAL_UINT32(0u, accumulators[2].channel_b_saturation_count);

  light_readings_force_saturation_for_test(false);
}

void setup() {
  // Step 1. Initialise Unity's serial bridge for the channel map tests.
  UNITY_SETUP_SERIAL_DEFAULT();
  device_setup_initialize();

  // Step 2. Execute every test case covering parsing and runtime behaviour.
  RUN_TEST(test_parse_command_accepts_plain_channel_map);
  RUN_TEST(test_parse_command_supports_sweep_override);
  RUN_TEST(test_parse_command_rejects_dwell_override);
  RUN_TEST(test_parse_command_rejects_wiper_override);
  RUN_TEST(test_parse_command_accepts_json_payload);
  RUN_TEST(test_parse_command_accepts_minimal_json_payload);
  RUN_TEST(test_parse_command_rejects_json_with_unknown_arguments);
  RUN_TEST(test_parse_command_rejects_json_with_invalid_value_types);
  RUN_TEST(test_parse_command_rejects_unknown_arguments);
  RUN_TEST(test_parse_command_rejects_malformed_key_value_token);
  RUN_TEST(test_parse_command_rejects_key_value_with_invalid_value);
  RUN_TEST(test_channel_map_exposes_state_descriptors);
  RUN_TEST(test_channel_map_run_executes_full_sequence);
  RUN_TEST(test_channel_map_run_reports_errors);
  RUN_TEST(test_channel_map_run_records_saturation_warning);

  // Step 3. Finish the Unity session prior to entering the idle loop.
  UNITY_END();
}

void loop() {
}
