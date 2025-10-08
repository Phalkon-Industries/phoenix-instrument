#include "phoenix_benchmark_support.hpp"
#include "phoenix_summary_formatter.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <cstring>
#include <unity.h>

using phoenix_benchmark_support::format_summary_header;
using phoenix_benchmark_support::format_summary_row;
using phoenix_benchmark_support::RunningStats;
using phoenix_benchmark_support::StateAccumulator;
using phoenix_benchmark_support::SummaryRowValues;

static void test_running_stats_accumulates_integer_values(void) {
  RunningStats<int32_t> stats;
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
  RunningStats<uint32_t> stats;
  stats.update(123u);

  TEST_ASSERT_EQUAL_UINT32(1u, stats.count);
  TEST_ASSERT_EQUAL_UINT32(123u, stats.min_value);
  TEST_ASSERT_EQUAL_UINT32(123u, stats.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.0, stats.standard_deviation());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.0, stats.range());
}

static void test_state_accumulator_tracks_channel_metrics(void) {
  StateAccumulator accumulator;

  accumulator.channel_a_codes.update(100);
  accumulator.channel_a_codes.update(140);
  accumulator.channel_b_codes.update(-50);
  accumulator.channel_b_codes.update(-30);
  accumulator.state_duration_us.update(15u);
  accumulator.state_duration_us.update(17u);

  TEST_ASSERT_EQUAL_UINT32(2u, accumulator.channel_a_codes.count);
  TEST_ASSERT_EQUAL_UINT32(2u, accumulator.channel_b_codes.count);
  TEST_ASSERT_EQUAL_UINT32(2u, accumulator.state_duration_us.count);

  TEST_ASSERT_EQUAL_INT32(100, accumulator.channel_a_codes.min_value);
  TEST_ASSERT_EQUAL_INT32(140, accumulator.channel_a_codes.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 120.0, accumulator.channel_a_codes.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 28.2842712475, accumulator.channel_a_codes.standard_deviation());

  TEST_ASSERT_EQUAL_INT32(-50, accumulator.channel_b_codes.min_value);
  TEST_ASSERT_EQUAL_INT32(-30, accumulator.channel_b_codes.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, -40.0, accumulator.channel_b_codes.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 14.1421356237, accumulator.channel_b_codes.standard_deviation());

  TEST_ASSERT_EQUAL_UINT32(15u, accumulator.state_duration_us.min_value);
  TEST_ASSERT_EQUAL_UINT32(17u, accumulator.state_duration_us.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 16.0, accumulator.state_duration_us.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 1.41421356237, accumulator.state_duration_us.standard_deviation());
}

static void test_summary_header_formats_aligned_columns(void) {
  char buffer[phoenix_benchmark_support::k_summary_table_buffer_bytes];
  TEST_ASSERT_TRUE(format_summary_header(buffer, sizeof(buffer)));

  const size_t label_width    = phoenix_benchmark_support::k_summary_label_width;
  const size_t samples_width  = phoenix_benchmark_support::k_summary_samples_width;
  const size_t channel_width  = phoenix_benchmark_support::k_summary_channel_width;
  const size_t duration_width = phoenix_benchmark_support::k_summary_duration_width;

  TEST_ASSERT_EQUAL_CHAR('S', buffer[0u]);

  const size_t samples_start = label_width + (samples_width - strlen("Samples"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[samples_start], "Samples", strlen("Samples")));

  const size_t mean_a_start = label_width + samples_width + (channel_width - strlen("Mean_A"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[mean_a_start], "Mean_A", strlen("Mean_A")));

  const size_t std_a_start = label_width + samples_width + channel_width + (channel_width - strlen("Std_A"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[std_a_start], "Std_A", strlen("Std_A")));

  const size_t mean_b_start = label_width + samples_width + (2u * channel_width) + (channel_width - strlen("Mean_B"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[mean_b_start], "Mean_B", strlen("Mean_B")));

  const size_t std_b_start = label_width + samples_width + (3u * channel_width) + (channel_width - strlen("Std_B"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[std_b_start], "Std_B", strlen("Std_B")));

  const size_t step_mean_start =
      label_width + samples_width + (4u * channel_width) + (duration_width - strlen("Step_us_mean"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[step_mean_start], "Step_us_mean", strlen("Step_us_mean")));

  const size_t step_std_start =
      label_width + samples_width + (4u * channel_width) + duration_width + (duration_width - strlen("Step_us_std"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[step_std_start], "Step_us_std", strlen("Step_us_std")));

  const size_t step_range_start = label_width + samples_width + (4u * channel_width) + (2u * duration_width) +
                                  (duration_width - strlen("Step_us_range"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[step_range_start], "Step_us_range", strlen("Step_us_range")));

  const size_t buffer_length = strlen(buffer);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, buffer_length, "Header should not be empty");
  TEST_ASSERT_NOT_EQUAL(' ', buffer[buffer_length - 1u]);
}

