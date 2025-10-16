#include "adc_speed/adc_speed_command_parser.hpp"
#include "adc_speed/adc_speed_formatter.hpp"
#include "channel_map/channel_map_formatter.hpp"
#include "channel_map/channel_map_support.hpp"
#include "core/phoenix_benchmark_core.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <cstring>
#include <unity.h>

static void test_running_stats_accumulates_integer_values(void) {
  // Step 1. Create an empty integer stats tracker and verify it starts without samples.
  PhoenixBenchmarkRunningStats<int32_t> stats;
  TEST_ASSERT_FALSE_MESSAGE(stats.has_samples(), "Stats should start empty");

  // Step 2. Feed three values representing a simple ramp.
  stats.update(10);
  stats.update(20);
  stats.update(30);

  // Step 3. Confirm the statistics reflect the submitted data set.
  TEST_ASSERT_TRUE(stats.has_samples());
  TEST_ASSERT_EQUAL_UINT32(3u, stats.count);
  TEST_ASSERT_EQUAL_INT32(10, stats.min_value);
  TEST_ASSERT_EQUAL_INT32(30, stats.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 20.0, stats.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 10.0, stats.standard_deviation());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 20.0, stats.range());
}

static void test_running_stats_handles_single_sample_std_zero(void) {
  // Step 1. Initialise stats and push a single sample to exercise zero-variance handling.
  PhoenixBenchmarkRunningStats<uint32_t> stats;
  stats.update(123u);

  // Step 2. Validate the counters and variance-related helpers collapse to zero.
  TEST_ASSERT_EQUAL_UINT32(1u, stats.count);
  TEST_ASSERT_EQUAL_UINT32(123u, stats.min_value);
  TEST_ASSERT_EQUAL_UINT32(123u, stats.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.0, stats.standard_deviation());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.0, stats.range());
}

static void test_state_accumulator_tracks_channel_metrics(void) {
  // Step 1. Create an accumulator and seed it with channel A and B samples.
  PhoenixBenchmarkStateAccumulator accumulator;

  accumulator.channel_a_codes.update(100);
  accumulator.channel_a_codes.update(140);
  accumulator.channel_b_codes.update(-50);
  accumulator.channel_b_codes.update(-30);

  // Step 2. Confirm both channels observed the expected number of samples.
  TEST_ASSERT_EQUAL_UINT32(2u, accumulator.channel_a_codes.count);
  TEST_ASSERT_EQUAL_UINT32(2u, accumulator.channel_b_codes.count);

  // Step 3. Validate channel A statistics including standard deviation.
  TEST_ASSERT_EQUAL_INT32(100, accumulator.channel_a_codes.min_value);
  TEST_ASSERT_EQUAL_INT32(140, accumulator.channel_a_codes.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 120.0, accumulator.channel_a_codes.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 28.2842712475, accumulator.channel_a_codes.standard_deviation());

  // Step 4. Validate channel B statistics including standard deviation.
  TEST_ASSERT_EQUAL_INT32(-50, accumulator.channel_b_codes.min_value);
  TEST_ASSERT_EQUAL_INT32(-30, accumulator.channel_b_codes.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, -40.0, accumulator.channel_b_codes.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 14.1421356237, accumulator.channel_b_codes.standard_deviation());

  // Step 5. Ensure no saturation events were recorded for either channel.
  TEST_ASSERT_EQUAL_UINT32(0u, accumulator.channel_a_saturation_count);
  TEST_ASSERT_EQUAL_UINT32(0u, accumulator.channel_b_saturation_count);
}

static void test_determine_dominant_channel_prefers_channel_a_when_delta_exceeds_drain(void) {
  // Step 1. Assemble a drain baseline with nearly equal amplitudes across channels.
  PhoenixBenchmarkStateAccumulator drain_accumulator;
  drain_accumulator.channel_a_codes.update(100);
  drain_accumulator.channel_a_codes.update(102);
  drain_accumulator.channel_b_codes.update(98);
  drain_accumulator.channel_b_codes.update(99);

  // Step 2. Populate a LED1 accumulator where channel A clearly dominates.
  PhoenixBenchmarkStateAccumulator led1_accumulator;
  led1_accumulator.channel_a_codes.update(500);
  led1_accumulator.channel_a_codes.update(520);
  led1_accumulator.channel_b_codes.update(110);
  led1_accumulator.channel_b_codes.update(120);

  // Step 3. Verify the helper selects channel A when the delta exceeds the drain threshold.
  const PhoenixBenchmarkChannel channel =
      phoenix_benchmark_channel_map_determine_dominant_channel(drain_accumulator, led1_accumulator, 5.0);
  TEST_ASSERT_EQUAL(PhoenixBenchmarkChannel::kChannelA, channel);
}

