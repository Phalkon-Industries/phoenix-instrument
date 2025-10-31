#include "adc_speed/adc_speed.hpp"
#include "adc_speed/adc_speed_command_parser.hpp"
#include "adc_speed/adc_speed_formatter.hpp"
#include "channel_map/channel_map_formatter.hpp"
#include "channel_map/channel_map_support.hpp"
#include "core/phoenix_benchmark_core.hpp"
#include "drift_capture/drift_capture.hpp"
#include "dwell_sweep/dwell_sweep.hpp"
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

static uint32_t                        g_drift_capture_fake_micros             = 0u;
static const int32_t*                  g_drift_capture_led1_codes              = nullptr;
static std::size_t                     g_drift_capture_led1_length             = 0u;
static std::size_t                     g_drift_capture_led1_index              = 0u;
static const int32_t*                  g_drift_capture_led2_codes              = nullptr;
static std::size_t                     g_drift_capture_led2_length             = 0u;
static std::size_t                     g_drift_capture_led2_index              = 0u;
static PhoenixBenchmarkDriftCaptureLed g_drift_capture_current_led             = PhoenixBenchmarkDriftCaptureLed::kLed1;
static bool                            g_drift_capture_led_active              = false;
static LedRouterState                  g_drift_capture_router_transitions[8]   = {};
static std::size_t                     g_drift_capture_router_transition_count = 0u;
static uint8_t                         g_drift_capture_last_wiper_code         = 0u;
static uint32_t                        g_drift_capture_last_osr_enum           = 0u;
static std::size_t                     g_drift_capture_osr_call_count          = 0u;
static AdcHalChannel                   g_drift_capture_last_channel            = AdcHalChannel::ADC_HAL_CHANNEL_4;
static char                            g_drift_capture_output_lines[32][128]   = {};
static std::size_t                     g_drift_capture_output_count            = 0u;

