#include "channel_map/channel_map_formatter.hpp"
#include "channel_map/channel_map_support.hpp"
#include "core/phoenix_benchmark_core.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <cstring>
#include <unity.h>

static void test_running_stats_accumulates_integer_values(void) {
  PhoenixBenchmarkRunningStats<int32_t> stats;
  TEST_ASSERT_FALSE_MESSAGE(stats.has_samples(), "Stats should start empty");

  stats.update(10);
  stats.update(20);
  stats.update(30);

  TEST_ASSERT_TRUE(stats.has_samples());
  TEST_ASSERT_EQUAL_UINT32(3u, stats.count);
  TEST_ASSERT_EQUAL_INT32(10, stats.min_value);
  TEST_ASSERT_EQUAL_INT32(30, stats.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 20.0, stats.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 10.0, stats.standard_deviation());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 20.0, stats.range());
}

static void test_running_stats_handles_single_sample_std_zero(void) {
  PhoenixBenchmarkRunningStats<uint32_t> stats;
  stats.update(123u);

  TEST_ASSERT_EQUAL_UINT32(1u, stats.count);
  TEST_ASSERT_EQUAL_UINT32(123u, stats.min_value);
  TEST_ASSERT_EQUAL_UINT32(123u, stats.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.0, stats.standard_deviation());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.0, stats.range());
}

static void test_state_accumulator_tracks_channel_metrics(void) {
  PhoenixBenchmarkStateAccumulator accumulator;

  accumulator.channel_a_codes.update(100);
  accumulator.channel_a_codes.update(140);
  accumulator.channel_b_codes.update(-50);
  accumulator.channel_b_codes.update(-30);

  TEST_ASSERT_EQUAL_UINT32(2u, accumulator.channel_a_codes.count);
  TEST_ASSERT_EQUAL_UINT32(2u, accumulator.channel_b_codes.count);

  TEST_ASSERT_EQUAL_INT32(100, accumulator.channel_a_codes.min_value);
  TEST_ASSERT_EQUAL_INT32(140, accumulator.channel_a_codes.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 120.0, accumulator.channel_a_codes.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 28.2842712475, accumulator.channel_a_codes.standard_deviation());

  TEST_ASSERT_EQUAL_INT32(-50, accumulator.channel_b_codes.min_value);
  TEST_ASSERT_EQUAL_INT32(-30, accumulator.channel_b_codes.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, -40.0, accumulator.channel_b_codes.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 14.1421356237, accumulator.channel_b_codes.standard_deviation());
}

static void test_determine_dominant_channel_prefers_channel_a_when_delta_exceeds_drain(void) {
  PhoenixBenchmarkStateAccumulator drain_accumulator;
  drain_accumulator.channel_a_codes.update(100);
  drain_accumulator.channel_a_codes.update(102);
  drain_accumulator.channel_b_codes.update(98);
  drain_accumulator.channel_b_codes.update(99);

  PhoenixBenchmarkStateAccumulator led1_accumulator;
  led1_accumulator.channel_a_codes.update(500);
  led1_accumulator.channel_a_codes.update(520);
  led1_accumulator.channel_b_codes.update(110);
  led1_accumulator.channel_b_codes.update(120);

  const PhoenixBenchmarkChannel channel =
      phoenix_benchmark_channel_map_determine_dominant_channel(drain_accumulator, led1_accumulator, 5.0);
  TEST_ASSERT_EQUAL(PhoenixBenchmarkChannel::kChannelA, channel);
}

static void test_determine_dominant_channel_returns_unknown_when_deltas_similar(void) {
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

  const PhoenixBenchmarkChannel channel =
      phoenix_benchmark_channel_map_determine_dominant_channel(drain_accumulator, led2_accumulator, 5.0);
  TEST_ASSERT_EQUAL(PhoenixBenchmarkChannel::kUnknown, channel);
}

static void test_format_channel_alignment_label_formats_mismatch_note(void) {
  char buffer[k_phoenix_benchmark_channel_map_summary_map_width + 1u];
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_format_alignment_label(
      PhoenixBenchmarkChannel::kChannelA, PhoenixBenchmarkChannel::kChannelB, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("B!=A", buffer);
}

static void test_parse_channel_map_command_extracts_parameters(void) {
  const char* command_line =
      "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":25,\"dwell_us\":75,\"wiper_code\":120}}";
  PhoenixBenchmarkChannelMapRequest request = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
  TEST_ASSERT_EQUAL_UINT32(25u, request.sweep_count);
  TEST_ASSERT_TRUE(request.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT32(75u, request.dwell_us);
  TEST_ASSERT_TRUE(request.has_wiper_override);
  TEST_ASSERT_EQUAL_UINT8(120u, request.wiper_code);
}

static void test_parse_channel_map_command_treats_missing_dwell_as_default(void) {
  const char*                       command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":3}}";
  PhoenixBenchmarkChannelMapRequest request      = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
  TEST_ASSERT_EQUAL_UINT32(3u, request.sweep_count);
  TEST_ASSERT_FALSE(request.has_dwell_override);
  TEST_ASSERT_FALSE(request.has_wiper_override);
}

static void test_parse_channel_map_command_rejects_out_of_range_wiper(void) {
  const char* command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":5,\"wiper_code\":300}}";
  PhoenixBenchmarkChannelMapRequest request = {};
  TEST_ASSERT_FALSE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
}

static void test_parse_channel_map_command_rejects_invalid_payload(void) {
  const char*                       command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":0}}";
  PhoenixBenchmarkChannelMapRequest request      = {};
  TEST_ASSERT_FALSE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
}

static void test_is_adc_code_saturated_detects_full_scale_codes(void) {
  TEST_ASSERT_TRUE(phoenix_benchmark_is_adc_code_saturated(8388607));
  TEST_ASSERT_TRUE(phoenix_benchmark_is_adc_code_saturated(-8388608));
  TEST_ASSERT_FALSE(phoenix_benchmark_is_adc_code_saturated(0));
  TEST_ASSERT_FALSE(phoenix_benchmark_is_adc_code_saturated(1024));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
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
  UNITY_END();
}

void loop() {
}
