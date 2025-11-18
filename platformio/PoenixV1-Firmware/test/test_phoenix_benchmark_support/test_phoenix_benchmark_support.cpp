#include "adc_speed/adc_speed.hpp"
#include "adc_speed/adc_speed_command_parser.hpp"
#include "adc_speed/adc_speed_formatter.hpp"
#include "channel_map/channel_map_formatter.hpp"
#include "channel_map/channel_map_support.hpp"
#include "cold_sweep/cold_sweep.hpp"
#include "cold_sweep/cold_sweep_formatter.hpp"
#include "core/phoenix_benchmark_core.hpp"
#include "drift_capture/drift_capture.hpp"
#include "dwell_sweep/dwell_sweep.hpp"
#include "light_readings.hpp"
#include "main.hpp"
#include "osr_latency/osr_latency.hpp"
#include "osr_latency/osr_latency_command_parser.hpp"
#include "osr_latency/osr_latency_formatter.hpp"
#include "osr_sweep/osr_sweep.hpp"
#include "osr_sweep/osr_sweep_formatter.hpp"
#include "pot_sweep/pot_sweep.hpp"
#include "pot_sweep/pot_sweep_formatter.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <cstring>
#include <unity.h>

extern void run_mcp356x_latency_lookup_tests(void);

static void test_running_stats_accumulates_integer_values(void) {
  // Step 1: Create an empty integer stats tracker and verify it starts without samples.
  PhoenixBenchmarkRunningStats<int32_t> stats;
  TEST_ASSERT_FALSE_MESSAGE(stats.has_samples(), "Stats should start empty");

  // Step 2: Feed three values representing a simple ramp.
  stats.update(10);
  stats.update(20);
  stats.update(30);

  // Step 3: Confirm the statistics reflect the submitted data set.
  TEST_ASSERT_TRUE(stats.has_samples());
  TEST_ASSERT_EQUAL_UINT32(3u, stats.count);
  TEST_ASSERT_EQUAL_INT32(10, stats.min_value);
  TEST_ASSERT_EQUAL_INT32(30, stats.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 20.0, stats.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 10.0, stats.standard_deviation());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 20.0, stats.range());
}

static void test_running_stats_handles_single_sample_std_zero(void) {
  // Step 1: Initialise stats and push a single sample to exercise zero-variance handling.
  PhoenixBenchmarkRunningStats<uint32_t> stats;
  stats.update(123u);

  // Step 2: Validate the counters and variance-related helpers collapse to zero.
  TEST_ASSERT_EQUAL_UINT32(1u, stats.count);
  TEST_ASSERT_EQUAL_UINT32(123u, stats.min_value);
  TEST_ASSERT_EQUAL_UINT32(123u, stats.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.0, stats.standard_deviation());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.0, stats.range());
}

static void test_state_accumulator_tracks_channel_metrics(void) {
  // Step 1: Create an accumulator and seed it with channel A and B samples.
  PhoenixBenchmarkStateAccumulator accumulator;

  accumulator.channel_a_codes.update(100);
  accumulator.channel_a_codes.update(140);
  accumulator.channel_b_codes.update(-50);
  accumulator.channel_b_codes.update(-30);

  // Step 2: Confirm both channels observed the expected number of samples.
  TEST_ASSERT_EQUAL_UINT32(2u, accumulator.channel_a_codes.count);
  TEST_ASSERT_EQUAL_UINT32(2u, accumulator.channel_b_codes.count);

  // Step 3: Validate channel A statistics including standard deviation.
  TEST_ASSERT_EQUAL_INT32(100, accumulator.channel_a_codes.min_value);
  TEST_ASSERT_EQUAL_INT32(140, accumulator.channel_a_codes.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 120.0, accumulator.channel_a_codes.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 28.2842712475, accumulator.channel_a_codes.standard_deviation());

  // Step 4: Validate channel B statistics including standard deviation.
  TEST_ASSERT_EQUAL_INT32(-50, accumulator.channel_b_codes.min_value);
  TEST_ASSERT_EQUAL_INT32(-30, accumulator.channel_b_codes.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, -40.0, accumulator.channel_b_codes.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 14.1421356237, accumulator.channel_b_codes.standard_deviation());

  // Step 5: Ensure no saturation events were recorded for either channel.
  TEST_ASSERT_EQUAL_UINT32(0u, accumulator.channel_a_saturation_count);
  TEST_ASSERT_EQUAL_UINT32(0u, accumulator.channel_b_saturation_count);
}

static void test_determine_dominant_channel_prefers_channel_a_when_delta_exceeds_drain(void) {
  // Step 1: Assemble a drain baseline with nearly equal amplitudes across channels.
  PhoenixBenchmarkStateAccumulator drain_accumulator;
  drain_accumulator.channel_a_codes.update(100);
  drain_accumulator.channel_a_codes.update(102);
  drain_accumulator.channel_b_codes.update(98);
  drain_accumulator.channel_b_codes.update(99);

  // Step 2: Populate a blue accumulator where channel A clearly dominates.
  PhoenixBenchmarkStateAccumulator blue_accumulator;
  blue_accumulator.channel_a_codes.update(500);
  blue_accumulator.channel_a_codes.update(520);
  blue_accumulator.channel_b_codes.update(110);
  blue_accumulator.channel_b_codes.update(120);

  // Step 3: Verify the helper selects channel A when the delta exceeds the drain threshold.
  const PhoenixBenchmarkChannel channel =
      phoenix_benchmark_channel_map_determine_dominant_channel(drain_accumulator, blue_accumulator, 5.0);
  TEST_ASSERT_EQUAL(PhoenixBenchmarkChannel::kChannelA, channel);
}

static void test_determine_dominant_channel_returns_unknown_when_deltas_similar(void) {
  // Step 1: Populate the drain and LED accumulators with similar channel deltas.
  PhoenixBenchmarkStateAccumulator drain_accumulator;
  drain_accumulator.channel_a_codes.update(200);
  drain_accumulator.channel_a_codes.update(205);
  drain_accumulator.channel_b_codes.update(195);
  drain_accumulator.channel_b_codes.update(198);

  PhoenixBenchmarkStateAccumulator green_accumulator;
  green_accumulator.channel_a_codes.update(203);
  green_accumulator.channel_a_codes.update(206);
  green_accumulator.channel_b_codes.update(197);
  green_accumulator.channel_b_codes.update(199);

  // Step 2: Expect the helper to classify the result as unknown because the deltas align.
  const PhoenixBenchmarkChannel channel =
      phoenix_benchmark_channel_map_determine_dominant_channel(drain_accumulator, green_accumulator, 5.0);
  TEST_ASSERT_EQUAL(PhoenixBenchmarkChannel::kUnknown, channel);
}

static void test_format_channel_alignment_label_formats_mismatch_note(void) {
  // Step 1: Format a mismatch between expected and observed channels.
  char buffer[k_phoenix_benchmark_channel_map_summary_map_width + 1u];
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_format_alignment_label(
      PhoenixBenchmarkChannel::kChannelA, PhoenixBenchmarkChannel::kChannelB, buffer, sizeof(buffer)));
  // Step 2: Confirm the formatter emits the shorthand note.
  TEST_ASSERT_EQUAL_STRING("B!=A", buffer);
}

static void test_parse_channel_map_command_extracts_parameters(void) {
  // Step 1: Provide a JSON command that only specifies the sweep count override.
  const char*                       command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":25}}";
  PhoenixBenchmarkChannelMapRequest request      = {};
  // Step 2: Parse the command and validate the sweep override while confirming optional fields stay unset.
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
  TEST_ASSERT_EQUAL_UINT32(25u, request.sweep_count);
  TEST_ASSERT_FALSE(request.has_dwell_override);
  TEST_ASSERT_FALSE(request.has_wiper_override);
}

static void test_parse_channel_map_command_treats_missing_dwell_as_default(void) {
  // Step 1: Supply a minimal command with only the sweep count specified.
  const char*                       command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":3}}";
  PhoenixBenchmarkChannelMapRequest request      = {};
  // Step 2: Confirm the parser applies defaults to optional fields.
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
  TEST_ASSERT_EQUAL_UINT32(3u, request.sweep_count);
  TEST_ASSERT_FALSE(request.has_dwell_override);
  TEST_ASSERT_FALSE(request.has_wiper_override);
}

static void test_parse_channel_map_command_rejects_wiper_override(void) {
  // Step 1: Attempt to parse a command that now includes a disallowed wiper override.
  const char* command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":5,\"wiper_code\":42}}";
  PhoenixBenchmarkChannelMapRequest request = {};
  // Step 2: Expect the helper to reject the payload because wiper overrides are no longer supported.
  TEST_ASSERT_FALSE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
}

static void test_parse_channel_map_command_rejects_invalid_payload(void) {
  // Step 1: Provide a payload that violates documented sweep limits.
  const char*                       command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":0}}";
  PhoenixBenchmarkChannelMapRequest request      = {};
  // Step 2: Confirm the parser reports failure when validation fails.
  TEST_ASSERT_FALSE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
}

static void test_parse_channel_map_command_rejects_dwell_override(void) {
  // Step 1: Attempt to parse a command that tries to override dwell time.
  const char* command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":4,\"dwell_us\":50}}";
  PhoenixBenchmarkChannelMapRequest request = {};
  // Step 2: Confirm the parser rejects unsupported dwell overrides.
  TEST_ASSERT_FALSE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
}

static uint32_t g_cold_sweep_fake_timestamp_us      = 0u;
static uint32_t g_cold_sweep_runner_calls           = 0u;
static uint32_t g_cold_sweep_stats_calls            = 0u;
static uint32_t g_cold_sweep_saturation_checks      = 0u;
static bool     g_cold_sweep_force_saturation_check = false;
static bool     g_cold_sweep_hardware_ready         = true;

static bool cold_sweep_fake_hardware_ready(void);

static void cold_sweep_reset_fakes(void) {
  g_cold_sweep_fake_timestamp_us      = 123456u;
  g_cold_sweep_runner_calls           = 0u;
  g_cold_sweep_stats_calls            = 0u;
  g_cold_sweep_saturation_checks      = 0u;
  g_cold_sweep_force_saturation_check = false;
  g_cold_sweep_hardware_ready         = true;
  phoenix_benchmark_cold_sweep_clear_test_hooks();
  phoenix_benchmark_cold_sweep_set_hardware_ready_checker_for_test(cold_sweep_fake_hardware_ready);
}

static bool cold_sweep_fake_hardware_ready(void) {
  return g_cold_sweep_hardware_ready;
}

static int cold_sweep_fake_light_readings_runner(uint32_t sweep_count, LightReadingsSweepCollection* collection) {
  // Step 1: Track invocation counts and populate deterministic sweep entries.
  ++g_cold_sweep_runner_calls;
  TEST_ASSERT_NOT_NULL(collection);
  TEST_ASSERT_NOT_NULL(collection->sweeps);

  collection->sweep_count = sweep_count;
  for (uint32_t index = 0u; index < sweep_count; ++index) {
    LightReadingsSweepSample& sample = collection->sweeps[index];
    sample.drain_blue_code           = static_cast<int32_t>(1000 + static_cast<int32_t>(index));
    sample.drain_green_code          = static_cast<int32_t>(2000 + static_cast<int32_t>(index));
    sample.blue_code                 = static_cast<int32_t>(3000 + static_cast<int32_t>(index));
    sample.green_code                = static_cast<int32_t>(4000 + static_cast<int32_t>(index));
  }

  return LIGHT_READINGS_OK;
}

