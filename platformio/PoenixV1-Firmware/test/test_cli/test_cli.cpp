#include "cli.hpp"
#include "device_setup.hpp"
#include "ph_equations.hpp"
#include "unity_config.h"
#include <Arduino.h>
#include <unity.h>

namespace {

class RecordingPrint : public Print {
 public:
  RecordingPrint() {
    reset();
  }

  void reset(void) {
    buffer_       = "";
    current_line_ = "";
    last_line_    = "";
  }

  String buffer(void) const {
    return buffer_;
  }

  String last_line(void) const {
    return last_line_;
  }

 protected:
  size_t write(uint8_t character) override {
    const char ch = static_cast<char>(character);
    buffer_ += ch;
    if (ch == '\n') {
      last_line_    = current_line_;
      current_line_ = "";
    }
    else if (ch != '\r') {
      current_line_ += ch;
    }
    return 1u;
  }

 private:
  String buffer_;
  String current_line_;
  String last_line_;
};

LightReadingsStatisticSummary make_summary(uint32_t sample_count, double mean, double standard_deviation,
                                           int32_t min_value, int32_t max_value, double drift_slope) {
  LightReadingsStatisticSummary summary = {};
  summary.sample_count                  = sample_count;
  summary.mean                          = mean;
  summary.standard_deviation            = standard_deviation;
  summary.min_value                     = min_value;
  summary.max_value                     = max_value;
  summary.drift_slope                   = drift_slope;
  summary.has_samples                   = true;
  return summary;
}

LightReadingsSweepStats make_default_baseline_stats(void) {
  LightReadingsSweepStats stats = {};
  stats.sweep_count             = 500u;
  stats.drain_blue              = make_summary(4u, 1.25, 0.5, -2, 3, 0.1);
  stats.drain_green             = make_summary(4u, 2.5, 0.75, -1, 4, 0.2);
  stats.blue                    = make_summary(4u, 3.75, 1.25, 0, 6, 0.3);
  stats.green                   = make_summary(4u, 4.5, 1.5, 1, 7, 0.4);
  return stats;
}

LightReadingsSweepStats make_default_sample_stats(void) {
  LightReadingsSweepStats stats = make_default_baseline_stats();
  stats.drain_blue.mean         = 1.0;
  stats.blue.mean               = 2.0;
  stats.drain_green.mean        = 2.0;
  stats.green.mean              = 3.25;
  return stats;
}

void configure_channel_means(LightReadingsSweepStats* stats, double drain_blue_mean, double blue_mean,
                             double drain_green_mean, double green_mean) {
  if (stats == NULL) {
    return;
  }
  stats->drain_blue.mean  = drain_blue_mean;
  stats->blue.mean        = blue_mean;
  stats->drain_green.mean = drain_green_mean;
  stats->green.mean       = green_mean;
}

// Helper for parsing tab-delimited output; reserved for future test expansion.
__attribute__((unused)) size_t split_tabs(const String& line, String* tokens, size_t max_tokens) {
  size_t    token_count = 0u;
  int       start_index = 0;
  const int length      = line.length();
  for (int index = 0; index <= length; ++index) {
    const bool at_end   = (index == length);
    const bool at_delim = (!at_end && (line[index] == '\t'));
    if (at_end || at_delim) {
      if (token_count < max_tokens) {
        tokens[token_count] = line.substring(start_index, index);
        ++token_count;
      }
      start_index = index + 1;
    }
  }
  return token_count;
}

}  // namespace

static RecordingPrint          g_recording_print;
static uint32_t                g_last_sweep_requested         = 0u;
static LightReadingsSweepStats g_stub_baseline_stats_template = make_default_baseline_stats();
static LightReadingsSweepStats g_stub_sample_stats_template   = make_default_sample_stats();
static bool                    g_stub_use_sample_stats_next   = false;
static float                   g_stub_board_temperature_c     = 25.0f;
static float                   g_stub_water_temperature_c     = 24.0f;

static void reset_stub_stats_templates(void) {
  g_stub_baseline_stats_template = make_default_baseline_stats();
  g_stub_sample_stats_template   = make_default_sample_stats();
}

static void stage_sample_stats(void) {
  g_stub_use_sample_stats_next = true;
}

static void set_baseline_means(double drain_blue_mean, double blue_mean, double drain_green_mean, double green_mean) {
  configure_channel_means(&g_stub_baseline_stats_template, drain_blue_mean, blue_mean, drain_green_mean, green_mean);
}

static void set_sample_means(double drain_blue_mean, double blue_mean, double drain_green_mean, double green_mean) {
  configure_channel_means(&g_stub_sample_stats_template, drain_blue_mean, blue_mean, drain_green_mean, green_mean);
}

static void set_default_temperatures(void) {
  g_stub_board_temperature_c = 25.0f;
  g_stub_water_temperature_c = 24.0f;
}