static void drift_capture_reset_fakes(void) {
  g_drift_capture_fake_micros             = 0u;
  g_drift_capture_led1_codes              = nullptr;
  g_drift_capture_led1_length             = 0u;
  g_drift_capture_led1_index              = 0u;
  g_drift_capture_led2_codes              = nullptr;
  g_drift_capture_led2_length             = 0u;
  g_drift_capture_led2_index              = 0u;
  g_drift_capture_current_led             = PhoenixBenchmarkDriftCaptureLed::kLed1;
  g_drift_capture_led_active              = false;
  g_drift_capture_router_transition_count = 0u;
  g_drift_capture_last_wiper_code         = 0u;
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

static bool drift_capture_fake_wiper_setter(uint8_t code) {
  g_drift_capture_last_wiper_code = code;
  return true;
}

static int drift_capture_fake_led_setter(LedRouterState state) {
  if (g_drift_capture_router_transition_count <
      (sizeof(g_drift_capture_router_transitions) / sizeof(g_drift_capture_router_transitions[0]))) {
    g_drift_capture_router_transitions[g_drift_capture_router_transition_count++] = state;
  }
  if (state == LedRouterState::LED_ROUTER_STATE_LED1) {
    g_drift_capture_current_led = PhoenixBenchmarkDriftCaptureLed::kLed1;
    g_drift_capture_led_active  = true;
  }
  else if (state == LedRouterState::LED_ROUTER_STATE_LED2) {
    g_drift_capture_current_led = PhoenixBenchmarkDriftCaptureLed::kLed2;
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
  if (g_drift_capture_current_led == PhoenixBenchmarkDriftCaptureLed::kLed1) {
    if (g_drift_capture_led1_index >= g_drift_capture_led1_length) {
      return false;
    }
    *out_code = g_drift_capture_led1_codes[g_drift_capture_led1_index++];
  }
  else {
    if (g_drift_capture_led2_index >= g_drift_capture_led2_length) {
      return false;
    }
    *out_code = g_drift_capture_led2_codes[g_drift_capture_led2_index++];
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
      .start_time_us = 0u, .end_time_us = 100000u, .step_delay_us = 10u, .osr = 4096u, .wiper_code = 0x20u};

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
  TEST_ASSERT_EQUAL_UINT8(defaults.wiper_code, options.wiper_code);
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
      .wiper_code         = 0x10u,
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
      .wiper_code         = 0x01u,
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
      .start_time_us = 0u, .end_time_us = 100000u, .step_delay_us = 0u, .osr = 4096u, .wiper_code = 0x00u};
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
  TEST_ASSERT_EQUAL_UINT8(defaults.wiper_code, result.options.wiper_code);
}

static void test_drift_capture_parse_command_accepts_json_overrides(void) {
  // Step 1: Provide explicit overrides for timing, OSR, and the wiper code.
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise(
      {.start_time_us = 0u, .end_time_us = 100000u, .step_delay_us = 0u, .osr = 4096u, .wiper_code = 0x00u});

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
  TEST_ASSERT_EQUAL_UINT8(64u, result.options.wiper_code);
}

static void test_drift_capture_parse_command_rejects_invalid_range(void) {
  // Step 1: Attempt to parse a JSON payload where the capture window is inverted.
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise(
      {.start_time_us = 0u, .end_time_us = 100000u, .step_delay_us = 0u, .osr = 4096u, .wiper_code = 0x00u});

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
      .start_time_us = 0u, .end_time_us = 20u, .step_delay_us = 10u, .osr = 4096u, .wiper_code = 0x12u};
  phoenix_benchmark_drift_capture_initialise(defaults);
  drift_capture_reset_fakes();

  static const int32_t led1_codes[] = {100, 110, 120};
  static const int32_t led2_codes[] = {200, 210, 220};
  g_drift_capture_led1_codes        = led1_codes;
  g_drift_capture_led1_length       = sizeof(led1_codes) / sizeof(led1_codes[0]);
  g_drift_capture_led2_codes        = led2_codes;
  g_drift_capture_led2_length       = sizeof(led2_codes) / sizeof(led2_codes[0]);

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
  TEST_ASSERT_EQUAL_UINT8(defaults.wiper_code, status.applied_wiper_code);
  TEST_ASSERT_EQUAL_UINT32(defaults.start_time_us, status.applied_start_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.end_time_us, status.applied_end_us);
  TEST_ASSERT_EQUAL_UINT32(defaults.step_delay_us, status.applied_step_us);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g_drift_capture_led1_length),
                           static_cast<uint32_t>(status.led1_samples));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g_drift_capture_led2_length),
                           static_cast<uint32_t>(status.led2_samples));
  TEST_ASSERT_EQUAL_UINT8(defaults.wiper_code, g_drift_capture_last_wiper_code);
  TEST_ASSERT_TRUE(g_drift_capture_osr_call_count >= 1u);

  std::size_t                                     led1_count = 0u;
  const PhoenixBenchmarkDriftCaptureSample* const led1_samples =
      phoenix_benchmark_drift_capture_led_samples(PhoenixBenchmarkDriftCaptureLed::kLed1, &led1_count);
  TEST_ASSERT_NOT_NULL(led1_samples);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g_drift_capture_led1_length), static_cast<uint32_t>(led1_count));
  TEST_ASSERT_EQUAL_UINT32(0u, led1_samples[0].elapsed_microseconds);
  TEST_ASSERT_EQUAL_INT32(led1_codes[0], led1_samples[0].adc_code);
  TEST_ASSERT_EQUAL_UINT32(10u, led1_samples[1].elapsed_microseconds);
  TEST_ASSERT_EQUAL_INT32(led1_codes[1], led1_samples[1].adc_code);
  TEST_ASSERT_EQUAL_UINT32(20u, led1_samples[2].elapsed_microseconds);
  TEST_ASSERT_EQUAL_INT32(led1_codes[2], led1_samples[2].adc_code);

  std::size_t                                     led2_count = 0u;
  const PhoenixBenchmarkDriftCaptureSample* const led2_samples =
      phoenix_benchmark_drift_capture_led_samples(PhoenixBenchmarkDriftCaptureLed::kLed2, &led2_count);
  TEST_ASSERT_NOT_NULL(led2_samples);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g_drift_capture_led2_length), static_cast<uint32_t>(led2_count));
  TEST_ASSERT_EQUAL_UINT32(0u, led2_samples[0].elapsed_microseconds);
  TEST_ASSERT_EQUAL_INT32(led2_codes[0], led2_samples[0].adc_code);

  TEST_ASSERT_TRUE(g_drift_capture_router_transition_count >= 3u);
  TEST_ASSERT_EQUAL(LedRouterState::LED_ROUTER_STATE_LED1, g_drift_capture_router_transitions[0]);
  TEST_ASSERT_EQUAL(LedRouterState::LED_ROUTER_STATE_DRAIN, g_drift_capture_router_transitions[1]);
  TEST_ASSERT_EQUAL(LedRouterState::LED_ROUTER_STATE_LED2, g_drift_capture_router_transitions[2]);
}