static int cold_sweep_fake_stats_calculator(const LightReadingsSweepCollection* collection,
                                            LightReadingsSweepStats*            stats_out) {
  // Step 1: Record the calculation request and fabricate per-channel summaries.
  ++g_cold_sweep_stats_calls;
  TEST_ASSERT_NOT_NULL(collection);
  TEST_ASSERT_NOT_NULL(stats_out);

  *stats_out             = {};
  stats_out->sweep_count = collection->sweep_count;

  stats_out->drain_blue.sample_count       = collection->sweep_count;
  stats_out->drain_blue.mean               = 1100.0;
  stats_out->drain_blue.standard_deviation = 10.0;
  stats_out->drain_blue.min_value          = 1000;
  stats_out->drain_blue.max_value          = 1200;
  stats_out->drain_blue.has_samples        = true;

  stats_out->drain_green.sample_count       = collection->sweep_count;
  stats_out->drain_green.mean               = 2100.0;
  stats_out->drain_green.standard_deviation = 20.0;
  stats_out->drain_green.min_value          = 2000;
  stats_out->drain_green.max_value          = 2200;
  stats_out->drain_green.has_samples        = true;

  stats_out->blue.sample_count       = collection->sweep_count;
  stats_out->blue.mean               = 3100.0;
  stats_out->blue.standard_deviation = 30.0;
  stats_out->blue.min_value          = 3000;
  stats_out->blue.max_value          = 3200;
  stats_out->blue.has_samples        = true;

  stats_out->green.sample_count       = collection->sweep_count;
  stats_out->green.mean               = 4100.0;
  stats_out->green.standard_deviation = 40.0;
  stats_out->green.min_value          = 4000;
  stats_out->green.max_value          = 4200;
  stats_out->green.has_samples        = true;

  return LIGHT_READINGS_OK;
}

static bool cold_sweep_fake_saturation_checker(void) {
  // Step 1: Track how many times saturation metadata was queried and return the forced flag.
  ++g_cold_sweep_saturation_checks;
  return g_cold_sweep_force_saturation_check;
}

static uint32_t cold_sweep_fake_timestamp_provider(void) {
  // Step 1: Surface a deterministic timestamp so the runner can attribute sweeps.
  return g_cold_sweep_fake_timestamp_us;
}

static void test_cold_sweep_run_populates_samples_and_statistics(void) {
  // Step 1: Configure the cold sweep hooks to return deterministic sweep data and summaries.
  cold_sweep_reset_fakes();
  phoenix_benchmark_cold_sweep_set_light_readings_runner_for_test(cold_sweep_fake_light_readings_runner);
  phoenix_benchmark_cold_sweep_set_stats_calculator_for_test(cold_sweep_fake_stats_calculator);
  phoenix_benchmark_cold_sweep_set_saturation_checker_for_test(cold_sweep_fake_saturation_checker);
  phoenix_benchmark_cold_sweep_set_timestamp_provider_for_test(cold_sweep_fake_timestamp_provider);

  static LightReadingsSweepSample sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT];
  LightReadingsSweepCollection    sweep_collection = {
         .sweep_count = 0u,
         .sweeps      = sweep_storage,
  };
  LightReadingsSweepStats stats = {};

  const PhoenixBenchmarkColdSweepOptions options = {
      .sweep_count        = 4u,
      .has_sweep_override = true,
      .dwell_override_us  = 0u,
      .has_dwell_override = false,
  };

  // Step 2: Execute the cold sweep and capture the resulting status payload.
  const PhoenixBenchmarkColdSweepExecutionStatus status =
      phoenix_benchmark_cold_sweep_run(options, &sweep_collection, &stats);

  phoenix_benchmark_cold_sweep_clear_test_hooks();

  // Step 3: Validate that sweep metrics, statistics, and timestamps surfaced as expected.
  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_FALSE(status.has_warnings);
  TEST_ASSERT_EQUAL_UINT32(options.sweep_count, status.captured_sweeps);
  TEST_ASSERT_EQUAL_UINT32(g_cold_sweep_fake_timestamp_us, status.timestamp_us);
  TEST_ASSERT_EQUAL_UINT32(options.sweep_count, sweep_collection.sweep_count);
  TEST_ASSERT_EQUAL_INT32(1000, sweep_collection.sweeps[0].drain_blue_code);
  TEST_ASSERT_EQUAL_INT32(4000, sweep_collection.sweeps[0].green_code);
  TEST_ASSERT_EQUAL_INT32(1003, sweep_collection.sweeps[3].drain_blue_code);
  TEST_ASSERT_EQUAL_INT32(4003, sweep_collection.sweeps[3].green_code);

  TEST_ASSERT_EQUAL_UINT32(options.sweep_count, stats.sweep_count);
  TEST_ASSERT_TRUE(stats.blue.has_samples);
  TEST_ASSERT_TRUE(stats.green.has_samples);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3100.0f, static_cast<float>(stats.blue.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4100.0f, static_cast<float>(stats.green.mean));
  TEST_ASSERT_EQUAL_INT32(3000, stats.blue.min_value);
  TEST_ASSERT_EQUAL_INT32(4200, stats.green.max_value);

  TEST_ASSERT_EQUAL_UINT32(1u, g_cold_sweep_runner_calls);
  TEST_ASSERT_EQUAL_UINT32(1u, g_cold_sweep_stats_calls);
  TEST_ASSERT_NOT_EQUAL(0u, g_cold_sweep_saturation_checks);
}

static void test_cold_sweep_run_reports_saturation_warning(void) {
  // Step 1: Enable the saturation flag so the warning plumbing can be exercised.
  cold_sweep_reset_fakes();
  g_cold_sweep_force_saturation_check = true;
  phoenix_benchmark_cold_sweep_set_light_readings_runner_for_test(cold_sweep_fake_light_readings_runner);
  phoenix_benchmark_cold_sweep_set_stats_calculator_for_test(cold_sweep_fake_stats_calculator);
  phoenix_benchmark_cold_sweep_set_saturation_checker_for_test(cold_sweep_fake_saturation_checker);
  phoenix_benchmark_cold_sweep_set_timestamp_provider_for_test(cold_sweep_fake_timestamp_provider);

  static LightReadingsSweepSample sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT];
  LightReadingsSweepCollection    sweep_collection = {
         .sweep_count = 0u,
         .sweeps      = sweep_storage,
  };
  LightReadingsSweepStats stats = {};

  const PhoenixBenchmarkColdSweepOptions options = {
      .sweep_count        = 2u,
      .has_sweep_override = true,
      .dwell_override_us  = 0u,
      .has_dwell_override = false,
  };

  // Step 2: Execute the cold sweep and confirm the saturation warning surfaces in the status payload.
  const PhoenixBenchmarkColdSweepExecutionStatus status =
      phoenix_benchmark_cold_sweep_run(options, &sweep_collection, &stats);

  phoenix_benchmark_cold_sweep_clear_test_hooks();

  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_TRUE(status.has_warnings);
  TEST_ASSERT_NOT_EQUAL(0u, status.warning_mask & k_phoenix_benchmark_cold_sweep_warning_saturation);
  TEST_ASSERT_EQUAL_UINT32(options.sweep_count, status.captured_sweeps);
  TEST_ASSERT_EQUAL_UINT32(options.sweep_count, sweep_collection.sweep_count);
}

static void test_cold_sweep_run_reports_error_when_hardware_not_ready(void) {
  // Step 1: Make the hardware readiness probe fail so the runner aborts.
  cold_sweep_reset_fakes();
  g_cold_sweep_hardware_ready = false;
  phoenix_benchmark_cold_sweep_set_hardware_ready_checker_for_test(cold_sweep_fake_hardware_ready);

  static LightReadingsSweepSample sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT] = {};
  LightReadingsSweepCollection    sweep_collection                              = {
                                      .sweep_count = 0u,
                                      .sweeps      = sweep_storage,
  };
  LightReadingsSweepStats stats = {};

  const PhoenixBenchmarkColdSweepOptions options = {
      .sweep_count        = 0u,
      .has_sweep_override = false,
      .dwell_override_us  = 0u,
      .has_dwell_override = false,
  };

  // Step 2: Expect the run to fail with a hardware initialisation error.
  const PhoenixBenchmarkColdSweepExecutionStatus status =
      phoenix_benchmark_cold_sweep_run(options, &sweep_collection, &stats);

  TEST_ASSERT_FALSE(status.success);
  TEST_ASSERT_NOT_NULL(status.message);
  TEST_ASSERT_EQUAL_STRING("hardware_initialisation_failed", status.message);
  TEST_ASSERT_EQUAL_UINT32(0u, sweep_collection.sweep_count);
  TEST_ASSERT_EQUAL_UINT32(0u, status.captured_sweeps);

  phoenix_benchmark_cold_sweep_clear_test_hooks();
}

static void test_cold_sweep_parse_command_accepts_plain_token(void) {
  // Step 1: Parse the bare command token and confirm it succeeds without overrides.
  const PhoenixBenchmarkColdSweepParseResult result = phoenix_benchmark_cold_sweep_parse_command("cold_sweep");

  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_FALSE(result.options.has_sweep_override);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT32(0u, result.options.sweep_count);
  TEST_ASSERT_EQUAL_UINT32(0u, result.options.dwell_override_us);
}

static void test_cold_sweep_parse_command_accepts_minimal_json(void) {
  // Step 1: Provide the JSON envelope used by other benchmarks and expect success.
  const PhoenixBenchmarkColdSweepParseResult result =
      phoenix_benchmark_cold_sweep_parse_command("{\"command\":\"cold_sweep\"}");

  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_FALSE(result.options.has_sweep_override);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
}

