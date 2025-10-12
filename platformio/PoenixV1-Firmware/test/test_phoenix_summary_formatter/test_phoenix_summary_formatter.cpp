#include "channel_map/channel_map_formatter.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <cstring>
#include <unity.h>

static void test_summary_header_formats_aligned_columns(void) {
  char buffer[k_phoenix_benchmark_channel_map_summary_table_buffer_bytes];
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_format_summary_header(buffer, sizeof(buffer)));

  const size_t label_width   = k_phoenix_benchmark_channel_map_summary_label_width;
  const size_t samples_width = k_phoenix_benchmark_channel_map_summary_samples_width;
  const size_t channel_width = k_phoenix_benchmark_channel_map_summary_channel_width;
  const size_t map_width     = k_phoenix_benchmark_channel_map_summary_map_width;
  const size_t warning_width = k_phoenix_benchmark_channel_map_summary_warning_width;

  TEST_ASSERT_EQUAL_CHAR('S', buffer[0u]);

  const size_t samples_start = label_width + (samples_width - strlen("Samples"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[samples_start], "Samples", strlen("Samples")));

  const size_t mean_a_start = label_width + samples_width + (channel_width - strlen("Mean_A"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[mean_a_start], "Mean_A", strlen("Mean_A")));

  const size_t std_a_start = label_width + samples_width + channel_width + (channel_width - strlen("Std_A"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[std_a_start], "Std_A", strlen("Std_A")));

  const size_t min_a_start = label_width + samples_width + (2u * channel_width) + (channel_width - strlen("Min_A"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[min_a_start], "Min_A", strlen("Min_A")));

  const size_t max_a_start = label_width + samples_width + (3u * channel_width) + (channel_width - strlen("Max_A"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[max_a_start], "Max_A", strlen("Max_A")));

  const size_t mean_b_start = label_width + samples_width + (4u * channel_width) + (channel_width - strlen("Mean_B"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[mean_b_start], "Mean_B", strlen("Mean_B")));

  const size_t std_b_start = label_width + samples_width + (5u * channel_width) + (channel_width - strlen("Std_B"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[std_b_start], "Std_B", strlen("Std_B")));

  const size_t min_b_start = label_width + samples_width + (6u * channel_width) + (channel_width - strlen("Min_B"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[min_b_start], "Min_B", strlen("Min_B")));

  const size_t max_b_start = label_width + samples_width + (7u * channel_width) + (channel_width - strlen("Max_B"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[max_b_start], "Max_B", strlen("Max_B")));

  const size_t map_start = label_width + samples_width + (8u * channel_width) + (map_width - strlen("Channel_Map"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[map_start], "Channel_Map", strlen("Channel_Map")));

  const size_t warning_start =
      label_width + samples_width + (8u * channel_width) + map_width + (warning_width - strlen("Warnings"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[warning_start], "Warnings", strlen("Warnings")));

  const size_t buffer_length = strlen(buffer);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, buffer_length, "Header should not be empty");
  TEST_ASSERT_NOT_EQUAL(' ', buffer[buffer_length - 1u]);
}