static int stub_sweep_success(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  g_last_sweep_requested = sweep_count;
  if (results_out != NULL) {
    results_out->sweep_count = sweep_count;
  }
  return LIGHT_READINGS_OK;
}

static int stub_sweep_error(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  g_last_sweep_requested = sweep_count;
  if (results_out != NULL) {
    results_out->sweep_count = sweep_count;
  }
  return LIGHT_READINGS_ERR_INVALID_ARG;
}

static int stub_compute_success(const LightReadingsSweepCollection* sweep_collection,
                                LightReadingsSweepStats*            stats_out) {
  if ((sweep_collection == NULL) || (stats_out == NULL)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  const LightReadingsSweepStats& template_ref =
      g_stub_use_sample_stats_next ? g_stub_sample_stats_template : g_stub_baseline_stats_template;

  *stats_out                   = template_ref;
  stats_out->sweep_count       = sweep_collection->sweep_count;
  g_stub_use_sample_stats_next = false;

  return LIGHT_READINGS_OK;
}

static int stub_measure_temperature_success(ThermistorId id, float* temperature_c_out) {
  if (temperature_c_out == NULL) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }

  switch (id) {
    case ThermistorId::THERMISTOR_ID_GAIN_STAGE:
      *temperature_c_out = g_stub_board_temperature_c;
      return THERMISTOR_READER_OK;
    case ThermistorId::THERMISTOR_ID_SAMPLE:
      *temperature_c_out = g_stub_water_temperature_c;
      return THERMISTOR_READER_OK;
    default:
      *temperature_c_out = 0.0f;
      return THERMISTOR_READER_ERR_INVALID_ARG;
  }
}

static int stub_measure_temperature_error(ThermistorId id, float* temperature_c_out) {
  if (temperature_c_out == NULL) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }

  if (id == ThermistorId::THERMISTOR_ID_GAIN_STAGE) {
    *temperature_c_out = g_stub_board_temperature_c;
    return THERMISTOR_READER_OK;
  }

  *temperature_c_out = 0.0f;
  return THERMISTOR_READER_ERR_NOT_INITIALIZED;
}

static const CliMeasurementHooks k_stub_hooks_success           = {stub_sweep_success, stub_compute_success,
                                                                   stub_measure_temperature_success};
static const CliMeasurementHooks k_stub_hooks_error             = {stub_sweep_error, stub_compute_success,
                                                                   stub_measure_temperature_success};
static const CliMeasurementHooks k_stub_hooks_temperature_error = {stub_sweep_success, stub_compute_success,
                                                                   stub_measure_temperature_error};

void setUp(void) {
  cli_initialize();
  cli_test_set_measurement_hooks(&k_stub_hooks_success);
  cli_test_set_output(&g_recording_print);
  g_recording_print.reset();
  g_last_sweep_requested = 0u;
  reset_stub_stats_templates();
  g_stub_use_sample_stats_next = false;
  set_default_temperatures();
}

void tearDown(void) {
  cli_test_set_measurement_hooks(NULL);
  cli_test_set_output(NULL);
}

static void test_cli_dispatch_rejects_empty_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_EMPTY_COMMAND, (int) cli_dispatch_command(""));
}

static void test_cli_dispatch_rejects_unknown_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_UNKNOWN_COMMAND, (int) cli_dispatch_command("unknown"));
}

static void test_cli_dispatch_accepts_baseline_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
}

static void test_cli_dispatch_accepts_help_command(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("help"));
}

static void test_cli_dispatch_accepts_version_command(void) {
  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("v"));
  // Version command now outputs multiple lines including settings, so check buffer contains version.
  String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("phoenix-cli") >= 0);
}

static void test_cli_baseline_command_sets_cached_flag(void) {
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());
}

static void test_cli_baseline_command_caches_stats_and_sweep_count(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());

  LightReadingsSweepStats cached_stats = {};
  cli_test_get_baseline_stats(&cached_stats);

  TEST_ASSERT_EQUAL_UINT(500u, g_last_sweep_requested);
  TEST_ASSERT_EQUAL_UINT(500u, cached_stats.sweep_count);
  TEST_ASSERT_EQUAL_UINT(4u, cached_stats.drain_blue.sample_count);
  TEST_ASSERT_EQUAL_UINT(4u, cached_stats.drain_green.sample_count);
  TEST_ASSERT_EQUAL_UINT(4u, cached_stats.blue.sample_count);
  TEST_ASSERT_EQUAL_UINT(4u, cached_stats.green.sample_count);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.1, cached_stats.drain_blue.drift_slope);
}

static void test_cli_sample_without_baseline_reports_missing(void) {
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
}

static void test_cli_sample_after_baseline_uses_cached_flag(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());
  stage_sample_stats();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());
}