static void test_cold_sweep_parse_command_rejects_parameters(void) {
  // Step 1: Ensure unknown parameter payloads are rejected to highlight protocol drift.
  const PhoenixBenchmarkColdSweepParseResult result =
      phoenix_benchmark_cold_sweep_parse_command("{\"command\":\"cold_sweep\",\"parameters\":{\"sweeps\":10}}");

  TEST_ASSERT_FALSE(result.success);
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_cold_sweep_format_summary_header_matches_expected_columns(void) {
  // Step 1: Render the summary header and confirm it surfaces the agreed columns.
  char buffer[k_phoenix_benchmark_cold_sweep_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_cold_sweep_format_summary_header(buffer, sizeof(buffer)));

  TEST_ASSERT_EQUAL_STRING("Channel        Samples  Mean      StdDev     Min        Max        Saturated", buffer);
}

static void test_cold_sweep_format_summary_row_formats_metrics(void) {
  // Step 1: Populate a summary row with complete metrics so formatting can be validated.
  PhoenixBenchmarkColdSweepSummaryRowValues values = {
      .label              = "drain_blue",
      .sample_count       = 5u,
      .mean               = 123.456,
      .standard_deviation = 7.89,
      .min_code           = -42,
      .max_code           = 2048,
      .has_samples        = true,
      .saturated          = true,
  };

  char buffer[k_phoenix_benchmark_cold_sweep_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_cold_sweep_format_summary_row(values, buffer, sizeof(buffer)));

  TEST_ASSERT_NOT_NULL(strstr(buffer, "drain_blue"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "123.456"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "7.890"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "-42"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "2048"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "yes"));
}

static void test_cold_sweep_format_summary_row_uses_placeholders_when_samples_missing(void) {
  // Step 1: Prepare a summary row without metrics so placeholder handling can be asserted.
  PhoenixBenchmarkColdSweepSummaryRowValues values = {
      .label              = "green",
      .sample_count       = 0u,
      .mean               = 0.0,
      .standard_deviation = 0.0,
      .min_code           = 0,
      .max_code           = 0,
      .has_samples        = false,
      .saturated          = false,
  };

  char buffer[k_phoenix_benchmark_cold_sweep_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_cold_sweep_format_summary_row(values, buffer, sizeof(buffer)));

  TEST_ASSERT_NOT_NULL(strstr(buffer, "green"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "--"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "no"));
}

static void test_cold_sweep_format_sample_header_matches_expected_columns(void) {
  // Step 1: Render the sample header to confirm column labels align with host expectations.
  char buffer[k_phoenix_benchmark_cold_sweep_sample_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_cold_sweep_format_sample_header(buffer, sizeof(buffer)));

  TEST_ASSERT_EQUAL_STRING("Index  Drain_Blue  Drain_Green  Blue  Green  Saturation", buffer);
}

static void test_cold_sweep_format_sample_row_formats_codes_and_mask(void) {
  // Step 1: Populate a sample row and ensure formatted output includes each field and saturation token.
  PhoenixBenchmarkColdSweepSampleRowValues values = {
      .sweep_index      = 3u,
      .drain_blue_code  = -500,
      .drain_green_code = 1024,
      .blue_code        = 2048,
      .green_code       = -1024,
      .saturation_mask  = static_cast<uint8_t>(k_phoenix_benchmark_cold_sweep_saturation_drain_blue |
                                               k_phoenix_benchmark_cold_sweep_saturation_green),
  };

  char buffer[k_phoenix_benchmark_cold_sweep_sample_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_cold_sweep_format_sample_row(values, buffer, sizeof(buffer)));

  TEST_ASSERT_NOT_NULL(strstr(buffer, "3"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "-500"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "1024"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "2048"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "-1024"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "db|green"));
}

static uint32_t                        g_drift_capture_fake_micros             = 0u;
static const int32_t*                  g_drift_capture_blue_codes              = nullptr;
static std::size_t                     g_drift_capture_blue_length             = 0u;
static std::size_t                     g_drift_capture_blue_index              = 0u;
static const int32_t*                  g_drift_capture_green_codes             = nullptr;
static std::size_t                     g_drift_capture_green_length            = 0u;
static std::size_t                     g_drift_capture_green_index             = 0u;
static PhoenixBenchmarkDriftCaptureLed g_drift_capture_current_led             = PhoenixBenchmarkDriftCaptureLed::kBlue;
static bool                            g_drift_capture_led_active              = false;
static LedRouterState                  g_drift_capture_router_transitions[8]   = {};
static std::size_t                     g_drift_capture_router_transition_count = 0u;
static uint8_t                         g_drift_capture_last_blue_wiper_code    = 0u;
static uint8_t                         g_drift_capture_last_green_wiper_code   = 0u;
static uint32_t                        g_drift_capture_last_osr_enum           = 0u;
static std::size_t                     g_drift_capture_osr_call_count          = 0u;
static AdcHalChannel                   g_drift_capture_last_channel            = AdcHalChannel::ADC_HAL_CHANNEL_4;
static char                            g_drift_capture_output_lines[32][128]   = {};
static std::size_t                     g_drift_capture_output_count            = 0u;

static void drift_capture_reset_fakes(void) {
  g_drift_capture_fake_micros             = 0u;
  g_drift_capture_blue_codes              = nullptr;
  g_drift_capture_blue_length             = 0u;
  g_drift_capture_blue_index              = 0u;
  g_drift_capture_green_codes             = nullptr;
  g_drift_capture_green_length            = 0u;
  g_drift_capture_green_index             = 0u;
  g_drift_capture_current_led             = PhoenixBenchmarkDriftCaptureLed::kBlue;
  g_drift_capture_led_active              = false;
  g_drift_capture_router_transition_count = 0u;
  g_drift_capture_last_blue_wiper_code    = 0u;
  g_drift_capture_last_green_wiper_code   = 0u;
  g_drift_capture_last_osr_enum           = 0u;
  g_drift_capture_osr_call_count          = 0u;
  g_drift_capture_last_channel            = AdcHalChannel::ADC_HAL_CHANNEL_4;
  g_drift_capture_output_count            = 0u;
  for (std::size_t index = 0u;
       index < (sizeof(g_drift_capture_router_transitions) / sizeof(g_drift_capture_router_transitions[0])); ++index) {
    g_drift_capture_router_transitions[index] = LedRouterState::LED_ROUTER_STATE_OFF;
  }
  for (std::size_t line = 0u; line < (sizeof(g_drift_capture_output_lines) / sizeof(g_drift_capture_output_lines[0]));
       ++line) {
    g_drift_capture_output_lines[line][0] = '\0';
  }
}

static bool drift_capture_fake_hardware_ready(void) {
  return true;
}

static bool drift_capture_fake_wiper_setter(uint8_t blue_code, uint8_t green_code) {
  g_drift_capture_last_blue_wiper_code  = blue_code;
  g_drift_capture_last_green_wiper_code = green_code;
  return true;
}

static int drift_capture_fake_led_setter(LedRouterState state) {
  if (g_drift_capture_router_transition_count <
      (sizeof(g_drift_capture_router_transitions) / sizeof(g_drift_capture_router_transitions[0]))) {
    g_drift_capture_router_transitions[g_drift_capture_router_transition_count++] = state;
  }
  if (state == LedRouterState::LED_ROUTER_STATE_BLUE) {
    g_drift_capture_current_led = PhoenixBenchmarkDriftCaptureLed::kBlue;
    g_drift_capture_led_active  = true;
  }
  else if (state == LedRouterState::LED_ROUTER_STATE_GREEN) {
    g_drift_capture_current_led = PhoenixBenchmarkDriftCaptureLed::kGreen;
    g_drift_capture_led_active  = true;
  }
  else {
    g_drift_capture_led_active = false;
  }
  return LED_ROUTER_OK;
}

static int drift_capture_fake_osr_setter(mcp356x_osr value) {
  g_drift_capture_last_osr_enum = static_cast<uint32_t>(value);
  ++g_drift_capture_osr_call_count;
  return 0;
}

static bool drift_capture_fake_adc_reader(AdcHalChannel channel, int32_t* out_code) {
  g_drift_capture_last_channel = channel;
  if ((out_code == nullptr) || !g_drift_capture_led_active) {
    return false;
  }
  if (g_drift_capture_current_led == PhoenixBenchmarkDriftCaptureLed::kBlue) {
    if (g_drift_capture_blue_index >= g_drift_capture_blue_length) {
      return false;
    }
    *out_code = g_drift_capture_blue_codes[g_drift_capture_blue_index++];
  }
  else {
    if (g_drift_capture_green_index >= g_drift_capture_green_length) {
      return false;
    }
    *out_code = g_drift_capture_green_codes[g_drift_capture_green_index++];
  }
  g_drift_capture_fake_micros += 3u;
  return true;
}

static uint32_t drift_capture_fake_micros(void) {
  return g_drift_capture_fake_micros;
}

static void drift_capture_fake_delay(uint32_t delay_us) {
  g_drift_capture_fake_micros += delay_us;
}

static void drift_capture_collect_line(const char* line) {
  if ((line == nullptr) || (g_drift_capture_output_count >=
                            (sizeof(g_drift_capture_output_lines) / sizeof(g_drift_capture_output_lines[0])))) {
    return;
  }
  std::strncpy(g_drift_capture_output_lines[g_drift_capture_output_count], line,
               sizeof(g_drift_capture_output_lines[g_drift_capture_output_count]) - 1u);
  g_drift_capture_output_lines[g_drift_capture_output_count]
                              [sizeof(g_drift_capture_output_lines[g_drift_capture_output_count]) - 1u] = '\0';
  ++g_drift_capture_output_count;
}

static void drift_capture_install_fakes(void) {
  phoenix_benchmark_drift_capture_set_hardware_ready_checker_for_test(drift_capture_fake_hardware_ready);
  phoenix_benchmark_drift_capture_set_wiper_setter_for_test(drift_capture_fake_wiper_setter);
  phoenix_benchmark_drift_capture_set_led_setter_for_test(drift_capture_fake_led_setter);
  phoenix_benchmark_drift_capture_set_osr_setter_for_test(drift_capture_fake_osr_setter);
  phoenix_benchmark_drift_capture_set_adc_reader_for_test(drift_capture_fake_adc_reader);
  phoenix_benchmark_drift_capture_set_micros_provider_for_test(drift_capture_fake_micros);
  phoenix_benchmark_drift_capture_set_delay_provider_for_test(drift_capture_fake_delay);
}

static void drift_capture_uninstall_fakes(void) {
  phoenix_benchmark_drift_capture_clear_test_hooks();
}

static void test_drift_capture_options_apply_defaults_inherit_initialised_values(void) {
  // Step 1: Supply defaults and ensure options inherit fields when overrides are absent.
  const PhoenixBenchmarkDriftCaptureDefaults defaults = {
      .start_time_us    = 0u,
      .end_time_us      = 100000u,
      .step_delay_us    = 10u,
      .osr              = 4096u,
      .blue_wiper_code  = 0x22u,
      .green_wiper_code = 0x33u,
  };

  PhoenixBenchmarkDriftCaptureOptions options = {};
  options.has_start_override                  = false;
  options.has_end_override                    = false;
  options.has_step_override                   = false;
  options.has_osr_override                    = false;
  options.has_wiper_override                  = false;
  options.apply_defaults(defaults);

  TEST_ASSERT_EQUAL_UINT32(defaults.start_time_us, options.start_time_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.end_time_us, options.end_time_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.step_delay_us, options.step_delay_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.osr, options.osr);
  TEST_ASSERT_EQUAL_UINT8(defaults.blue_wiper_code, options.blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(defaults.green_wiper_code, options.green_wiper_code);
}

static void test_drift_capture_options_validate_rejects_inverted_range(void) {
  // Step 1: Configure an option set with an inverted window.
  PhoenixBenchmarkDriftCaptureOptions options = {
      .start_time_us      = 200u,
      .has_start_override = true,
      .end_time_us        = 100u,
      .has_end_override   = true,
      .step_delay_us      = 10u,
      .has_step_override  = true,
      .osr                = 4096u,
      .has_osr_override   = true,
      .blue_wiper_code    = 0x10u,
      .green_wiper_code   = 0x10u,
      .has_wiper_override = true,
  };

  const char* error_message = nullptr;
  TEST_ASSERT_FALSE(options.validate(&error_message));
  TEST_ASSERT_NOT_NULL(error_message);
}

static void test_drift_capture_options_validate_rejects_schedule_exceeding_buffer(void) {
  // Step 1: Select a step that would exceed the static buffer capacity.
  PhoenixBenchmarkDriftCaptureOptions options = {
      .start_time_us      = 0u,
      .has_start_override = true,
      .end_time_us        = static_cast<uint32_t>((k_phoenix_benchmark_drift_capture_max_sample_count + 1u) * 10u),
      .has_end_override   = true,
      .step_delay_us      = 10u,
      .has_step_override  = true,
      .osr                = 4096u,
      .has_osr_override   = true,
      .blue_wiper_code    = 0x01u,
      .green_wiper_code   = 0x01u,
      .has_wiper_override = true,
  };

  const char* error_message = nullptr;
  TEST_ASSERT_FALSE(options.validate(&error_message));
  TEST_ASSERT_NOT_NULL(error_message);
}

static void test_drift_capture_parse_command_accepts_plain_token(void) {
  // Step 1: Ensure the bare command inherits initialised defaults.
  phoenix_benchmark_drift_capture_reset_state();
  const PhoenixBenchmarkDriftCaptureDefaults defaults = {
      .start_time_us    = 0u,
      .end_time_us      = 100000u,
      .step_delay_us    = 0u,
      .osr              = 4096u,
      .blue_wiper_code  = 0xAAu,
      .green_wiper_code = 0xBBu,
  };
  phoenix_benchmark_drift_capture_initialise(defaults);

  const PhoenixBenchmarkDriftCaptureParseResult result = phoenix_benchmark_drift_capture_parse_command("drift_capture");

  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_FALSE(result.options.has_start_override);
  TEST_ASSERT_FALSE(result.options.has_end_override);
  TEST_ASSERT_FALSE(result.options.has_step_override);
  TEST_ASSERT_FALSE(result.options.has_osr_override);
  TEST_ASSERT_FALSE(result.options.has_wiper_override);
  TEST_ASSERT_EQUAL_UINT32(defaults.start_time_us, result.options.start_time_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.end_time_us, result.options.end_time_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.step_delay_us, result.options.step_delay_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.osr, result.options.osr);
  TEST_ASSERT_EQUAL_UINT8(defaults.blue_wiper_code, result.options.blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(defaults.green_wiper_code, result.options.green_wiper_code);
}

static void test_drift_capture_defaults_use_light_config(void) {
  // Step 1: Build a light readings configuration with distinct wiper codes per colour.
  const LightReadingsConfig light_config = {
    .drain_state    = LedRouterState::LED_ROUTER_STATE_DRAIN,
    .green_channel  = {LedRouterState::LED_ROUTER_STATE_GREEN, AdcHalChannel::ADC_HAL_CHANNEL_3, 150u, 0x42u},
    .blue_channel   = {LedRouterState::LED_ROUTER_STATE_BLUE, AdcHalChannel::ADC_HAL_CHANNEL_0, 175u, 0xA7u},
    .adc_timeout_us = 50000u,
  };

  // Step 2: Seed baseline drift defaults with placeholder wiper codes that should be overridden.
  const PhoenixBenchmarkDriftCaptureDefaults baseline_defaults = {
      .start_time_us    = 10u,
      .end_time_us      = 250u,
      .step_delay_us    = 5u,
      .osr              = 4096u,
      .blue_wiper_code  = 0x00u,
      .green_wiper_code = 0x00u,
  };

  // Step 3: Derive defaults from the light configuration and confirm per-colour wipers carry through.
  const PhoenixBenchmarkDriftCaptureDefaults derived_defaults =
      phoenix_benchmark_drift_capture_defaults_from_light_config(light_config, baseline_defaults);

  TEST_ASSERT_EQUAL_UINT32(baseline_defaults.start_time_us, derived_defaults.start_time_us);
  TEST_ASSERT_EQUAL_UINT32(baseline_defaults.end_time_us, derived_defaults.end_time_us);
  TEST_ASSERT_EQUAL_UINT32(baseline_defaults.step_delay_us, derived_defaults.step_delay_us);
  TEST_ASSERT_EQUAL_UINT32(baseline_defaults.osr, derived_defaults.osr);
  TEST_ASSERT_EQUAL_UINT8(light_config.blue_channel.wiper_code, derived_defaults.blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(light_config.green_channel.wiper_code, derived_defaults.green_wiper_code);
}

static void test_drift_capture_parse_command_accepts_json_overrides(void) {
  // Step 1: Provide explicit overrides for timing, OSR, and the wiper code.
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise({.start_time_us    = 0u,
                                              .end_time_us      = 100000u,
                                              .step_delay_us    = 0u,
                                              .osr              = 4096u,
                                              .blue_wiper_code  = 0x00u,
                                              .green_wiper_code = 0x00u});

  const char* payload =
      "{\"command\":\"drift_capture\",\"parameters\":{\"start_time_us\":500,\"end_time_us\":1500,"
      "\"step_delay_us\":25,\"osr\":8192,\"wiper_code\":64}}";

  const PhoenixBenchmarkDriftCaptureParseResult result = phoenix_benchmark_drift_capture_parse_command(payload);

  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_TRUE(result.options.has_start_override);
  TEST_ASSERT_TRUE(result.options.has_end_override);
  TEST_ASSERT_TRUE(result.options.has_step_override);
  TEST_ASSERT_TRUE(result.options.has_osr_override);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
  TEST_ASSERT_EQUAL_UINT32(500u, result.options.start_time_us);
  TEST_ASSERT_EQUAL_UINT32(1500u, result.options.end_time_us);
  TEST_ASSERT_EQUAL_UINT32(25u, result.options.step_delay_us);
  TEST_ASSERT_EQUAL_UINT32(8192u, result.options.osr);
  TEST_ASSERT_EQUAL_UINT8(64u, result.options.blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(64u, result.options.green_wiper_code);
}

static void test_drift_capture_parse_command_rejects_invalid_range(void) {
  // Step 1: Attempt to parse a JSON payload where the capture window is inverted.
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise({.start_time_us    = 0u,
                                              .end_time_us      = 100000u,
                                              .step_delay_us    = 0u,
                                              .osr              = 4096u,
                                              .blue_wiper_code  = 0x00u,
                                              .green_wiper_code = 0x00u});

  const char* payload =
      "{\"command\":\"drift_capture\",\"parameters\":{\"start_time_us\":5000,\"end_time_us\":200,\"osr\":4096}}";

  const PhoenixBenchmarkDriftCaptureParseResult result = phoenix_benchmark_drift_capture_parse_command(payload);

  TEST_ASSERT_FALSE(result.success);
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_drift_capture_run_captures_leds_in_sequence(void) {
  // Step 1: Execute a deterministic capture covering both LED paths.
  phoenix_benchmark_drift_capture_reset_state();
  const PhoenixBenchmarkDriftCaptureDefaults defaults = {
      .start_time_us    = 0u,
      .end_time_us      = 20u,
      .step_delay_us    = 10u,
      .osr              = 4096u,
      .blue_wiper_code  = 0x12u,
      .green_wiper_code = 0x34u,
  };
  phoenix_benchmark_drift_capture_initialise(defaults);
  drift_capture_reset_fakes();

  static const int32_t blue_codes[]  = {100, 110, 120};
  static const int32_t green_codes[] = {200, 210, 220};
  g_drift_capture_blue_codes         = blue_codes;
  g_drift_capture_blue_length        = sizeof(blue_codes) / sizeof(blue_codes[0]);
  g_drift_capture_green_codes        = green_codes;
  g_drift_capture_green_length       = sizeof(green_codes) / sizeof(green_codes[0]);

  drift_capture_install_fakes();

  PhoenixBenchmarkDriftCaptureOptions options = {};
  options.apply_defaults(defaults);
  const PhoenixBenchmarkDriftCaptureOutputCallbacks callbacks = {nullptr};

  const PhoenixBenchmarkDriftCaptureExecutionStatus status = phoenix_benchmark_drift_capture_run(options, callbacks);

  drift_capture_uninstall_fakes();

  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_FALSE(status.has_warnings);
  TEST_ASSERT_EQUAL_UINT8(0u, status.warning_mask);
  TEST_ASSERT_EQUAL_UINT32(defaults.osr, status.applied_osr);
  TEST_ASSERT_EQUAL_UINT8(defaults.blue_wiper_code, status.applied_blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(defaults.green_wiper_code, status.applied_green_wiper_code);
  TEST_ASSERT_EQUAL_UINT32(defaults.start_time_us, status.applied_start_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.end_time_us, status.applied_end_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.step_delay_us, status.applied_step_us);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g_drift_capture_blue_length),
                           static_cast<uint32_t>(status.blue_samples));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g_drift_capture_green_length),
                           static_cast<uint32_t>(status.green_samples));
  TEST_ASSERT_EQUAL_UINT8(defaults.blue_wiper_code, g_drift_capture_last_blue_wiper_code);
  TEST_ASSERT_EQUAL_UINT8(defaults.green_wiper_code, g_drift_capture_last_green_wiper_code);
  TEST_ASSERT_TRUE(g_drift_capture_osr_call_count >= 1u);

  std::size_t                                     blue_count = 0u;
  const PhoenixBenchmarkDriftCaptureSample* const blue_samples =
      phoenix_benchmark_drift_capture_led_samples(PhoenixBenchmarkDriftCaptureLed::kBlue, &blue_count);
  TEST_ASSERT_NOT_NULL(blue_samples);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g_drift_capture_blue_length), static_cast<uint32_t>(blue_count));
  TEST_ASSERT_EQUAL_UINT32(0u, blue_samples[0].elapsed_microseconds);
  TEST_ASSERT_EQUAL_INT32(blue_codes[0], blue_samples[0].adc_code);
  TEST_ASSERT_EQUAL_UINT32(10u, blue_samples[1].elapsed_microseconds);
  TEST_ASSERT_EQUAL_INT32(blue_codes[1], blue_samples[1].adc_code);
  TEST_ASSERT_EQUAL_UINT32(20u, blue_samples[2].elapsed_microseconds);
  TEST_ASSERT_EQUAL_INT32(blue_codes[2], blue_samples[2].adc_code);

  std::size_t                                     green_count = 0u;
  const PhoenixBenchmarkDriftCaptureSample* const green_samples =
      phoenix_benchmark_drift_capture_led_samples(PhoenixBenchmarkDriftCaptureLed::kGreen, &green_count);
  TEST_ASSERT_NOT_NULL(green_samples);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g_drift_capture_green_length), static_cast<uint32_t>(green_count));
  TEST_ASSERT_EQUAL_UINT32(0u, green_samples[0].elapsed_microseconds);
  TEST_ASSERT_EQUAL_INT32(green_codes[0], green_samples[0].adc_code);

  TEST_ASSERT_TRUE(g_drift_capture_router_transition_count >= 3u);
  TEST_ASSERT_EQUAL(LedRouterState::LED_ROUTER_STATE_BLUE, g_drift_capture_router_transitions[0]);
  TEST_ASSERT_EQUAL(LedRouterState::LED_ROUTER_STATE_DRAIN, g_drift_capture_router_transitions[1]);
  TEST_ASSERT_EQUAL(LedRouterState::LED_ROUTER_STATE_GREEN, g_drift_capture_router_transitions[2]);
}