static void test_summary_row_formats_state_metrics(void) {
  PhoenixBenchmarkChannelMapSummaryRowValues values = {
      .label               = "LED1",
      .sample_count        = 42u,
      .mean_channel_a      = 12345.678,
      .std_channel_a       = 12.345,
      .min_channel_a       = 12000.0,
      .max_channel_a       = 13000.0,
      .mean_channel_b      = -75.125,
      .std_channel_b       = 3.25,
      .min_channel_b       = -90.0,
      .max_channel_b       = -60.0,
      .channel_alignment   = "A=OK",
      .warning_label       = "SAT A=1",
      .has_channel_metrics = true,
  };

  char buffer[k_phoenix_benchmark_channel_map_summary_table_buffer_bytes];
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_format_summary_row(values, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_CHAR('L', buffer[0u]);

  const size_t label_width   = k_phoenix_benchmark_channel_map_summary_label_width;
  const size_t samples_width = k_phoenix_benchmark_channel_map_summary_samples_width;
  const size_t channel_width = k_phoenix_benchmark_channel_map_summary_channel_width;
  const size_t map_width     = k_phoenix_benchmark_channel_map_summary_map_width;

  const size_t samples_start = label_width + (samples_width - strlen("42"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[samples_start], "42", strlen("42")));

  const size_t mean_a_start = label_width + samples_width + (channel_width - strlen("12345.678"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[mean_a_start], "12345.678", strlen("12345.678")));

  const size_t std_a_start = label_width + samples_width + channel_width + (channel_width - strlen("12.345"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[std_a_start], "12.345", strlen("12.345")));

  const size_t min_a_start = label_width + samples_width + (2u * channel_width) + (channel_width - strlen("12000.000"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[min_a_start], "12000.000", strlen("12000.000")));

  const size_t max_a_start = label_width + samples_width + (3u * channel_width) + (channel_width - strlen("13000.000"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[max_a_start], "13000.000", strlen("13000.000")));

  const size_t mean_b_start = label_width + samples_width + (4u * channel_width) + (channel_width - strlen("-75.125"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[mean_b_start], "-75.125", strlen("-75.125")));

  const size_t std_b_start = label_width + samples_width + (5u * channel_width) + (channel_width - strlen("3.250"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[std_b_start], "3.250", strlen("3.250")));

  const size_t min_b_start = label_width + samples_width + (6u * channel_width) + (channel_width - strlen("-90.000"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[min_b_start], "-90.000", strlen("-90.000")));

  const size_t max_b_start = label_width + samples_width + (7u * channel_width) + (channel_width - strlen("-60.000"));
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[max_b_start], "-60.000", strlen("-60.000")));

  const size_t map_start = label_width + samples_width + (8u * channel_width);
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[map_start], "A=OK", strlen("A=OK")));

  const size_t warning_start = label_width + samples_width + (8u * channel_width) + map_width;
  TEST_ASSERT_EQUAL_INT(0, strncmp(&buffer[warning_start], "SAT A=1", strlen("SAT A=1")));

  const size_t buffer_length = strlen(buffer);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, buffer_length, "Row should not be empty");
  TEST_ASSERT_NOT_EQUAL(' ', buffer[buffer_length - 1u]);
}

static void test_summary_row_inserts_placeholders_without_channel_metrics(void) {
  PhoenixBenchmarkChannelMapSummaryRowValues values = {
      .label               = "Cycle",
      .sample_count        = 10u,
      .mean_channel_a      = 0.0,
      .std_channel_a       = 0.0,
      .min_channel_a       = 0.0,
      .max_channel_a       = 0.0,
      .mean_channel_b      = 0.0,
      .std_channel_b       = 0.0,
      .min_channel_b       = 0.0,
      .max_channel_b       = 0.0,
      .channel_alignment   = nullptr,
      .warning_label       = nullptr,
      .has_channel_metrics = false,
  };

  char buffer[k_phoenix_benchmark_channel_map_summary_table_buffer_bytes];
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_format_summary_row(values, buffer, sizeof(buffer)));

  const size_t label_width   = k_phoenix_benchmark_channel_map_summary_label_width;
  const size_t samples_width = k_phoenix_benchmark_channel_map_summary_samples_width;
  const size_t channel_width = k_phoenix_benchmark_channel_map_summary_channel_width;
  const size_t map_width     = k_phoenix_benchmark_channel_map_summary_map_width;

  const size_t first_placeholder = label_width + samples_width;
  TEST_ASSERT_EQUAL_CHAR('-', buffer[first_placeholder]);
  TEST_ASSERT_EQUAL_CHAR('-', buffer[first_placeholder + 1u]);

  const size_t second_placeholder = label_width + samples_width + channel_width;
  TEST_ASSERT_EQUAL_CHAR('-', buffer[second_placeholder]);
  TEST_ASSERT_EQUAL_CHAR('-', buffer[second_placeholder + 1u]);

  const size_t map_placeholder = label_width + samples_width + (8u * channel_width);
  TEST_ASSERT_EQUAL_CHAR('-', buffer[map_placeholder]);
  TEST_ASSERT_EQUAL_CHAR('-', buffer[map_placeholder + 1u]);

  const size_t warning_placeholder = label_width + samples_width + (8u * channel_width) + map_width;
  TEST_ASSERT_EQUAL_CHAR('-', buffer[warning_placeholder]);
  TEST_ASSERT_EQUAL_CHAR('-', buffer[warning_placeholder + 1u]);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  UNITY_BEGIN();
  RUN_TEST(test_summary_header_formats_aligned_columns);
  RUN_TEST(test_summary_row_formats_state_metrics);
  RUN_TEST(test_summary_row_inserts_placeholders_without_channel_metrics);
  UNITY_END();
}

void loop() {
}