static void test_cli_sample_command_emits_stats_and_ph_after_baseline(void) {
  set_baseline_means(10.0, 110.0, 12.0, 212.0);
  set_sample_means(11.0, 61.0, 13.0, 93.0);
  g_stub_board_temperature_c = 26.5f;
  g_stub_water_temperature_c = 24.25f;

  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  stage_sample_stats();

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));

  // Step 1: Verify the output contains the expected status messages and table headers.
  String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("Taking sample...") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("temp_sample") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("abs_blue") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("pH") >= 0);

  // Step 2: Compute expected values for comparison.
  const double reference_blue =
      g_stub_baseline_stats_template.blue.mean - g_stub_baseline_stats_template.drain_blue.mean;
  const double reference_green =
      g_stub_baseline_stats_template.green.mean - g_stub_baseline_stats_template.drain_green.mean;
  const double sample_blue  = g_stub_sample_stats_template.blue.mean - g_stub_sample_stats_template.drain_blue.mean;
  const double sample_green = g_stub_sample_stats_template.green.mean - g_stub_sample_stats_template.drain_green.mean;

  double expected_absorbance_blue  = 0.0;
  double expected_absorbance_green = 0.0;
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK,
                        ph_equations_calc_absorbance(reference_blue, sample_blue, &expected_absorbance_blue));
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK,
                        ph_equations_calc_absorbance(reference_green, sample_green, &expected_absorbance_green));

  double expected_r_ratio = 0.0;
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK, ph_equations_calc_r_ratio(expected_absorbance_green, expected_absorbance_blue,
                                                                   &expected_r_ratio));

  constexpr double k_test_default_salinity_psu = 35.0;
  double           expected_ph                 = 0.0;
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK,
                        ph_equations_compute_ph(expected_r_ratio, static_cast<double>(g_stub_water_temperature_c),
                                                k_test_default_salinity_psu, &expected_ph));

  // Step 3: Verify the last line contains the pH value (tabular format: values row).
  // The last line is the data row with whitespace-separated values ending with pH.
  String last_line = g_recording_print.last_line();
  last_line.trim();
  // The pH value is the last whitespace-delimited field in the result row.
  int last_space = last_line.lastIndexOf(' ');
  TEST_ASSERT_TRUE(last_space > 0);
  String ph_field = last_line.substring(last_space + 1);
  ph_field.trim();
  float parsed_ph = ph_field.toFloat();
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, static_cast<float>(expected_ph), parsed_ph);
}

static void test_cli_sample_reports_temperature_error_when_measurement_fails(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  stage_sample_stats();
  cli_test_set_measurement_hooks(&k_stub_hooks_temperature_error);

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));

  String sample_line = g_recording_print.last_line();
  sample_line.trim();
  TEST_ASSERT_TRUE(sample_line.startsWith("error\ttemperature_water\t"));
}

static void test_cli_baseline_failure_does_not_set_cache(void) {
  cli_test_set_measurement_hooks(&k_stub_hooks_error);

  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
}

// Test that the calibrate command is recognized and dispatched successfully.
static void test_cli_dispatch_accepts_calibrate_command(void) {
  // Note: This test verifies dispatch acceptance. The actual calibration uses real hardware
  // via light_calibration module, so we only test that the command is recognized.
  // The calibration may fail due to uninitialized hardware, but dispatch should return OK.
  const CliDispatchResult result = cli_dispatch_command("c");
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) result);
}

// NOTE: The following calibration behavior tests are commented out because they require
// fully initialized hardware (SPI, ADC, LEDs, digipot). The calibration module doesn't
// use CLI measurement hooks - it directly accesses light_readings hardware.
// These tests should be run as integration tests with full device_setup() initialization.
//
// static void test_cli_calibrate_clears_baseline_cache(void)
// static void test_cli_calibrate_outputs_recommended_wipers(void)

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  UNITY_BEGIN();

  cli_initialize();

  RUN_TEST(test_cli_dispatch_rejects_empty_command);
  RUN_TEST(test_cli_dispatch_rejects_unknown_command);
  RUN_TEST(test_cli_dispatch_accepts_baseline_command);
  RUN_TEST(test_cli_dispatch_accepts_help_command);
  RUN_TEST(test_cli_dispatch_accepts_version_command);
  RUN_TEST(test_cli_baseline_command_sets_cached_flag);
  RUN_TEST(test_cli_baseline_command_caches_stats_and_sweep_count);
  RUN_TEST(test_cli_sample_without_baseline_reports_missing);
  RUN_TEST(test_cli_sample_after_baseline_uses_cached_flag);
  RUN_TEST(test_cli_sample_command_emits_stats_and_ph_after_baseline);
  RUN_TEST(test_cli_sample_reports_temperature_error_when_measurement_fails);
  RUN_TEST(test_cli_baseline_failure_does_not_set_cache);
  RUN_TEST(test_cli_dispatch_accepts_calibrate_command);
  // NOTE: test_cli_calibrate_clears_baseline_cache and test_cli_calibrate_outputs_recommended_wipers
  // are integration tests that require full hardware initialization. Run them separately.

  UNITY_END();
}

void loop() {
}