static void test_drift_capture_run_sets_saturation_warning(void) {
  // Step 1: Feed a saturated sample to confirm warning propagation.
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise({.start_time_us    = 0u,
                                              .end_time_us      = 0u,
                                              .step_delay_us    = 0u,
                                              .osr              = 4096u,
                                              .blue_wiper_code  = 0x00u,
                                              .green_wiper_code = 0x00u});
  drift_capture_reset_fakes();

  static const int32_t blue_codes[]  = {k_phoenix_benchmark_adc_positive_full_scale_code};
  static const int32_t green_codes[] = {0};
  g_drift_capture_blue_codes         = blue_codes;
  g_drift_capture_blue_length        = 1u;
  g_drift_capture_green_codes        = green_codes;
  g_drift_capture_green_length       = 1u;

  drift_capture_install_fakes();

  PhoenixBenchmarkDriftCaptureOptions options = {};
  options.apply_defaults({.start_time_us    = 0u,
                          .end_time_us      = 0u,
                          .step_delay_us    = 0u,
                          .osr              = 4096u,
                          .blue_wiper_code  = 0x00u,
                          .green_wiper_code = 0x00u});
  const PhoenixBenchmarkDriftCaptureOutputCallbacks callbacks = {nullptr};

  const PhoenixBenchmarkDriftCaptureExecutionStatus status = phoenix_benchmark_drift_capture_run(options, callbacks);

  drift_capture_uninstall_fakes();

  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_TRUE(status.has_warnings);
  TEST_ASSERT_NOT_EQUAL(0u, status.warning_mask & k_phoenix_benchmark_drift_capture_warning_saturation);
}

static void test_drift_capture_run_sets_overflow_warning(void) {
  // Step 1: Allow step=0 captures to saturate the static buffer and halt gracefully.
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise({.start_time_us    = 0u,
                                              .end_time_us      = 500000u,
                                              .step_delay_us    = 0u,
                                              .osr              = 4096u,
                                              .blue_wiper_code  = 0x00u,
                                              .green_wiper_code = 0x00u});
  drift_capture_reset_fakes();

  static int32_t led_codes[k_phoenix_benchmark_drift_capture_max_sample_count + 8u];
  for (std::size_t index = 0u; index < (sizeof(led_codes) / sizeof(led_codes[0])); ++index) {
    led_codes[index] = static_cast<int32_t>(index);
  }
  g_drift_capture_blue_codes   = led_codes;
  g_drift_capture_blue_length  = sizeof(led_codes) / sizeof(led_codes[0]);
  g_drift_capture_green_codes  = led_codes;
  g_drift_capture_green_length = sizeof(led_codes) / sizeof(led_codes[0]);

  drift_capture_install_fakes();

  PhoenixBenchmarkDriftCaptureOptions options                 = {};
  options.start_time_us                                       = 0u;
  options.end_time_us                                         = 500000u;
  options.step_delay_us                                       = 0u;
  const PhoenixBenchmarkDriftCaptureOutputCallbacks callbacks = {nullptr};

  const PhoenixBenchmarkDriftCaptureExecutionStatus status = phoenix_benchmark_drift_capture_run(options, callbacks);

  drift_capture_uninstall_fakes();

  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_TRUE(status.has_warnings);
  TEST_ASSERT_NOT_EQUAL(0u, status.warning_mask & k_phoenix_benchmark_drift_capture_warning_buffer_overflow);
  TEST_ASSERT_EQUAL_UINT32(k_phoenix_benchmark_drift_capture_max_sample_count,
                           static_cast<uint32_t>(status.blue_samples));
}