static void test_determine_dominant_channel_returns_unknown_when_deltas_similar(void) {
  // Step 1. Populate the drain and LED accumulators with similar channel deltas.
  PhoenixBenchmarkStateAccumulator drain_accumulator;
  drain_accumulator.channel_a_codes.update(200);
  drain_accumulator.channel_a_codes.update(205);
  drain_accumulator.channel_b_codes.update(195);
  drain_accumulator.channel_b_codes.update(198);

  PhoenixBenchmarkStateAccumulator led2_accumulator;
  led2_accumulator.channel_a_codes.update(203);
  led2_accumulator.channel_a_codes.update(206);
  led2_accumulator.channel_b_codes.update(197);
  led2_accumulator.channel_b_codes.update(199);

  // Step 2. Expect the helper to classify the result as unknown because the deltas align.
  const PhoenixBenchmarkChannel channel =
      phoenix_benchmark_channel_map_determine_dominant_channel(drain_accumulator, led2_accumulator, 5.0);
  TEST_ASSERT_EQUAL(PhoenixBenchmarkChannel::kUnknown, channel);
}

static void test_format_channel_alignment_label_formats_mismatch_note(void) {
  // Step 1. Format a mismatch between expected and observed channels.
  char buffer[k_phoenix_benchmark_channel_map_summary_map_width + 1u];
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_format_alignment_label(
      PhoenixBenchmarkChannel::kChannelA, PhoenixBenchmarkChannel::kChannelB, buffer, sizeof(buffer)));
  // Step 2. Confirm the formatter emits the shorthand note.
  TEST_ASSERT_EQUAL_STRING("B!=A", buffer);
}