static void test_drift_capture_run_sets_saturation_warning(void) {
  // Step 1: Feed a saturated sample to confirm warning propagation.
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise(
      {.start_time_us = 0u, .end_time_us = 0u, .step_delay_us = 0u, .osr = 4096u, .wiper_code = 0x00u});
  drift_capture_reset_fakes();

  static const int32_t led1_codes[] = {k_phoenix_benchmark_adc_positive_full_scale_code};
  static const int32_t led2_codes[] = {0};
  g_drift_capture_led1_codes        = led1_codes;
  g_drift_capture_led1_length       = 1u;
  g_drift_capture_led2_codes        = led2_codes;
  g_drift_capture_led2_length       = 1u;

  drift_capture_install_fakes();

  PhoenixBenchmarkDriftCaptureOptions options = {};
  options.apply_defaults(
      {.start_time_us = 0u, .end_time_us = 0u, .step_delay_us = 0u, .osr = 4096u, .wiper_code = 0x00u});
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
  phoenix_benchmark_drift_capture_initialise(
      {.start_time_us = 0u, .end_time_us = 500000u, .step_delay_us = 0u, .osr = 4096u, .wiper_code = 0x00u});
  drift_capture_reset_fakes();

  static int32_t led_codes[k_phoenix_benchmark_drift_capture_max_sample_count + 8u];
  for (std::size_t index = 0u; index < (sizeof(led_codes) / sizeof(led_codes[0])); ++index) {
    led_codes[index] = static_cast<int32_t>(index);
  }
  g_drift_capture_led1_codes  = led_codes;
  g_drift_capture_led1_length = sizeof(led_codes) / sizeof(led_codes[0]);
  g_drift_capture_led2_codes  = led_codes;
  g_drift_capture_led2_length = sizeof(led_codes) / sizeof(led_codes[0]);

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
                           static_cast<uint32_t>(status.led1_samples));
}

static void test_drift_capture_run_emits_nan_padding(void) {
  // Step 1: Capture mismatched LED lengths and confirm missing entries are padded.
  phoenix_benchmark_drift_capture_reset_state();
  phoenix_benchmark_drift_capture_initialise(
      {.start_time_us = 0u, .end_time_us = 10u, .step_delay_us = 10u, .osr = 4096u, .wiper_code = 0x00u});
  drift_capture_reset_fakes();

  static const int32_t led1_codes[] = {10, 20};
  static const int32_t led2_codes[] = {30};
  g_drift_capture_led1_codes        = led1_codes;
  g_drift_capture_led1_length       = sizeof(led1_codes) / sizeof(led1_codes[0]);
  g_drift_capture_led2_codes        = led2_codes;
  g_drift_capture_led2_length       = sizeof(led2_codes) / sizeof(led2_codes[0]);

  drift_capture_install_fakes();

  PhoenixBenchmarkDriftCaptureOptions options = {};
  options.apply_defaults(
      {.start_time_us = 0u, .end_time_us = 10u, .step_delay_us = 10u, .osr = 4096u, .wiper_code = 0x00u});
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
  RUN_TEST(test_pot_sweep_parse_command_applies_defaults);
  RUN_TEST(test_pot_sweep_parse_command_accepts_plain_token);
  RUN_TEST(test_pot_sweep_parse_command_accepts_overrides);
  RUN_TEST(test_pot_sweep_parse_command_rejects_wiper_parameters);
  RUN_TEST(test_pot_sweep_run_rejects_insufficient_row_capacity);
  // Step 3: Finalise Unity before idling in loop().
  UNITY_END();
}

void loop() {
}