static void test_summary_row_formats_state_metrics(void) {
  SummaryRowValues values = {
      .label               = "LED1",
      .sample_count        = 42u,
      .mean_channel_a      = 12345.678,
      .std_channel_a       = 12.345,
      .mean_channel_b      = -75.125,
      .std_channel_b       = 3.25,
      .step_mean_us        = 150.5,
      .step_std_us         = 2.5,
      .step_range_us       = 7.0,
      .has_channel_metrics = true,
  };

  char buffer[phoenix_benchmark_support::k_summary_table_buffer_bytes];
  TEST_ASSERT_TRUE(format_summary_row(values, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_CHAR('L', buffer[0u]);

  const size_t label_width   = phoenix_benchmark_support::k_summary_label_width;
  const size_t samples_width = phoenix_benchmark_support::k_summary_samples_width;
  const size_t channel_width = phoenix_benchmark_support::k_summary_channel_width;

  const size_t samples_start = label_width + (samples_width - strlen("42"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[samples_start], "42", strlen("42")));

  const size_t mean_a_start = label_width + samples_width + (channel_width - strlen("12345.678"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[mean_a_start], "12345.678", strlen("12345.678")));

  const size_t std_a_start = label_width + samples_width + channel_width + (channel_width - strlen("12.345"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[std_a_start], "12.345", strlen("12.345")));

  const size_t mean_b_start = label_width + samples_width + (2u * channel_width) + (channel_width - strlen("-75.125"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[mean_b_start], "-75.125", strlen("-75.125")));

  const size_t std_b_start = label_width + samples_width + (3u * channel_width) + (channel_width - strlen("3.250"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[std_b_start], "3.250", strlen("3.250")));

  const size_t buffer_length = strlen(buffer);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, buffer_length, "Row should not be empty");
  TEST_ASSERT_NOT_EQUAL(' ', buffer[buffer_length - 1u]);
}

static void test_summary_row_inserts_placeholders_without_channel_metrics(void) {
  SummaryRowValues values = {
      .label               = "Cycle",
      .sample_count        = 10u,
      .mean_channel_a      = 0.0,
      .std_channel_a       = 0.0,
      .mean_channel_b      = 0.0,
      .std_channel_b       = 0.0,
      .step_mean_us        = 1000.0,
      .step_std_us         = 25.0,
      .step_range_us       = 75.0,
      .has_channel_metrics = false,
  };

  char buffer[phoenix_benchmark_support::k_summary_table_buffer_bytes];
  TEST_ASSERT_TRUE(format_summary_row(values, buffer, sizeof(buffer)));

  const size_t label_width   = phoenix_benchmark_support::k_summary_label_width;
  const size_t samples_width = phoenix_benchmark_support::k_summary_samples_width;
  const size_t channel_width = phoenix_benchmark_support::k_summary_channel_width;

  const size_t first_placeholder = label_width + samples_width;
  TEST_ASSERT_EQUAL_CHAR('-', buffer[first_placeholder]);
  TEST_ASSERT_EQUAL_CHAR('-', buffer[first_placeholder + 1u]);

  const size_t second_placeholder = label_width + samples_width + channel_width;
  TEST_ASSERT_EQUAL_CHAR('-', buffer[second_placeholder]);
  TEST_ASSERT_EQUAL_CHAR('-', buffer[second_placeholder + 1u]);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  UNITY_BEGIN();
  RUN_TEST(test_running_stats_accumulates_integer_values);
  RUN_TEST(test_running_stats_handles_single_sample_std_zero);
  RUN_TEST(test_state_accumulator_tracks_channel_metrics);
  RUN_TEST(test_summary_header_formats_aligned_columns);
  RUN_TEST(test_summary_row_formats_state_metrics);
  RUN_TEST(test_summary_row_inserts_placeholders_without_channel_metrics);
  UNITY_END();
}

void loop() {
}