static void test_drift_capture_run_emits_nan_padding(void) {
  // Step 1: Capture mismatched LED lengths and confirm missing entries are padded.
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise({.start_time_us    = 0u,
                                              .end_time_us      = 10u,
                                              .step_delay_us    = 10u,
                                              .osr              = 4096u,
                                              .blue_wiper_code  = 0x00u,
                                              .green_wiper_code = 0x00u});
  drift_capture_reset_fakes();

  static const int32_t blue_codes[]  = {10, 20};
  static const int32_t green_codes[] = {30};
  g_drift_capture_blue_codes         = blue_codes;
  g_drift_capture_blue_length        = sizeof(blue_codes) / sizeof(blue_codes[0]);
  g_drift_capture_green_codes        = green_codes;
  g_drift_capture_green_length       = sizeof(green_codes) / sizeof(green_codes[0]);

  drift_capture_install_fakes();

  PhoenixBenchmarkDriftCaptureOptions options = {};
  options.apply_defaults({.start_time_us    = 0u,
                          .end_time_us      = 10u,
                          .step_delay_us    = 10u,
                          .osr              = 4096u,
                          .blue_wiper_code  = 0x00u,
                          .green_wiper_code = 0x00u});
  const PhoenixBenchmarkDriftCaptureOutputCallbacks callbacks = {drift_capture_collect_line};

  const PhoenixBenchmarkDriftCaptureExecutionStatus status = phoenix_benchmark_drift_capture_run(options, callbacks);

  drift_capture_uninstall_fakes();

  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_TRUE(g_drift_capture_output_count > 0u);

  bool saw_nan_padding = false;
  for (std::size_t index = 0u; index < g_drift_capture_output_count; ++index) {
    if (std::strstr(g_drift_capture_output_lines[index], "nan") != nullptr) {
      saw_nan_padding = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(saw_nan_padding);
}

static void test_is_adc_code_saturated_detects_full_scale_codes(void) {
  // Step 1: Assert the helper flags both positive and negative full-scale codes as saturated.
  TEST_ASSERT_TRUE(phoenix_benchmark_is_adc_code_saturated(8388607));
  TEST_ASSERT_TRUE(phoenix_benchmark_is_adc_code_saturated(-8388608));
  // Step 2: Ensure mid-scale values are treated as unsaturated.
  TEST_ASSERT_FALSE(phoenix_benchmark_is_adc_code_saturated(0));
  TEST_ASSERT_FALSE(phoenix_benchmark_is_adc_code_saturated(1024));
}

static void test_adc_speed_format_summary_header_renders_expected_columns(void) {
  // Step 1: Invoke the header formatter and confirm it reports success.
  char buffer[k_phoenix_benchmark_adc_speed_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_adc_speed_format_summary_header(buffer, sizeof(buffer)));

  // Step 2: Ensure the header uses the agreed column labels and alignment spacing.
  TEST_ASSERT_EQUAL_STRING("Mode          Samples_per_s        Loop_us     Errors Notes", buffer);
}

static void test_adc_speed_format_summary_row_formats_metrics(void) {
  // Step 1: Format a row with populated metrics so numeric alignment can be asserted.
  PhoenixBenchmarkAdcSpeedSummaryRowValues values = {
      .mode_label         = "Blocking",
      .samples_per_second = 12345.678,
      .loop_microseconds  = 42.5,
      .error_count        = 3u,
      .notes              = "ok",
      .has_metrics        = true,
  };

  char buffer[k_phoenix_benchmark_adc_speed_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_adc_speed_format_summary_row(values, buffer, sizeof(buffer)));

  // Step 2: Verify the rendered row contains the formatted metrics with fixed precision.
  TEST_ASSERT_NOT_NULL(strstr(buffer, "Blocking"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "12345.68"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "42.500"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "3"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "ok"));
}

static void test_adc_speed_format_summary_row_uses_placeholders_without_metrics(void) {
  // Step 1: Produce a row where metrics are absent so placeholders should appear.
  PhoenixBenchmarkAdcSpeedSummaryRowValues values = {
      .mode_label         = "IRQ",
      .samples_per_second = 0.0,
      .loop_microseconds  = 0.0,
      .error_count        = 0u,
      .notes              = nullptr,
      .has_metrics        = false,
  };

  char buffer[k_phoenix_benchmark_adc_speed_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_adc_speed_format_summary_row(values, buffer, sizeof(buffer)));

  // Step 2: Check that placeholder markers were emitted in place of real metrics.
  TEST_ASSERT_NOT_NULL(strstr(buffer, "IRQ"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "--"));
}

static void test_adc_speed_parse_command_line_accepts_json_payload(void) {
  // Step 1: Supply a JSON command enabling both blocking and IRQ modes with a custom duration.
  const char* command_line =
      "{\"command\":\"adc_speed\",\"parameters\":{\"duration_ms\":1500,\"enable_blocking\":true,\"enable_irq\":true}}";

  const PhoenixBenchmarkAdcSpeedParseOutcome outcome = phoenix_benchmark_adc_speed_parse_command_line(command_line);

  // Step 2: Expect the parser to accept the payload and surface the supplied configuration.
  TEST_ASSERT_TRUE(outcome.success);
  TEST_ASSERT_EQUAL_UINT32(1500u, outcome.options.duration_ms);
  TEST_ASSERT_TRUE(outcome.options.enable_blocking);
  TEST_ASSERT_TRUE(outcome.options.enable_irq);
  TEST_ASSERT_TRUE(outcome.options.has_duration_override);
  TEST_ASSERT_TRUE(outcome.options.has_blocking_override);
  TEST_ASSERT_TRUE(outcome.options.has_irq_override);
  TEST_ASSERT_NULL(outcome.error_message);
}

static void test_adc_speed_parse_command_line_rejects_invalid_duration(void) {
  // Step 1: Attempt to parse an invalid payload with a zero duration value.
  const char* command_line = "{\"command\":\"adc_speed\",\"parameters\":{\"duration_ms\":0}}";

  const PhoenixBenchmarkAdcSpeedParseOutcome outcome = phoenix_benchmark_adc_speed_parse_command_line(command_line);

  // Step 2: The parser should reject the command and surface an invalid value error.
  TEST_ASSERT_FALSE(outcome.success);
  TEST_ASSERT_EQUAL_STRING(k_phoenix_benchmark_adc_speed_error_invalid_value, outcome.error_message);
}

static void test_adc_speed_parse_command_uses_initialised_defaults(void) {
  // Step 1: Seed the adc speed module with non-default values.
  phoenix_benchmark_adc_speed_reset_state();
  const PhoenixBenchmarkAdcSpeedDefaults defaults = {
      .duration_ms     = 2500u,
      .enable_blocking = false,
      .enable_irq      = true,
  };
  phoenix_benchmark_adc_speed_initialise(defaults);

  // Step 2: Parse a minimal command and confirm defaults were applied to options.
  const PhoenixBenchmarkAdcSpeedParseResult result =
      phoenix_benchmark_adc_speed_parse_command("{\"command\":\"adc_speed\"}");
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_EQUAL_UINT32(defaults.duration_ms, result.options.duration_ms);
  TEST_ASSERT_EQUAL(defaults.enable_blocking, result.options.enable_blocking);
  TEST_ASSERT_EQUAL(defaults.enable_irq, result.options.enable_irq);
  TEST_ASSERT_FALSE(result.options.has_duration_override);
  TEST_ASSERT_FALSE(result.options.has_blocking_override);
  TEST_ASSERT_FALSE(result.options.has_irq_override);
}

static void test_adc_speed_parse_command_overrides_defaults(void) {
  // Step 1: Seed defaults and provide overrides for duration and mode flags.
  phoenix_benchmark_adc_speed_reset_state();
  const PhoenixBenchmarkAdcSpeedDefaults defaults = {
      .duration_ms     = 500u,
      .enable_blocking = true,
      .enable_irq      = false,
  };
  phoenix_benchmark_adc_speed_initialise(defaults);

  const char* command_line =
      "{\"command\":\"adc_speed\",\"parameters\":{\"duration_ms\":1500,\"enable_blocking\":false,\"enable_irq\":true}}";

  // Step 2: Parse and confirm overrides win over defaults.
  const PhoenixBenchmarkAdcSpeedParseResult result = phoenix_benchmark_adc_speed_parse_command(command_line);
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_EQUAL_UINT32(1500u, result.options.duration_ms);
  TEST_ASSERT_FALSE(result.options.enable_blocking);
  TEST_ASSERT_TRUE(result.options.enable_irq);
  TEST_ASSERT_TRUE(result.options.has_duration_override);
  TEST_ASSERT_TRUE(result.options.has_blocking_override);
  TEST_ASSERT_TRUE(result.options.has_irq_override);
}

static uint32_t g_adc_speed_fake_micros = 0u;

static uint32_t adc_speed_fake_micros_provider(void) {
  return g_adc_speed_fake_micros;
}

static bool adc_speed_blocking_only_provider(PhoenixBenchmarkAdcSpeedTestMode mode, uint32_t iteration,
                                             int32_t* out_sample) {
  (void) iteration;
  if (out_sample != nullptr) {
    *out_sample = 0;
  }
  if (mode != PhoenixBenchmarkAdcSpeedTestMode::kBlocking) {
    return false;
  }
  g_adc_speed_fake_micros += 1000u;
  return true;
}

static bool adc_speed_dual_mode_provider(PhoenixBenchmarkAdcSpeedTestMode mode, uint32_t iteration,
                                         int32_t* out_sample) {
  if (out_sample != nullptr) {
    *out_sample = static_cast<int32_t>(iteration);
  }
  if (mode == PhoenixBenchmarkAdcSpeedTestMode::kBlocking) {
    g_adc_speed_fake_micros += 750u;
    return true;
  }
  g_adc_speed_fake_micros += 1500u;
  return iteration != 2u;
}

static void adc_speed_reset_test_hooks(void) {
  phoenix_benchmark_adc_speed_clear_sample_provider_for_test();
  phoenix_benchmark_adc_speed_clear_micros_provider_for_test();
  g_adc_speed_fake_micros = 0u;
}

static void test_adc_speed_run_collects_blocking_metrics(void) {
  // Step 1: Install deterministic hooks so the run executes without hardware dependencies.
  adc_speed_reset_test_hooks();
  phoenix_benchmark_adc_speed_set_sample_provider_for_test(adc_speed_blocking_only_provider);
  phoenix_benchmark_adc_speed_set_micros_provider_for_test(adc_speed_fake_micros_provider);

  PhoenixBenchmarkAdcSpeedOptions options = {
      .duration_ms           = 5u,
      .enable_blocking       = true,
      .enable_irq            = false,
      .has_duration_override = true,
      .has_blocking_override = true,
      .has_irq_override      = true,
  };

  const PhoenixBenchmarkAdcSpeedExecutionStatus status = phoenix_benchmark_adc_speed_run(options, nullptr, 0u);

  adc_speed_reset_test_hooks();

  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_FALSE(status.has_warnings);
  TEST_ASSERT_NULL(status.message);
  TEST_ASSERT_TRUE(status.blocking_executed);
  TEST_ASSERT_FALSE(status.irq_executed);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 1000.0, status.blocking_samples_per_second);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 1000.0, status.blocking_loop_microseconds);
  TEST_ASSERT_EQUAL_UINT32(0u, status.blocking_error_count);
}

static void test_adc_speed_run_collects_dual_mode_metrics(void) {
  // Step 1: Exercise both blocking and IRQ paths with scripted timing.
  adc_speed_reset_test_hooks();
  phoenix_benchmark_adc_speed_set_sample_provider_for_test(adc_speed_dual_mode_provider);
  phoenix_benchmark_adc_speed_set_micros_provider_for_test(adc_speed_fake_micros_provider);

  PhoenixBenchmarkAdcSpeedOptions options = {
      .duration_ms           = 9u,
      .enable_blocking       = true,
      .enable_irq            = true,
      .has_duration_override = true,
      .has_blocking_override = true,
      .has_irq_override      = true,
  };

  const PhoenixBenchmarkAdcSpeedExecutionStatus status = phoenix_benchmark_adc_speed_run(options, nullptr, 0u);

  adc_speed_reset_test_hooks();

  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_TRUE(status.has_warnings);
  TEST_ASSERT_TRUE(status.blocking_executed);
  TEST_ASSERT_TRUE(status.irq_executed);
  TEST_ASSERT_TRUE(status.blocking_samples_per_second > 0.0);
  TEST_ASSERT_TRUE(status.irq_samples_per_second > 0.0);
  TEST_ASSERT_EQUAL_UINT32(0u, status.blocking_error_count);
  TEST_ASSERT_EQUAL_UINT32(1u, status.irq_error_count);
}