static void test_parse_channel_map_command_extracts_parameters(void) {
  // Step 1. Provide a JSON command with explicit dwell and wiper overrides.
  const char* command_line =
      "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":25,\"dwell_us\":75,\"wiper_code\":120}}";
  PhoenixBenchmarkChannelMapRequest request = {};
  // Step 2. Parse the command and validate each captured field.
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
  TEST_ASSERT_EQUAL_UINT32(25u, request.sweep_count);
  TEST_ASSERT_TRUE(request.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT32(75u, request.dwell_us);
  TEST_ASSERT_TRUE(request.has_wiper_override);
  TEST_ASSERT_EQUAL_UINT8(120u, request.wiper_code);
}

static void test_parse_channel_map_command_treats_missing_dwell_as_default(void) {
  // Step 1. Supply a minimal command with only the sweep count specified.
  const char*                       command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":3}}";
  PhoenixBenchmarkChannelMapRequest request      = {};
  // Step 2. Confirm the parser applies defaults to optional fields.
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
  TEST_ASSERT_EQUAL_UINT32(3u, request.sweep_count);
  TEST_ASSERT_FALSE(request.has_dwell_override);
  TEST_ASSERT_FALSE(request.has_wiper_override);
}

static void test_parse_channel_map_command_rejects_out_of_range_wiper(void) {
  // Step 1. Attempt to parse a command whose wiper code exceeds the allowed range.
  const char* command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":5,\"wiper_code\":300}}";
  PhoenixBenchmarkChannelMapRequest request = {};
  // Step 2. Expect the helper to reject the payload.
  TEST_ASSERT_FALSE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
}

static void test_parse_channel_map_command_rejects_invalid_payload(void) {
  // Step 1. Provide a payload that violates documented sweep limits.
  const char*                       command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":0}}";
  PhoenixBenchmarkChannelMapRequest request      = {};
  // Step 2. Confirm the parser reports failure when validation fails.
  TEST_ASSERT_FALSE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
}

static void test_is_adc_code_saturated_detects_full_scale_codes(void) {
  // Step 1. Assert the helper flags both positive and negative full-scale codes as saturated.
  TEST_ASSERT_TRUE(phoenix_benchmark_is_adc_code_saturated(8388607));
  TEST_ASSERT_TRUE(phoenix_benchmark_is_adc_code_saturated(-8388608));
  // Step 2. Ensure mid-scale values are treated as unsaturated.
  TEST_ASSERT_FALSE(phoenix_benchmark_is_adc_code_saturated(0));
  TEST_ASSERT_FALSE(phoenix_benchmark_is_adc_code_saturated(1024));
}

static void test_adc_speed_format_summary_header_renders_expected_columns(void) {
  // Step 1. Invoke the header formatter and confirm it reports success.
  char buffer[k_phoenix_benchmark_adc_speed_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_adc_speed_format_summary_header(buffer, sizeof(buffer)));

  // Step 2. Ensure the header uses the agreed column labels and alignment spacing.
  TEST_ASSERT_EQUAL_STRING(
    "Mode          Samples_per_s        Loop_us     Errors Notes",
    buffer);
}

static void test_adc_speed_format_summary_row_formats_metrics(void) {
  // Step 1. Format a row with populated metrics so numeric alignment can be asserted.
  PhoenixBenchmarkAdcSpeedSummaryRowValues values = {
      .mode_label          = "Blocking",
      .samples_per_second  = 12345.678,
      .loop_microseconds   = 42.5,
      .error_count         = 3u,
      .notes               = "ok",
      .has_metrics         = true,
  };

  char buffer[k_phoenix_benchmark_adc_speed_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_adc_speed_format_summary_row(values, buffer, sizeof(buffer)));

  // Step 2. Verify the rendered row contains the formatted metrics with fixed precision.
  TEST_ASSERT_NOT_NULL(strstr(buffer, "Blocking"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "12345.68"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "42.500"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "3"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "ok"));
}

static void test_adc_speed_format_summary_row_uses_placeholders_without_metrics(void) {
  // Step 1. Produce a row where metrics are absent so placeholders should appear.
  PhoenixBenchmarkAdcSpeedSummaryRowValues values = {
      .mode_label          = "IRQ",
      .samples_per_second  = 0.0,
      .loop_microseconds   = 0.0,
      .error_count         = 0u,
      .notes               = nullptr,
      .has_metrics         = false,
  };

  char buffer[k_phoenix_benchmark_adc_speed_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_adc_speed_format_summary_row(values, buffer, sizeof(buffer)));

  // Step 2. Check that placeholder markers were emitted in place of real metrics.
  TEST_ASSERT_NOT_NULL(strstr(buffer, "IRQ"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "--"));
}

static void test_adc_speed_parse_command_line_accepts_json_payload(void) {
  // Step 1. Supply a JSON command enabling both blocking and IRQ modes with a custom duration.
  const char* command_line =
      "{\"command\":\"adc_speed\",\"parameters\":{\"duration_ms\":1500,\"enable_blocking\":true,\"enable_irq\":true}}";

  const PhoenixBenchmarkAdcSpeedParseOutcome outcome =
      phoenix_benchmark_adc_speed_parse_command_line(command_line);

  // Step 2. Expect the parser to accept the payload and surface the supplied configuration.
  TEST_ASSERT_TRUE(outcome.success);
  TEST_ASSERT_EQUAL_UINT32(1500u, outcome.options.duration_ms);
  TEST_ASSERT_TRUE(outcome.options.enable_blocking);
  TEST_ASSERT_TRUE(outcome.options.enable_irq);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_adc_speed_parse_command_line_rejects_invalid_duration(void) {
  // Step 1. Attempt to parse an invalid payload with a zero duration value.
  const char* command_line =
      "{\"command\":\"adc_speed\",\"parameters\":{\"duration_ms\":0}}";

  const PhoenixBenchmarkAdcSpeedParseOutcome outcome =
      phoenix_benchmark_adc_speed_parse_command_line(command_line);

  // Step 2. The parser should reject the command and surface an invalid value error.
  TEST_ASSERT_FALSE(outcome.success);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_adc_speed_error_invalid_value, outcome.error_message);
}

void setup() {
  // Step 1. Initialise Unity's serial logging channel.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2. Run the full Phoenix benchmark support test suite.
  UNITY_BEGIN();
  RUN_TEST(test_running_stats_accumulates_integer_values);
  RUN_TEST(test_running_stats_handles_single_sample_std_zero);
  RUN_TEST(test_state_accumulator_tracks_channel_metrics);
  RUN_TEST(test_determine_dominant_channel_prefers_channel_a_when_delta_exceeds_drain);
  RUN_TEST(test_determine_dominant_channel_returns_unknown_when_deltas_similar);
  RUN_TEST(test_format_channel_alignment_label_formats_mismatch_note);
  RUN_TEST(test_parse_channel_map_command_extracts_parameters);
  RUN_TEST(test_parse_channel_map_command_treats_missing_dwell_as_default);
  RUN_TEST(test_parse_channel_map_command_rejects_out_of_range_wiper);
  RUN_TEST(test_parse_channel_map_command_rejects_invalid_payload);
  RUN_TEST(test_is_adc_code_saturated_detects_full_scale_codes);
  RUN_TEST(test_adc_speed_format_summary_header_renders_expected_columns);
  RUN_TEST(test_adc_speed_format_summary_row_formats_metrics);
  RUN_TEST(test_adc_speed_format_summary_row_uses_placeholders_without_metrics);
  RUN_TEST(test_adc_speed_parse_command_line_accepts_json_payload);
  RUN_TEST(test_adc_speed_parse_command_line_rejects_invalid_duration);
  // Step 3. Finalise Unity before idling in loop().
  UNITY_END();
}

void loop() {
}
