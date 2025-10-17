#include "adc_speed/adc_speed.hpp"
#include "adc_speed/adc_speed_command_parser.hpp"
#include "adc_speed/adc_speed_formatter.hpp"
#include "channel_map/channel_map_formatter.hpp"
#include "channel_map/channel_map_support.hpp"
#include "core/phoenix_benchmark_core.hpp"
#include "main.hpp"
#include "osr_sweep/osr_sweep.hpp"
#include "osr_sweep/osr_sweep_formatter.hpp"
#include "pot_sweep/pot_sweep.hpp"
#include "pot_sweep/pot_sweep_formatter.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <cstring>
#include <unity.h>

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

  // Step 2: Populate a LED1 accumulator where channel A clearly dominates.
  PhoenixBenchmarkStateAccumulator led1_accumulator;
  led1_accumulator.channel_a_codes.update(500);
  led1_accumulator.channel_a_codes.update(520);
  led1_accumulator.channel_b_codes.update(110);
  led1_accumulator.channel_b_codes.update(120);

  // Step 3: Verify the helper selects channel A when the delta exceeds the drain threshold.
  const PhoenixBenchmarkChannel channel =
      phoenix_benchmark_channel_map_determine_dominant_channel(drain_accumulator, led1_accumulator, 5.0);
  TEST_ASSERT_EQUAL(PhoenixBenchmarkChannel::kChannelA, channel);
}