static void test_dwell_sweep_run_uses_light_readings_saturation_reporting(void) {
  // Step 1: Configure a single-step dwell sweep and force saturation via the light readings helper.
  phoenix_benchmark_dwell_sweep_reset_state();
  PhoenixBenchmarkDwellSweepOptions options = {
      .sweeps_per_dwell    = 1u,
      .has_sweeps_override = true,
      .start_dwell_us      = 100u,
      .has_start_override  = true,
      .end_dwell_us        = 100u,
      .has_end_override    = true,
      .dwell_step_us       = 100u,
      .has_step_override   = true,
  };

  light_readings_force_saturation_for_test(true);

  // Step 2: Execute the dwell sweep and capture the resulting metrics.
  static PhoenixBenchmarkDwellSweepRowMetrics     rows[1] = {};
  const PhoenixBenchmarkDwellSweepExecutionStatus status =
      phoenix_benchmark_dwell_sweep_run(options, rows, sizeof(rows) / sizeof(rows[0]));

  light_readings_force_saturation_for_test(false);

  // Step 3: Confirm the run succeeded and reported saturation via light readings warnings.
  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_EQUAL_UINT32(1u, status.rows_generated);
  TEST_ASSERT_TRUE(status.has_warnings);
  TEST_ASSERT_EQUAL_UINT32(options.sweeps_per_dwell, rows[0].sweeps_completed);
  TEST_ASSERT_NULL(rows[0].error_message);
  TEST_ASSERT_NOT_EQUAL(0u, rows[0].warning_mask & k_phoenix_benchmark_dwell_sweep_warning_saturation);
  TEST_ASSERT_NOT_EQUAL(0u, rows[0].warning_mask & k_phoenix_benchmark_dwell_sweep_warning_adc_error);
}

static void test_osr_sweep_run_reports_light_readings_warnings(void) {
  // Step 1: Configure a minimal OSR sweep and force saturation to exercise light readings integration.
  phoenix_benchmark_osr_sweep_reset_state();
  PhoenixBenchmarkOsrSweepOptions options = {
      .sweep_count        = 1u,
      .has_sweep_override = true,
      .dwell_us           = 100u,
      .has_dwell_override = true,
      .wiper_code         = 0x20u,
      .has_wiper_override = true,
  };

  static PhoenixBenchmarkOsrSweepRowMetrics rows[k_phoenix_benchmark_osr_value_count];
  for (std::size_t index = 0u; index < k_phoenix_benchmark_osr_value_count; ++index) {
    rows[index] = PhoenixBenchmarkOsrSweepRowMetrics{};
  }
  light_readings_reset_for_test();
  light_readings_force_saturation_for_test(true);
  const PhoenixBenchmarkOsrSweepExecutionStatus status =
      phoenix_benchmark_osr_sweep_run(options, rows, k_phoenix_benchmark_osr_value_count);
  phoenix_benchmark_osr_sweep_clear_test_hooks();

  light_readings_force_saturation_for_test(false);

  // Step 2: Confirm the sweep succeeded, surfaced the forced saturation, and populated each OSR row.
  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_TRUE(status.has_warnings);
  TEST_ASSERT_EQUAL_UINT32(k_phoenix_benchmark_osr_value_count, status.rows_generated);

  uint32_t saturated_rows = 0u;
  for (std::size_t index = 0u; index < status.rows_generated; ++index) {
    TEST_ASSERT_EQUAL_UINT32(options.sweep_count, rows[index].sweep_count);
    if ((rows[index].blue.channel_a_saturation_count > 0u) && (rows[index].green.channel_b_saturation_count > 0u)) {
      ++saturated_rows;
    }
  }
  TEST_ASSERT_TRUE(saturated_rows > 0u);
}

static void test_pot_sweep_run_reports_light_readings_saturation(void) {
  // Step 1: Run a full pot sweep with saturation forced to validate light readings driven logic.
  phoenix_benchmark_pot_sweep_reset_state();
  PhoenixBenchmarkPotSweepOptions options = {
      .sweeps_per_wiper    = 1u,
      .has_sweeps_override = true,
      .dwell_us            = 100u,
      .has_dwell_override  = true,
  };

  static PhoenixBenchmarkPotSweepRowMetrics rows[k_phoenix_benchmark_pot_sweep_max_wiper_count];
  for (std::size_t index = 0u; index < k_phoenix_benchmark_pot_sweep_max_wiper_count; ++index) {
    rows[index] = PhoenixBenchmarkPotSweepRowMetrics{};
  }
  light_readings_reset_for_test();
  light_readings_force_saturation_for_test(true);
  const PhoenixBenchmarkPotSweepExecutionStatus status =
      phoenix_benchmark_pot_sweep_run(options, rows, k_phoenix_benchmark_pot_sweep_max_wiper_count);
  phoenix_benchmark_pot_sweep_clear_test_hooks();

  light_readings_force_saturation_for_test(false);

  // Step 2: Verify the sweep surfaced saturation warnings and fell back to the default recommendations.
  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_TRUE(status.has_warnings);
  TEST_ASSERT_EQUAL_UINT32(k_phoenix_benchmark_pot_sweep_max_wiper_count, status.rows_generated);
  TEST_ASSERT_TRUE(status.blue_recommendation_valid);
  TEST_ASSERT_EQUAL_UINT8(0x00u, status.blue_recommended_wiper);
  TEST_ASSERT_TRUE(status.green_recommendation_valid);
  TEST_ASSERT_EQUAL_UINT8(0x00u, status.green_recommended_wiper);

  for (uint32_t index = 0u; index < status.rows_generated; ++index) {
    TEST_ASSERT_TRUE(rows[index].blue_saturated);
    TEST_ASSERT_TRUE(rows[index].green_saturated);
  }
}

static uint32_t g_osr_latency_blocking_call_count = 0u;
static uint32_t g_osr_latency_irq_call_count      = 0u;
static uint32_t g_osr_latency_summary_line_count  = 0u;

static bool osr_latency_fake_blocking_sampler(mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count,
                                              PhoenixBenchmarkRunningStats<uint32_t>* stats_out) {
  (void) osr;
  (void) warmup_count;

  ++g_osr_latency_blocking_call_count;
  if ((stats_out != NULL) && (sample_count > 0u)) {
    // Step 1: Record a deterministic sample so future formatting can surface metrics.
    stats_out->update(sample_count * 10u);
    stats_out->update(sample_count * 12u);
  }
  return true;
}

static bool osr_latency_fake_irq_sampler(mcp356x_osr osr, uint32_t warmup_count, uint32_t sample_count,
                                         PhoenixBenchmarkRunningStats<uint32_t>* stats_out) {
  (void) osr;
  (void) warmup_count;

  ++g_osr_latency_irq_call_count;
  if ((stats_out != NULL) && (sample_count > 0u)) {
    // Step 1: Record a deterministic sample set for IRQ timing.
    stats_out->update(sample_count * 8u);
    stats_out->update(sample_count * 9u);
  }
  return true;
}

static void osr_latency_fake_summary_writer(const char* line) {
  if (line != NULL) {
    ++g_osr_latency_summary_line_count;
  }
}

static void test_osr_latency_options_apply_defaults_inherit_initialised_values(void) {
  // Step 1: Create options without overrides so defaults should populate each field.
  PhoenixBenchmarkOsrLatencyOptions options = {};
  options.sample_count                      = 0u;
  options.warmup_count                      = 0u;
  options.include_blocking                  = false;
  options.include_irq                       = false;
  options.has_sample_override               = false;
  options.has_warmup_override               = false;
  options.has_blocking_override             = false;
  options.has_irq_override                  = false;

  // Step 2: Provide defaults that the helper should apply to the options.
  const PhoenixBenchmarkOsrLatencyDefaults defaults = {
      .warmup_count     = 2u,
      .sample_count     = 12u,
      .include_blocking = true,
      .include_irq      = false,
  };

  // Step 3: Apply default values and confirm each field inherited the defaults.
  options.apply_defaults(defaults);

  TEST_ASSERT_EQUAL_UINT32(defaults.sample_count, options.sample_count);
  TEST_ASSERT_EQUAL_UINT32(defaults.warmup_count, options.warmup_count);
  TEST_ASSERT_EQUAL(defaults.include_blocking, options.include_blocking);
  TEST_ASSERT_EQUAL(defaults.include_irq, options.include_irq);
}

static void test_osr_latency_options_validate_rejects_zero_sample_count(void) {
  // Step 1: Compose options with an invalid zero sample count.
  PhoenixBenchmarkOsrLatencyOptions options = {
      .warmup_count          = 1u,
      .sample_count          = 0u,
      .include_blocking      = true,
      .include_irq           = true,
      .has_warmup_override   = true,
      .has_sample_override   = true,
      .has_blocking_override = true,
      .has_irq_override      = true,
  };

  // Step 2: Validate and expect the helper to reject the invalid configuration.
  const char* error_message = NULL;
  TEST_ASSERT_FALSE(options.validate(&error_message));
  TEST_ASSERT_NOT_NULL(error_message);
}

static void test_osr_latency_parse_command_accepts_plain_payload(void) {
  // Step 1: Seed the module with deterministic defaults.
  phoenix_benchmark_osr_latency_reset_state();
  const PhoenixBenchmarkOsrLatencyDefaults defaults = {
      .warmup_count     = 3u,
      .sample_count     = 10u,
      .include_blocking = true,
      .include_irq      = false,
  };
  phoenix_benchmark_osr_latency_initialise(defaults);

  // Step 2: Parse a minimal JSON command and confirm defaults were applied.
  const PhoenixBenchmarkOsrLatencyParseResult result =
      phoenix_benchmark_osr_latency_parse_command("{\"command\":\"osr_latency\"}");

  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_EQUAL_UINT32(defaults.sample_count, result.options.sample_count);
  TEST_ASSERT_EQUAL_UINT32(defaults.warmup_count, result.options.warmup_count);
  TEST_ASSERT_EQUAL(defaults.include_blocking, result.options.include_blocking);
  TEST_ASSERT_EQUAL(defaults.include_irq, result.options.include_irq);
}