static void test_determine_dominant_channel_returns_unknown_when_deltas_similar(void) {
  // Step 1: Populate the drain and LED accumulators with similar channel deltas.
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

  // Step 2: Expect the helper to classify the result as unknown because the deltas align.
  const PhoenixBenchmarkChannel channel =
      phoenix_benchmark_channel_map_determine_dominant_channel(drain_accumulator, led2_accumulator, 5.0);
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
  // Step 1: Provide a JSON command with explicit dwell and wiper overrides.
  const char* command_line =
      "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":25,\"dwell_us\":75,\"wiper_code\":120}}";
  PhoenixBenchmarkChannelMapRequest request = {};
  // Step 2: Parse the command and validate each captured field.
  TEST_ASSERT_TRUE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
  TEST_ASSERT_EQUAL_UINT32(25u, request.sweep_count);
  TEST_ASSERT_TRUE(request.has_dwell_override);
  TEST_ASSERT_EQUAL_UINT32(75u, request.dwell_us);
  TEST_ASSERT_TRUE(request.has_wiper_override);
  TEST_ASSERT_EQUAL_UINT8(120u, request.wiper_code);
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

static void test_parse_channel_map_command_rejects_out_of_range_wiper(void) {
  // Step 1: Attempt to parse a command whose wiper code exceeds the allowed range.
  const char* command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":5,\"wiper_code\":300}}";
  PhoenixBenchmarkChannelMapRequest request = {};
  // Step 2: Expect the helper to reject the payload.
  TEST_ASSERT_FALSE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
}

static void test_parse_channel_map_command_rejects_invalid_payload(void) {
  // Step 1: Provide a payload that violates documented sweep limits.
  const char*                       command_line = "{\"command\":\"channel_map\",\"parameters\":{\"sweeps\":0}}";
  PhoenixBenchmarkChannelMapRequest request      = {};
  // Step 2: Confirm the parser reports failure when validation fails.
  TEST_ASSERT_FALSE(phoenix_benchmark_channel_map_parse_command(command_line, &request));
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

namespace {

constexpr std::size_t k_expected_osr_value_count = k_phoenix_benchmark_osr_value_count;

PhoenixBenchmarkChannelMapExecutionStatus g_fake_osr_runner_status = {
    .success = true,
};

static std::size_t g_fake_runner_call_count = 0u;
static std::size_t g_fake_set_osr_count     = 0u;
static uint32_t    g_fake_micros_now        = 0u;

static PhoenixBenchmarkChannelMapExecutionStatus fake_osr_channel_map_runner(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks&) {
  TEST_ASSERT_NOT_NULL(accumulators);
  TEST_ASSERT_EQUAL_UINT32(100u, options.sweep_count);
  TEST_ASSERT_EQUAL_UINT32(50u, options.dwell_us);
  TEST_ASSERT_EQUAL_UINT8(0x33u, options.wiper_code);

  for (std::size_t index = 0u; index < 3u; ++index) {
    TEST_ASSERT_EQUAL_UINT32(0u, accumulators[index].channel_a_codes.count);
    TEST_ASSERT_EQUAL_UINT32(0u, accumulators[index].channel_b_codes.count);
  }

  const uint32_t base_value = static_cast<uint32_t>((g_fake_runner_call_count + 1u) * 100u);

  auto seed_accumulator = [base_value](PhoenixBenchmarkStateAccumulator& accumulator, uint32_t offset) {
    accumulator.channel_a_codes.count     = 2u;
    accumulator.channel_a_codes.mean      = static_cast<double>(base_value + offset);
    accumulator.channel_a_codes.m2        = 25.0;
    accumulator.channel_a_codes.min_value = static_cast<int32_t>(base_value + offset - 5u);
    accumulator.channel_a_codes.max_value = static_cast<int32_t>(base_value + offset + 5u);

    accumulator.channel_b_codes.count     = 2u;
    accumulator.channel_b_codes.mean      = static_cast<double>(base_value + offset + 10u);
    accumulator.channel_b_codes.m2        = 36.0;
    accumulator.channel_b_codes.min_value = static_cast<int32_t>(base_value + offset + 2u);
    accumulator.channel_b_codes.max_value = static_cast<int32_t>(base_value + offset + 12u);
  };

  seed_accumulator(accumulators[0], 1u);
  seed_accumulator(accumulators[1], 11u);
  seed_accumulator(accumulators[2], 21u);

  g_fake_micros_now += static_cast<uint32_t>(75u + g_fake_runner_call_count);

  ++g_fake_runner_call_count;
  return g_fake_osr_runner_status;
}

static int fake_set_osr(mcp356x_osr) {
  ++g_fake_set_osr_count;
  return MCP356X_OK;
}

static uint32_t fake_micros(void) {
  return g_fake_micros_now;
}

}  // namespace

static void test_osr_sweep_run_iterates_all_osr_values(void) {
  // Step 1: Install deterministic hooks for the firmware runner.
  phoenix_benchmark_osr_sweep_reset_state();
  phoenix_benchmark_osr_sweep_set_channel_map_runner_for_test(fake_osr_channel_map_runner);
  phoenix_benchmark_osr_sweep_set_osr_setter_for_test(fake_set_osr);
  phoenix_benchmark_osr_sweep_set_micros_provider_for_test(fake_micros);

  PhoenixBenchmarkOsrSweepOptions options = {};
  options.sweep_count                     = 100u;
  options.has_sweep_override              = true;
  options.dwell_us                        = 50u;
  options.has_dwell_override              = true;
  options.wiper_code                      = 0x33u;
  options.has_wiper_override              = true;

  static PhoenixBenchmarkOsrSweepRowMetrics rows[k_expected_osr_value_count] = {};
  g_fake_runner_call_count                                                   = 0u;
  g_fake_set_osr_count                                                       = 0u;
  g_fake_micros_now                                                          = 1000u;

  PhoenixBenchmarkOsrSweepExecutionStatus status =
      phoenix_benchmark_osr_sweep_run(options, rows, k_expected_osr_value_count);

  // Step 2: Confirm the sweep succeeded and exercised every OSR value.
  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_EQUAL_UINT32(k_expected_osr_value_count, status.rows_generated);
  TEST_ASSERT_EQUAL_UINT32(k_expected_osr_value_count, g_fake_runner_call_count);
  TEST_ASSERT_EQUAL_UINT32(k_expected_osr_value_count, g_fake_set_osr_count);

  // Step 3: Validate captured metrics and elapsed time.
  uint32_t expected_elapsed = 75u;
  for (std::size_t index = 0u; index < status.rows_generated; ++index) {
    const PhoenixBenchmarkOsrSweepRowMetrics& row = rows[index];
    TEST_ASSERT_EQUAL_UINT32(100u, row.sweep_count);
    TEST_ASSERT_TRUE(row.drain.channel_a_codes.has_samples());
    TEST_ASSERT_FLOAT_WITHIN(0.0001, static_cast<double>((index + 1u) * 100u + 1u), row.drain.channel_a_codes.mean);
    TEST_ASSERT_TRUE(row.elapsed_microseconds >= expected_elapsed);
    expected_elapsed += 1u;
  }

  // Step 4: Release test hooks to avoid influencing later tests.
  phoenix_benchmark_osr_sweep_clear_test_hooks();
}

static void test_osr_sweep_format_summary_header(void) {
  // Step 1: Render the summary header and confirm each column appears.
  char buffer[k_phoenix_benchmark_osr_sweep_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_osr_sweep_format_summary_header(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING(
      "Value      Samples  Drain_Mean  Drain_Std  Drain_Min  Drain_Max  LED1_Mean  LED1_Std  LED1_Min  LED1_Max  LED2_Mean  LED2_Std  LED2_Min  LED2_Max  Sweep_us",
      buffer);
}

static void test_osr_sweep_format_summary_row_formats_metrics(void) {
  // Step 1: Populate a row with metrics so alignment and precision can be exercised.
  PhoenixBenchmarkOsrSweepSummaryRowValues values = {};
  values.label                                    = "OSR512";
  values.sample_count                             = 100u;
  values.drain_mean                               = 123.456;
  values.drain_std                                = 0.25;
  values.drain_min                                = 120.0;
  values.drain_max                                = 128.0;
  values.led1_mean                                = 223.1;
  values.led1_std                                 = 0.75;
  values.led1_min                                 = 219.0;
  values.led1_max                                 = 229.0;
  values.led2_mean                                = 321.9;
  values.led2_std                                 = 0.80;
  values.led2_min                                 = 318.0;
  values.led2_max                                 = 326.0;
  values.sweep_duration_us                        = 12345u;
  values.has_metrics                              = true;

  char buffer[k_phoenix_benchmark_osr_sweep_summary_buffer_bytes] = {};
  TEST_ASSERT_TRUE(phoenix_benchmark_osr_sweep_format_summary_row(values, buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "OSR512"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "123.456"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "0.250"));
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
  // Step 1: Configure defaults with a constrained range to simplify validation.
  phoenix_benchmark_pot_sweep_reset_state();
  const PhoenixBenchmarkPotSweepDefaults defaults = {
      .sweeps_per_wiper = 5u,
      .wiper_start      = 0x10u,
      .wiper_end        = 0x14u,
      .wiper_step       = 0x02u,
  };
  phoenix_benchmark_pot_sweep_initialise(defaults);

  const PhoenixBenchmarkPotSweepParseResult result =
      phoenix_benchmark_pot_sweep_parse_command("{\"command\":\"pot_sweep\"}");

  // Step 2: Confirm defaults applied when no overrides are provided.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_EQUAL_UINT32(defaults.sweeps_per_wiper, result.options.sweeps_per_wiper);
  TEST_ASSERT_FALSE(result.options.has_sweeps_override);
  TEST_ASSERT_FALSE(result.options.has_wiper_list_override);
  TEST_ASSERT_EQUAL_UINT32(3u, static_cast<uint32_t>(result.options.wiper_count));
  TEST_ASSERT_EQUAL_UINT8(0x10u, result.options.wiper_codes[0]);
  TEST_ASSERT_EQUAL_UINT8(0x12u, result.options.wiper_codes[1]);
  TEST_ASSERT_EQUAL_UINT8(0x14u, result.options.wiper_codes[2]);
}

static void test_pot_sweep_parse_command_accepts_plain_token(void) {
  // Step 1: Seed constrained defaults so the plain command can be validated precisely.
  phoenix_benchmark_pot_sweep_reset_state();
  const PhoenixBenchmarkPotSweepDefaults defaults = {
      .sweeps_per_wiper = 7u,
      .wiper_start      = 0x20u,
      .wiper_end        = 0x24u,
      .wiper_step       = 0x02u,
  };
  phoenix_benchmark_pot_sweep_initialise(defaults);

  const PhoenixBenchmarkPotSweepParseResult result = phoenix_benchmark_pot_sweep_parse_command("pot_sweep");

  // Step 2: Ensure the parser accepts the plain token and applies defaults.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_NULL(result.error_message);
  TEST_ASSERT_EQUAL_UINT32(defaults.sweeps_per_wiper, result.options.sweeps_per_wiper);
  TEST_ASSERT_FALSE(result.options.has_sweeps_override);
  TEST_ASSERT_FALSE(result.options.has_wiper_list_override);
  TEST_ASSERT_EQUAL_UINT32(3u, static_cast<uint32_t>(result.options.wiper_count));
  TEST_ASSERT_EQUAL_UINT8(0x20u, result.options.wiper_codes[0]);
  TEST_ASSERT_EQUAL_UINT8(0x22u, result.options.wiper_codes[1]);
  TEST_ASSERT_EQUAL_UINT8(0x24u, result.options.wiper_codes[2]);
}

static void test_pot_sweep_parse_command_accepts_overrides(void) {
  // Step 1: Reset defaults and issue a command overriding sweeps and wiper list.
  phoenix_benchmark_pot_sweep_reset_state();
  phoenix_benchmark_pot_sweep_initialise(
      {.sweeps_per_wiper = 4u, .wiper_start = 0x00u, .wiper_end = 0xFFu, .wiper_step = 0x01u});

  const char* payload =
      "{\"command\":\"pot_sweep\",\"parameters\":{"
      "\"sweeps\":3,\"wipers\":[0,32,64,96]}}";

  const PhoenixBenchmarkPotSweepParseResult result = phoenix_benchmark_pot_sweep_parse_command(payload);

  // Step 2: Validate overrides captured correctly.
  TEST_ASSERT_TRUE(result.success);
  TEST_ASSERT_TRUE(result.options.has_sweeps_override);
  TEST_ASSERT_TRUE(result.options.has_wiper_list_override);
  TEST_ASSERT_EQUAL_UINT32(3u, result.options.sweeps_per_wiper);
  TEST_ASSERT_EQUAL_UINT32(4u, static_cast<uint32_t>(result.options.wiper_count));
  TEST_ASSERT_EQUAL_UINT8(0u, result.options.wiper_codes[0]);
  TEST_ASSERT_EQUAL_UINT8(32u, result.options.wiper_codes[1]);
  TEST_ASSERT_EQUAL_UINT8(64u, result.options.wiper_codes[2]);
  TEST_ASSERT_EQUAL_UINT8(96u, result.options.wiper_codes[3]);
}

namespace {

static std::size_t g_pot_sweep_runner_calls = 0u;
static uint8_t     g_pot_sweep_last_wiper   = 0u;

static PhoenixBenchmarkChannelMapExecutionStatus fake_pot_sweep_runner(
    const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators,
    const PhoenixBenchmarkChannelMapOutputCallbacks&) {
  TEST_ASSERT_NOT_NULL(accumulators);
  ++g_pot_sweep_runner_calls;
  g_pot_sweep_last_wiper = options.wiper_code;

  // Step 1: Seed deterministic maxima for LED1 (channel A) and LED2 (channel B).
  const int32_t led1_base = static_cast<int32_t>(options.wiper_code) * 1000;
  const int32_t led2_base = static_cast<int32_t>(options.wiper_code) * 1100;

  PhoenixBenchmarkStateAccumulator& drain = accumulators[0];
  PhoenixBenchmarkStateAccumulator& led1  = accumulators[1];
  PhoenixBenchmarkStateAccumulator& led2  = accumulators[2];

  drain.channel_a_codes.count     = 1u;
  drain.channel_a_codes.max_value = 100;
  drain.channel_b_codes.count     = 1u;
  drain.channel_b_codes.max_value = 90;

  led1.channel_a_codes.count     = 5u;
  led1.channel_a_codes.max_value = led1_base + 7000000;
  led1.channel_b_codes.count     = 5u;
  led1.channel_b_codes.max_value = 0;

  led2.channel_a_codes.count     = 5u;
  led2.channel_a_codes.max_value = 0;
  led2.channel_b_codes.count     = 5u;
  led2.channel_b_codes.max_value = led2_base + 7200000;

  if (options.wiper_code >= 0x20u) {
    led1.channel_a_codes.max_value = 7600000;
  }
  if (options.wiper_code >= 0x30u) {
    led2.channel_b_codes.max_value = 7800000;
  }

  return {true, PHOENIX_BENCHMARK_OK, nullptr, false};
}

}  // namespace

static void test_pot_sweep_run_collects_metrics_and_recommendations(void) {
  // Step 1: Prepare options with three wipers and deterministic runner hooks.
  phoenix_benchmark_pot_sweep_reset_state();
  phoenix_benchmark_pot_sweep_initialise(
      {.sweeps_per_wiper = 5u, .wiper_start = 0x00u, .wiper_end = 0x50u, .wiper_step = 0x10u});

  PhoenixBenchmarkPotSweepOptions options = {};
  options.has_wiper_list_override         = true;
  options.wiper_count                     = 3u;
  options.wiper_codes[0]                  = 0x00u;
  options.wiper_codes[1]                  = 0x20u;
  options.wiper_codes[2]                  = 0x30u;
  options.sweeps_per_wiper                = 5u;
  options.has_sweeps_override             = true;

  PhoenixBenchmarkPotSweepRowMetrics rows[4] = {};

#if defined(UNIT_TEST)
  phoenix_benchmark_pot_sweep_set_channel_map_runner_for_test(fake_pot_sweep_runner);
  phoenix_benchmark_pot_sweep_set_hardware_ready_checker_for_test([]() { return true; });
#endif

  g_pot_sweep_runner_calls = 0u;

  const PhoenixBenchmarkPotSweepExecutionStatus status = phoenix_benchmark_pot_sweep_run(options, rows, 4u);

#if defined(UNIT_TEST)
  phoenix_benchmark_pot_sweep_clear_test_hooks();
#endif

  // Step 2: Validate run status and row collection.
  TEST_ASSERT_TRUE(status.success);
  TEST_ASSERT_TRUE(status.has_warnings);  // saturation triggered for wipers >= 0x20
  TEST_ASSERT_EQUAL_UINT32(3u, status.rows_generated);
  TEST_ASSERT_EQUAL_UINT32(3u, g_pot_sweep_runner_calls);

  TEST_ASSERT_EQUAL_UINT8(0x00u, rows[0].wiper_code);
  TEST_ASSERT_EQUAL_INT32(7000000, rows[0].led1_max_code);
  TEST_ASSERT_FALSE(rows[0].led1_saturated);
  TEST_ASSERT_EQUAL_INT32(7200000, rows[0].led2_max_code);
  TEST_ASSERT_FALSE(rows[0].led2_saturated);

  TEST_ASSERT_TRUE(rows[1].led1_saturated);
  TEST_ASSERT_FALSE(rows[1].led2_saturated);
  TEST_ASSERT_TRUE(rows[2].led1_saturated);
  TEST_ASSERT_TRUE(rows[2].led2_saturated);

  TEST_ASSERT_TRUE(status.led1_recommendation_valid);
  TEST_ASSERT_EQUAL_UINT8(0x00u, status.led1_recommended_wiper);
  TEST_ASSERT_TRUE(status.led2_recommendation_valid);
  TEST_ASSERT_EQUAL_UINT8(0x20u, status.led2_recommended_wiper);
}

void setup() {
  // Step 1: Initialise Unity's serial logging channel.
  UNITY_SETUP_SERIAL_DEFAULT();
  // Step 2: Run the full Phoenix benchmark support test suite.
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
  RUN_TEST(test_pot_sweep_parse_command_applies_defaults);
  RUN_TEST(test_pot_sweep_parse_command_accepts_plain_token);
  RUN_TEST(test_pot_sweep_parse_command_accepts_overrides);
  RUN_TEST(test_pot_sweep_run_collects_metrics_and_recommendations);
  // Step 3: Finalise Unity before idling in loop().
  UNITY_END();
}

void loop() {
}