static void test_osr_latency_parse_command_rejects_invalid_payload(void) {
  // Step 1: Initialise defaults so validation can run after parsing.
  phoenix_benchmark_osr_latency_reset_state();
  phoenix_benchmark_osr_latency_initialise(
      {.warmup_count = 1u, .sample_count = 8u, .include_blocking = true, .include_irq = true});

  // Step 2: Provide a payload with an invalid sample override and expect failure.
  const PhoenixBenchmarkOsrLatencyParseResult result =
      phoenix_benchmark_osr_latency_parse_command("{\"command\":\"osr_latency\",\"parameters\":{\"samples\":0}}");

  TEST_ASSERT_FALSE(result.success);
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_osr_latency_format_summary_header_matches_expected_columns(void) {
  // Step 1: Render the summary header and ensure the agreed column labels appear.
  char buffer[k_phoenix_benchmark_osr_latency_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_osr_latency_format_summary_header(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("OSR      Mode      Samples  Mean_us  StdDev_us  Min_us  Max_us", buffer);
}

static void test_osr_latency_format_summary_row_formats_metrics(void) {
  // Step 1: Populate a row with representative metrics.
  const PhoenixBenchmarkOsrLatencySummaryRowValues values = {
      .osr_label    = "OSR4096",
      .mode_label   = "blocking",
      .sample_count = 12u,
      .mean_us      = 123.456,
      .stddev_us    = 3.21,
      .min_us       = 111u,
      .max_us       = 130u,
      .has_metrics  = true,
  };

  // Step 2: Format the row and confirm each metric surfaced.
  char buffer[k_phoenix_benchmark_osr_latency_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_osr_latency_format_summary_row(values, buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "OSR4096"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "blocking"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "123.456"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "3.210"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "111"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "130"));
}

static void test_osr_latency_format_summary_row_uses_placeholders_without_metrics(void) {
  // Step 1: Prepare a row that lacks metrics so placeholder handling can be asserted.
  const PhoenixBenchmarkOsrLatencySummaryRowValues values = {
      .osr_label    = "OSR8192",
      .mode_label   = "irq",
      .sample_count = 0u,
      .mean_us      = 0.0,
      .stddev_us    = 0.0,
      .min_us       = 0u,
      .max_us       = 0u,
      .has_metrics  = false,
  };

  // Step 2: Format the row and confirm placeholders appear for missing data.
  char buffer[k_phoenix_benchmark_osr_latency_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_osr_latency_format_summary_row(values, buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "OSR8192"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "--"));
}

static void test_osr_latency_run_invokes_registered_samplers(void) {
  // Step 1: Reset module state and install deterministic sampler hooks.
  phoenix_benchmark_osr_latency_reset_state();
  const PhoenixBenchmarkOsrLatencyDefaults defaults = {
      .warmup_count     = 1u,
      .sample_count     = 4u,
      .include_blocking = true,
      .include_irq      = true,
  };
  phoenix_benchmark_osr_latency_initialise(defaults);

  g_osr_latency_blocking_call_count = 0u;
  g_osr_latency_irq_call_count      = 0u;
  g_osr_latency_summary_line_count  = 0u;

  phoenix_benchmark_osr_latency_set_blocking_sampler_for_test(osr_latency_fake_blocking_sampler);
  phoenix_benchmark_osr_latency_set_irq_sampler_for_test(osr_latency_fake_irq_sampler);

  PhoenixBenchmarkOsrLatencyOptions options = {
      .warmup_count          = defaults.warmup_count,
      .sample_count          = defaults.sample_count,
      .include_blocking      = true,
      .include_irq           = true,
      .has_warmup_override   = true,
      .has_sample_override   = true,
      .has_blocking_override = true,
      .has_irq_override      = true,
  };

  PhoenixBenchmarkOsrLatencyMeasurement           rows[k_phoenix_benchmark_osr_latency_row_capacity] = {};
  const PhoenixBenchmarkOsrLatencyOutputCallbacks callbacks = {osr_latency_fake_summary_writer};

  // Step 2: Execute the runner and expect it to invoke the configured hooks.
  const PhoenixBenchmarkOsrLatencyExecutionStatus status =
      phoenix_benchmark_osr_latency_run(options, rows, k_phoenix_benchmark_osr_latency_row_capacity, callbacks);

  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_NOT_EQUAL(0u, status.rows_generated);
  TEST_ASSERT_NOT_EQUAL(0u, g_osr_latency_blocking_call_count);
  TEST_ASSERT_NOT_EQUAL(0u, g_osr_latency_irq_call_count);
  TEST_ASSERT_NOT_EQUAL(0u, g_osr_latency_summary_line_count);

  // Step 3: Clear hooks so subsequent tests start from the production configuration.
  phoenix_benchmark_osr_latency_clear_test_hooks();
}

static void test_osr_sweep_parse_command_applies_initialised_defaults(void) {
  // Step 1: Seed OSR sweep defaults and parse a minimal command referencing the sweep.
  phoenix_benchmark_osr_sweep_reset_state();
  const PhoenixBenchmarkOsrSweepDefaults defaults = {
      .sweep_count = 75u,
      .dwell_us    = 250u,
      .wiper_code  = 0x40u,
  };
  phoenix_benchmark_osr_sweep_initialise(defaults);

  const PhoenixBenchmarkOsrSweepParseResult result =
      phoenix_benchmark_osr_sweep_parse_command("{\"command\":\"osr_sweep\"}");

  // Step 2: Confirm defaults were applied in the absence of overrides.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_FALSE(result.options.has_sweep_override);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
  TEST_ASSERT_FALSE(result.options.has_wiper_override);
  TEST_ASSERT_EQUAL_UINT32(defaults.sweep_count, result.options.sweep_count);
  TEST_ASSERT_EQUAL_UINT32(defaults.dwell_us, result.options.dwell_us);
  TEST_ASSERT_EQUAL_UINT8(defaults.wiper_code, result.options.wiper_code);
}

static void test_osr_sweep_parse_command_accepts_overrides(void) {
  // Step 1: Provide explicit overrides for sweep count, dwell time, and wiper code.
  phoenix_benchmark_osr_sweep_reset_state();
  const char* payload =
      "{\"command\":\"osr_sweep\",\"parameters\":{\"sweeps\":150,\"dwell_us\":500,\"wiper_code\":128}}";

  const PhoenixBenchmarkOsrSweepParseResult result = phoenix_benchmark_osr_sweep_parse_command(payload);

  // Step 2: Validate that overrides were captured.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_TRUE(result.options.has_sweep_override);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_TRUE(result.options.has_wiper_override);
  TEST_ASSERT_EQUAL_UINT32(150u, result.options.sweep_count);
  TEST_ASSERT_EQUAL_UINT32(500u, result.options.dwell_us);
  TEST_ASSERT_EQUAL_UINT8(128u, result.options.wiper_code);
}

static void test_dwell_sweep_parse_command_applies_defaults(void) {
  // Step 1: Seed dwell sweep defaults so minimal commands inherit the baseline.
  phoenix_benchmark_dwell_sweep_reset_state();
  const PhoenixBenchmarkDwellSweepDefaults defaults = {
      .sweeps_per_dwell = 4u,
      .start_dwell_us   = 100u,
      .end_dwell_us     = 400u,
      .dwell_step_us    = 100u,
  };
  phoenix_benchmark_dwell_sweep_initialise(defaults);

  // Step 2: Parse a minimal JSON payload and verify defaults populate the options.
  const PhoenixBenchmarkDwellSweepParseResult result =
      phoenix_benchmark_dwell_sweep_parse_command("{\"command\":\"dwell_sweep\"}");

  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_FALSE(result.options.has_sweeps_override);
  TEST_ASSERT_FALSE(result.options.has_start_override);
  TEST_ASSERT_FALSE(result.options.has_end_override);
  TEST_ASSERT_FALSE(result.options.has_step_override);
  TEST_ASSERT_EQUAL_UINT32(defaults.sweeps_per_dwell, result.options.sweeps_per_dwell);
  TEST_ASSERT_EQUAL_UINT32(defaults.start_dwell_us, result.options.start_dwell_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.end_dwell_us, result.options.end_dwell_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.dwell_step_us, result.options.dwell_step_us);
}

static void test_dwell_sweep_parse_command_accepts_plain_command(void) {
  // Step 1: Reset defaults so the bare command pathway inherits the baseline configuration.
  phoenix_benchmark_dwell_sweep_reset_state();

  const PhoenixBenchmarkDwellSweepParseResult result = phoenix_benchmark_dwell_sweep_parse_command("dwell_sweep");

  // Step 2: Confirm the parser treats the plain command as valid and applies defaults.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_FALSE(result.options.has_sweeps_override);
  TEST_ASSERT_FALSE(result.options.has_start_override);
  TEST_ASSERT_FALSE(result.options.has_end_override);
  TEST_ASSERT_FALSE(result.options.has_step_override);
  TEST_ASSERT_EQUAL_UINT32(4u, result.options.sweeps_per_dwell);
  TEST_ASSERT_EQUAL_UINT32(100u, result.options.start_dwell_us);
  TEST_ASSERT_EQUAL_UINT32(400u, result.options.end_dwell_us);
  TEST_ASSERT_EQUAL_UINT32(100u, result.options.dwell_step_us);
}

static void test_dwell_sweep_parse_command_rejects_invalid_ranges(void) {
  // Step 1: Attempt to parse an invalid payload featuring a zero step and inverted range.
  phoenix_benchmark_dwell_sweep_reset_state();
  const char* payload =
      "{\"command\":\"dwell_sweep\",\"parameters\":{\"sweeps_per_dwell\":0,\"start_dwell_us\":500,\"end_dwell_us\":100,\"dwell_step_us\":0}}";

  const PhoenixBenchmarkDwellSweepParseResult result = phoenix_benchmark_dwell_sweep_parse_command(payload);

  TEST_ASSERT_FALSE(result.success);
  TEST_ASSERT_NOT_NULL(result.error_message);
}

static void test_dwell_sweep_options_apply_defaults_inherit_initialised_values(void) {
  // Step 1: Provide defaults and confirm options copy each field when overrides are absent.
  const PhoenixBenchmarkDwellSweepDefaults defaults = {
      .sweeps_per_dwell = 4u,
      .start_dwell_us   = 100u,
      .end_dwell_us     = 400u,
      .dwell_step_us    = 100u,
  };

  PhoenixBenchmarkDwellSweepOptions options = {};
  options.has_sweeps_override               = false;
  options.has_start_override                = false;
  options.has_end_override                  = false;
  options.has_step_override                 = false;

  options.apply_defaults(defaults);

  TEST_ASSERT_EQUAL_UINT32(defaults.sweeps_per_dwell, options.sweeps_per_dwell);
  TEST_ASSERT_EQUAL_UINT32(defaults.start_dwell_us, options.start_dwell_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.end_dwell_us, options.end_dwell_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.dwell_step_us, options.dwell_step_us);
}

static void test_dwell_sweep_options_validate_accepts_nominal_configuration(void) {
  // Step 1: Populate a valid configuration and ensure validation succeeds.
  PhoenixBenchmarkDwellSweepOptions options = {
      .sweeps_per_dwell    = 5u,
      .has_sweeps_override = true,
      .start_dwell_us      = 100u,
      .has_start_override  = true,
      .end_dwell_us        = 400u,
      .has_end_override    = true,
      .dwell_step_us       = 50u,
      .has_step_override   = true,
  };

  const char* error_message = nullptr;
  TEST_ASSERT_TRUE(options.validate(&error_message));
  TEST_ASSERT_NULL(error_message);
}

static void test_dwell_sweep_run_rejects_null_row_buffer(void) {
  // Step 1: Invoke the runner without a destination buffer so it reports invalid arguments.
  PhoenixBenchmarkDwellSweepOptions options = {
      .sweeps_per_dwell    = 1u,
      .has_sweeps_override = true,
      .start_dwell_us      = 0u,
      .has_start_override  = true,
      .end_dwell_us        = 0u,
      .has_end_override    = true,
      .dwell_step_us       = 1u,
      .has_step_override   = true,
  };

  const PhoenixBenchmarkDwellSweepExecutionStatus status = phoenix_benchmark_dwell_sweep_run(options, nullptr, 0u);

  TEST_ASSERT_FALSE(status.success);
  TEST_ASSERT_NOT_NULL(status.message);
  TEST_ASSERT_EQUAL_STRING("invalid arguments", status.message);
  TEST_ASSERT_EQUAL_UINT32(0u, status.rows_generated);
}
static void test_osr_sweep_run_iterates_all_osr_values(void) {
  // Step 1: Configure modest defaults so the hardware run completes quickly.
  phoenix_benchmark_osr_sweep_reset_state();
  const PhoenixBenchmarkOsrSweepDefaults defaults = {
      .sweep_count = 1u,
      .dwell_us    = 50u,
      .wiper_code  = 0x00u,
  };
  phoenix_benchmark_osr_sweep_initialise(defaults);

  PhoenixBenchmarkOsrSweepOptions options = {};
  options.apply_defaults(defaults);

  static PhoenixBenchmarkOsrSweepRowMetrics rows[k_phoenix_benchmark_osr_value_count] = {};

  const PhoenixBenchmarkOsrSweepExecutionStatus status =
      phoenix_benchmark_osr_sweep_run(options, rows, k_phoenix_benchmark_osr_value_count);

  // Step 2: Confirm the sweep produced metrics for each OSR value in the table.
  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_EQUAL_UINT32(k_phoenix_benchmark_osr_value_count, status.rows_generated);
  for (std::size_t index = 0u; index < status.rows_generated; ++index) {
    TEST_ASSERT_EQUAL_UINT32(defaults.sweep_count, rows[index].sweep_count);
  }
}

static void test_osr_sweep_format_summary_header(void) {
  // Step 1: Render the summary header and confirm each column appears.
  char buffer[k_phoenix_benchmark_osr_sweep_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_osr_sweep_format_summary_header(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING(
      "Value      Samples  Drain_Blue_Mean  Drain_Blue_Std  Drain_Blue_Min  Drain_Blue_Max  Drain_Green_Mean  Drain_Green_Std  Drain_Green_Min  Drain_Green_Max  Blue_Mean  Blue_Std  Blue_Min  Blue_Max  Green_Mean  Green_Std  Green_Min  Green_Max  Sweep_us",
      buffer);
}

static void test_osr_sweep_format_summary_row_formats_metrics(void) {
  // Step 1: Populate a row with metrics so alignment and precision can be exercised.
  PhoenixBenchmarkOsrSweepSummaryRowValues values = {};
  values.label                                    = "OSR512";
  values.sample_count                             = 100u;
  values.drain_blue_mean                          = 123.456;
  values.drain_blue_std                           = 0.25;
  values.drain_blue_min                           = 120.0;
  values.drain_blue_max                           = 128.0;
  values.drain_green_mean                         = 223.1;
  values.drain_green_std                          = 0.75;
  values.drain_green_min                          = 219.0;
  values.drain_green_max                          = 229.0;
  values.blue_mean                                = 323.9;
  values.blue_std                                 = 0.80;
  values.blue_min                                 = 318.0;
  values.blue_max                                 = 329.0;
  values.green_mean                               = 423.9;
  values.green_std                                = 0.90;
  values.green_min                                = 420.0;
  values.green_max                                = 430.0;
  values.sweep_duration_us                        = 12345u;
  values.has_metrics                              = true;

  char buffer[k_phoenix_benchmark_osr_sweep_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_osr_sweep_format_summary_row(values, buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "OSR512"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "123.456"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "0.250"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "229.000"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "12345"));
}

static void test_osr_sweep_format_summary_row_uses_placeholders_without_metrics(void) {
  // Step 1: Render a row where metrics were absent so placeholders appear.
  PhoenixBenchmarkOsrSweepSummaryRowValues values = {};
  values.label                                    = "OSR8192";
  values.sample_count                             = 0u;
  values.sweep_duration_us                        = 0u;
  values.has_metrics                              = false;

  char buffer[k_phoenix_benchmark_osr_sweep_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_osr_sweep_format_summary_row(values, buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "OSR8192"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "--"));
}

static void test_pot_sweep_parse_command_applies_defaults(void) {
  // Step 1: Configure defaults with a distinct dwell override to simplify validation.
  phoenix_benchmark_pot_sweep_reset_state();
  const PhoenixBenchmarkPotSweepDefaults defaults = {
      .sweeps_per_wiper = 5u,
      .dwell_us         = 100u,
  };
  phoenix_benchmark_pot_sweep_initialise(defaults);

  const PhoenixBenchmarkPotSweepParseResult result =
      phoenix_benchmark_pot_sweep_parse_command("{\"command\":\"pot_sweep\"}");

  // Step 2: Confirm defaults applied when no overrides are provided.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_EQUAL_UINT32(defaults.sweeps_per_wiper, result.options.sweeps_per_wiper);
  TEST_ASSERT_FALSE(result.options.has_sweeps_override);
  TEST_ASSERT_EQUAL_UINT32(defaults.dwell_us, result.options.dwell_us);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
}

static void test_pot_sweep_parse_command_accepts_plain_token(void) {
  // Step 1: Seed defaults so the plain command inherits them directly.
  phoenix_benchmark_pot_sweep_reset_state();
  const PhoenixBenchmarkPotSweepDefaults defaults = {
      .sweeps_per_wiper = 7u,
      .dwell_us         = 100u,
  };
  phoenix_benchmark_pot_sweep_initialise(defaults);

  const PhoenixBenchmarkPotSweepParseResult result = phoenix_benchmark_pot_sweep_parse_command("pot_sweep");

  // Step 2: Ensure the parser accepts the plain token and applies defaults.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_EQUAL_UINT32(defaults.sweeps_per_wiper, result.options.sweeps_per_wiper);
  TEST_ASSERT_FALSE(result.options.has_sweeps_override);
  TEST_ASSERT_EQUAL_UINT32(defaults.dwell_us, result.options.dwell_us);
  TEST_ASSERT_FALSE(result.options.has_dwell_override);
}

static void test_pot_sweep_parse_command_accepts_overrides(void) {
  // Step 1: Reset defaults and issue a command overriding sweeps and dwell time.
  phoenix_benchmark_pot_sweep_reset_state();
  phoenix_benchmark_pot_sweep_initialise({.sweeps_per_wiper = 4u, .dwell_us = 75u});

  const char* payload = "{\"command\":\"pot_sweep\",\"parameters\":{\"sweeps\":3,\"dwell_us\":500}}";

  const PhoenixBenchmarkPotSweepParseResult result = phoenix_benchmark_pot_sweep_parse_command(payload);

  // Step 2: Validate overrides captured correctly.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_TRUE(result.options.has_sweeps_override);
  TEST_ASSERT_TRUE(result.options.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT32(3u, result.options.sweeps_per_wiper);
  TEST_ASSERT_EQUAL_UINT32(500u, result.options.dwell_us);
}

static void test_pot_sweep_parse_command_rejects_wiper_parameters(void) {
  // Step 1: Ensure any attempt to supply legacy wiper controls is rejected.
  phoenix_benchmark_pot_sweep_reset_state();
  phoenix_benchmark_pot_sweep_initialise({.sweeps_per_wiper = 5u, .dwell_us = 100u});

  const PhoenixBenchmarkPotSweepParseResult list_result =
      phoenix_benchmark_pot_sweep_parse_command("{\"command\":\"pot_sweep\",\"parameters\":{\"wipers\":[0,16]}}");
  TEST_ASSERT_FALSE(list_result.success);

  const PhoenixBenchmarkPotSweepParseResult range_result =
      phoenix_benchmark_pot_sweep_parse_command("{\"command\":\"pot_sweep\",\"parameters\":{\"wiper_start\":0}}");
  TEST_ASSERT_FALSE(range_result.success);
}
static void test_pot_sweep_run_rejects_insufficient_row_capacity(void) {
  // Step 1: Provide fewer rows than the firmware requires so input validation fails early.
  phoenix_benchmark_pot_sweep_reset_state();
  PhoenixBenchmarkPotSweepOptions options = {};
  options.sweeps_per_wiper                = 1u;
  options.has_sweeps_override             = true;
  options.dwell_us                        = 100u;
  options.has_dwell_override              = true;

  static PhoenixBenchmarkPotSweepRowMetrics rows[8] = {};

  const PhoenixBenchmarkPotSweepExecutionStatus status =
      phoenix_benchmark_pot_sweep_run(options, rows, sizeof(rows) / sizeof(rows[0]));

  TEST_ASSERT_FALSE(status.success);
  TEST_ASSERT_NOT_NULL(status.message);
  TEST_ASSERT_EQUAL_STRING("invalid arguments", status.message);
  TEST_ASSERT_EQUAL_UINT32(0u, status.rows_generated);
}

void setup() {
  // Step 1: Initialise Unity's serial logging channel.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2: Run the full Phoenix benchmark support test suite.
  UNITY_BEGIN();
  run_mcp356x_latency_lookup_tests();
  RUN_TEST(test_osr_latency_options_apply_defaults_inherit_initialised_values);
  RUN_TEST(test_osr_latency_options_validate_rejects_zero_sample_count);
  RUN_TEST(test_osr_latency_parse_command_accepts_plain_payload);
  RUN_TEST(test_osr_latency_parse_command_rejects_invalid_payload);
  RUN_TEST(test_osr_latency_format_summary_header_matches_expected_columns);
  RUN_TEST(test_osr_latency_format_summary_row_formats_metrics);
  RUN_TEST(test_osr_latency_format_summary_row_uses_placeholders_without_metrics);
  RUN_TEST(test_osr_latency_run_invokes_registered_samplers);
  RUN_TEST(test_running_stats_accumulates_integer_values);
  RUN_TEST(test_running_stats_handles_single_sample_std_zero);
  RUN_TEST(test_state_accumulator_tracks_channel_metrics);
  RUN_TEST(test_determine_dominant_channel_prefers_channel_a_when_delta_exceeds_drain);
  RUN_TEST(test_determine_dominant_channel_returns_unknown_when_deltas_similar);
  RUN_TEST(test_format_channel_alignment_label_formats_mismatch_note);
  RUN_TEST(test_parse_channel_map_command_extracts_parameters);
  RUN_TEST(test_parse_channel_map_command_treats_missing_dwell_as_default);
  RUN_TEST(test_parse_channel_map_command_rejects_wiper_override);
  RUN_TEST(test_parse_channel_map_command_rejects_invalid_payload);
  RUN_TEST(test_parse_channel_map_command_rejects_dwell_override);
  RUN_TEST(test_drift_capture_options_apply_defaults_inherit_initialised_values);
  RUN_TEST(test_drift_capture_options_validate_rejects_inverted_range);
  RUN_TEST(test_drift_capture_options_validate_rejects_schedule_exceeding_buffer);
  RUN_TEST(test_drift_capture_parse_command_accepts_plain_token);
  RUN_TEST(test_drift_capture_defaults_use_light_config);
  RUN_TEST(test_drift_capture_parse_command_accepts_json_overrides);
  RUN_TEST(test_drift_capture_parse_command_rejects_invalid_range);
  RUN_TEST(test_drift_capture_run_captures_leds_in_sequence);
  RUN_TEST(test_drift_capture_run_sets_saturation_warning);
  RUN_TEST(test_drift_capture_run_sets_overflow_warning);
  RUN_TEST(test_drift_capture_run_emits_nan_padding);
  RUN_TEST(test_is_adc_code_saturated_detects_full_scale_codes);
  RUN_TEST(test_adc_speed_format_summary_header_renders_expected_columns);
  RUN_TEST(test_adc_speed_format_summary_row_formats_metrics);
  RUN_TEST(test_adc_speed_format_summary_row_uses_placeholders_without_metrics);
  RUN_TEST(test_adc_speed_parse_command_line_accepts_json_payload);
  RUN_TEST(test_adc_speed_parse_command_line_rejects_invalid_duration);
  RUN_TEST(test_adc_speed_parse_command_uses_initialised_defaults);
  RUN_TEST(test_adc_speed_parse_command_overrides_defaults);
  RUN_TEST(test_adc_speed_run_collects_blocking_metrics);
  RUN_TEST(test_adc_speed_run_collects_dual_mode_metrics);
  RUN_TEST(test_osr_sweep_parse_command_applies_initialised_defaults);
  RUN_TEST(test_osr_sweep_parse_command_accepts_overrides);
  RUN_TEST(test_osr_sweep_run_iterates_all_osr_values);
  RUN_TEST(test_osr_sweep_format_summary_header);
  RUN_TEST(test_osr_sweep_format_summary_row_formats_metrics);
  RUN_TEST(test_osr_sweep_format_summary_row_uses_placeholders_without_metrics);
  RUN_TEST(test_dwell_sweep_parse_command_applies_defaults);
  RUN_TEST(test_dwell_sweep_parse_command_accepts_plain_command);
  RUN_TEST(test_dwell_sweep_parse_command_rejects_invalid_ranges);
  RUN_TEST(test_dwell_sweep_options_apply_defaults_inherit_initialised_values);
  RUN_TEST(test_dwell_sweep_options_validate_accepts_nominal_configuration);
  RUN_TEST(test_dwell_sweep_run_rejects_null_row_buffer);
  RUN_TEST(test_dwell_sweep_run_uses_light_readings_saturation_reporting);
  RUN_TEST(test_pot_sweep_parse_command_applies_defaults);
  RUN_TEST(test_pot_sweep_parse_command_accepts_plain_token);
  RUN_TEST(test_pot_sweep_parse_command_accepts_overrides);
  RUN_TEST(test_pot_sweep_parse_command_rejects_wiper_parameters);
  RUN_TEST(test_pot_sweep_run_rejects_insufficient_row_capacity);
  RUN_TEST(test_cold_sweep_run_populates_samples_and_statistics);
  RUN_TEST(test_cold_sweep_run_reports_saturation_warning);
  RUN_TEST(test_cold_sweep_run_reports_error_when_hardware_not_ready);
  RUN_TEST(test_cold_sweep_parse_command_accepts_plain_token);
  RUN_TEST(test_cold_sweep_parse_command_accepts_minimal_json);
  RUN_TEST(test_cold_sweep_parse_command_rejects_parameters);
  RUN_TEST(test_cold_sweep_format_summary_header_matches_expected_columns);
  RUN_TEST(test_cold_sweep_format_summary_row_formats_metrics);
  RUN_TEST(test_cold_sweep_format_summary_row_uses_placeholders_when_samples_missing);
  RUN_TEST(test_cold_sweep_format_sample_header_matches_expected_columns);
  RUN_TEST(test_cold_sweep_format_sample_row_formats_codes_and_mask);
  RUN_TEST(test_osr_sweep_run_reports_light_readings_warnings);
  RUN_TEST(test_pot_sweep_run_reports_light_readings_saturation);
  // Step 3: Finalise Unity before idling in loop().
  UNITY_END();
}

void loop() {
}
