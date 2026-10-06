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

// Splits a delimited output line (tab or CSV) into tokens; returns the total field count even past max_tokens.
size_t split_fields(const String& line, char delimiter, String* tokens, size_t max_tokens) {
  size_t    token_count = 0u;
  int       start_index = 0;
  const int length      = line.length();
  for (int index = 0; index <= length; ++index) {
    const bool at_end   = (index == length);
    const bool at_delim = (!at_end && (line[index] == delimiter));
    if (at_end || at_delim) {
      if (token_count < max_tokens) {
        tokens[token_count] = line.substring(start_index, index);
      }
      ++token_count;
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
static ThermistorSweepResult   g_stub_temperatures            = {};

constexpr size_t k_test_sample_index = static_cast<size_t>(ThermistorId::THERMISTOR_ID_SAMPLE);

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
  // Distinct value per thermistor (ThermistorId order) so tests can verify column placement.
  const float k_default_temperatures_c[] = {24.0f, 31.5f, 30.25f, 25.0f, 27.75f};
  g_stub_temperatures                    = {};
  for (size_t index = 0u; index < 5u; ++index) {
    g_stub_temperatures.temperatures_c[index] = k_default_temperatures_c[index];
    g_stub_temperatures.valid[index]          = true;
  }
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

static int stub_measure_all_temperatures_success(ThermistorSweepResult* result_out) {
  if (result_out == NULL) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }

  *result_out = g_stub_temperatures;
  return THERMISTOR_READER_OK;
}

static int stub_measure_all_temperatures_error(ThermistorSweepResult* result_out) {
  if (result_out != NULL) {
    *result_out = {};
  }

  return THERMISTOR_READER_ERR_NOT_INITIALIZED;
}

static const CliMeasurementHooks k_stub_hooks_success           = {stub_sweep_success, stub_compute_success,
                                                                   stub_measure_all_temperatures_success};
static const CliMeasurementHooks k_stub_hooks_error             = {stub_sweep_error, stub_compute_success,
                                                                   stub_measure_all_temperatures_success};
static const CliMeasurementHooks k_stub_hooks_temperature_error = {stub_sweep_success, stub_compute_success,
                                                                   stub_measure_all_temperatures_error};

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
  g_stub_temperatures.temperatures_c[k_test_sample_index] = 24.25f;

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
  TEST_ASSERT_EQUAL_INT(
      PH_EQUATIONS_OK,
      ph_equations_compute_ph(expected_r_ratio,
                              static_cast<double>(g_stub_temperatures.temperatures_c[k_test_sample_index]),
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
  TEST_ASSERT_TRUE(sample_line.startsWith("error\ttemperature\t"));
}

// Every thermistor must appear in the sample output, in ThermistorId order, with two decimal places.
static void test_cli_sample_reports_every_thermistor(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  stage_sample_stats();

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));

  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("temp_sample   temp_blue  temp_green   temp_gain  temp_drive") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("       24.00       31.50       30.25       25.00       27.75") >= 0);
}

// The count column was dropped from the channel stats table.
static void test_cli_sample_omits_count_column(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  stage_sample_stats();

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));

  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("channel") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("count") < 0);
}

// A failed sample thermistor still emits the measurements, but marks the sensor ERR and pH ERROR.
static void test_cli_sample_invalid_sample_thermistor_reports_ph_error(void) {
  set_baseline_means(10.0, 110.0, 12.0, 212.0);
  set_sample_means(11.0, 61.0, 13.0, 93.0);
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  stage_sample_stats();
  g_stub_temperatures.valid[k_test_sample_index] = false;

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));

  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("         ERR       31.50       30.25       25.00       27.75") >= 0);

  String last_line = g_recording_print.last_line();
  last_line.trim();
  TEST_ASSERT_TRUE(last_line.endsWith("ERROR"));
}

// A failed non-sample thermistor is marked ERR without affecting the pH result.
static void test_cli_sample_invalid_led_thermistor_keeps_ph(void) {
  set_baseline_means(10.0, 110.0, 12.0, 212.0);
  set_sample_means(11.0, 61.0, 13.0, 93.0);
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  stage_sample_stats();
  g_stub_temperatures.valid[static_cast<size_t>(ThermistorId::THERMISTOR_ID_BLUE_LED)] = false;

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));

  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("       24.00         ERR       30.25       25.00       27.75") >= 0);

  String last_line = g_recording_print.last_line();
  last_line.trim();
  TEST_ASSERT_FALSE(last_line.endsWith("ERROR"));
}

// Baseline output includes the temperature table so LED drift between b and s is visible.
static void test_cli_baseline_reports_every_thermistor(void) {
  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));

  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("temp_sample   temp_blue  temp_green   temp_gain  temp_drive") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("       24.00       31.50       30.25       25.00       27.75") >= 0);
}

// A thermistor sweep failure during baseline reports the error and leaves no baseline cached.
static void test_cli_baseline_temperature_failure_does_not_set_cache(void) {
  cli_test_set_measurement_hooks(&k_stub_hooks_temperature_error);

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());

  String last_line = g_recording_print.last_line();
  last_line.trim();
  TEST_ASSERT_TRUE(last_line.startsWith("error\ttemperature\t"));
}

static void test_cli_baseline_failure_does_not_set_cache(void) {
  cli_test_set_measurement_hooks(&k_stub_hooks_error);

  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
}

// CSV schema contract: hosts key off these column names, so changes here are breaking.
static const char* const k_expected_csv_header =
    "type,"
    "drain_blue_mean,drain_blue_stddev,drain_blue_min,drain_blue_max,drain_blue_drift,"
    "drain_green_mean,drain_green_stddev,drain_green_min,drain_green_max,drain_green_drift,"
    "blue_mean,blue_stddev,blue_min,blue_max,blue_drift,"
    "green_mean,green_stddev,green_min,green_max,green_drift,"
    "temp_sample,temp_blue,temp_green,temp_gain,temp_drive,"
    "abs_blue,abs_green,r_ratio,ph";
constexpr size_t k_csv_field_count       = 30u;
constexpr size_t k_csv_temp_sample_field = 21u;
constexpr size_t k_csv_temp_drive_field  = 25u;
constexpr size_t k_csv_abs_blue_field    = 26u;
constexpr size_t k_csv_ph_field          = 29u;

// Computes the pH the CLI should report for the current stub templates and sample thermistor value.
static double expected_ph_from_stubs(void) {
  const double reference_blue =
      g_stub_baseline_stats_template.blue.mean - g_stub_baseline_stats_template.drain_blue.mean;
  const double reference_green =
      g_stub_baseline_stats_template.green.mean - g_stub_baseline_stats_template.drain_green.mean;
  const double sample_blue  = g_stub_sample_stats_template.blue.mean - g_stub_sample_stats_template.drain_blue.mean;
  const double sample_green = g_stub_sample_stats_template.green.mean - g_stub_sample_stats_template.drain_green.mean;

  double absorbance_blue  = 0.0;
  double absorbance_green = 0.0;
  double r_ratio          = 0.0;
  double ph_value         = 0.0;
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK, ph_equations_calc_absorbance(reference_blue, sample_blue, &absorbance_blue));
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK,
                        ph_equations_calc_absorbance(reference_green, sample_green, &absorbance_green));
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK, ph_equations_calc_r_ratio(absorbance_green, absorbance_blue, &r_ratio));
  TEST_ASSERT_EQUAL_INT(
      PH_EQUATIONS_OK,
      ph_equations_compute_ph(r_ratio, static_cast<double>(g_stub_temperatures.temperatures_c[k_test_sample_index]),
                              35.0, &ph_value));
  return ph_value;
}

static void test_cli_csv_command_emits_header(void) {
  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("csv"));
  TEST_ASSERT_EQUAL_STRING(k_expected_csv_header, g_recording_print.last_line().c_str());
}

// In CSV mode a baseline prints exactly one record: no status line, empty absorbance/pH fields.
static void test_cli_csv_baseline_emits_single_record(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("csv"));

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));

  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.endsWith("\n"));
  TEST_ASSERT_EQUAL_INT(output.indexOf('\n'), output.lastIndexOf('\n'));  // exactly one line

  String       fields[k_csv_field_count];
  const String record = g_recording_print.last_line();
  TEST_ASSERT_EQUAL_UINT(k_csv_field_count, split_fields(record, ',', fields, k_csv_field_count));
  TEST_ASSERT_EQUAL_STRING("baseline", fields[0].c_str());
  TEST_ASSERT_EQUAL_STRING("1.25", fields[1].c_str());
  TEST_ASSERT_EQUAL_STRING("0.50", fields[2].c_str());
  TEST_ASSERT_EQUAL_STRING("-2", fields[3].c_str());
  TEST_ASSERT_EQUAL_STRING("3", fields[4].c_str());
  TEST_ASSERT_EQUAL_STRING("0.1000", fields[5].c_str());
  TEST_ASSERT_EQUAL_STRING("24.00", fields[k_csv_temp_sample_field].c_str());
  TEST_ASSERT_EQUAL_STRING("27.75", fields[k_csv_temp_drive_field].c_str());
  for (size_t index = k_csv_abs_blue_field; index <= k_csv_ph_field; ++index) {
    TEST_ASSERT_EQUAL_STRING("", fields[index].c_str());
  }
}

// In CSV mode a sample prints one record whose final field is the computed pH.
static void test_cli_csv_sample_emits_record_with_ph(void) {
  set_baseline_means(10.0, 110.0, 12.0, 212.0);
  set_sample_means(11.0, 61.0, 13.0, 93.0);
  g_stub_temperatures.temperatures_c[k_test_sample_index] = 24.25f;
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("csv"));
  stage_sample_stats();

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));
  TEST_ASSERT_TRUE(g_recording_print.buffer().indexOf("Taking sample...") < 0);

  String       fields[k_csv_field_count];
  const String record = g_recording_print.last_line();
  TEST_ASSERT_EQUAL_UINT(k_csv_field_count, split_fields(record, ',', fields, k_csv_field_count));
  TEST_ASSERT_EQUAL_STRING("sample", fields[0].c_str());
  TEST_ASSERT_EQUAL_STRING("24.25", fields[k_csv_temp_sample_field].c_str());
  TEST_ASSERT_TRUE(fields[k_csv_abs_blue_field].length() > 0u);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, static_cast<float>(expected_ph_from_stubs()), fields[k_csv_ph_field].toFloat());
}

// A failed sample thermistor leaves its field and the pH field empty rather than printing ERR/ERROR.
static void test_cli_csv_invalid_sample_thermistor_leaves_fields_empty(void) {
  set_baseline_means(10.0, 110.0, 12.0, 212.0);
  set_sample_means(11.0, 61.0, 13.0, 93.0);
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("csv"));
  stage_sample_stats();
  g_stub_temperatures.valid[k_test_sample_index] = false;

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("s"));

  String       fields[k_csv_field_count];
  const String record = g_recording_print.last_line();
  TEST_ASSERT_EQUAL_UINT(k_csv_field_count, split_fields(record, ',', fields, k_csv_field_count));
  TEST_ASSERT_EQUAL_STRING("", fields[k_csv_temp_sample_field].c_str());
  TEST_ASSERT_TRUE(fields[k_csv_abs_blue_field].length() > 0u);
  TEST_ASSERT_EQUAL_STRING("", fields[k_csv_ph_field].c_str());
}

// Error lines follow the active separator so a CSV stream stays comma-delimited.
static void test_cli_csv_errors_use_commas(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("csv"));
  cli_test_set_measurement_hooks(&k_stub_hooks_temperature_error);

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_TRUE(g_recording_print.last_line().startsWith("error,temperature,"));

  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_UNKNOWN_COMMAND, (int) cli_dispatch_command("foo"));
  TEST_ASSERT_EQUAL_STRING("error,unknown_command,foo", g_recording_print.last_line().c_str());
}

static void test_cli_table_command_restores_table_output(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("csv"));
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("table"));

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));

  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("Taking baseline...") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("channel") >= 0);
}

// Output mode is session state: re-initialising the CLI returns to table output.
static void test_cli_initialize_resets_output_mode(void) {
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("csv"));
  cli_initialize();
  cli_test_set_measurement_hooks(&k_stub_hooks_success);
  cli_test_set_output(&g_recording_print);

  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("b"));
  TEST_ASSERT_TRUE(g_recording_print.buffer().indexOf("Taking baseline...") >= 0);
}

static void test_cli_version_reports_output_mode(void) {
  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("v"));
  TEST_ASSERT_TRUE(g_recording_print.buffer().indexOf("output_mode:      table") >= 0);

  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("csv"));
  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT((int) CLI_DISPATCH_OK, (int) cli_dispatch_command("v"));
  TEST_ASSERT_TRUE(g_recording_print.buffer().indexOf("output_mode:      csv") >= 0);
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
  RUN_TEST(test_cli_sample_reports_every_thermistor);
  RUN_TEST(test_cli_sample_omits_count_column);
  RUN_TEST(test_cli_sample_invalid_sample_thermistor_reports_ph_error);
  RUN_TEST(test_cli_sample_invalid_led_thermistor_keeps_ph);
  RUN_TEST(test_cli_baseline_reports_every_thermistor);
  RUN_TEST(test_cli_baseline_temperature_failure_does_not_set_cache);
  RUN_TEST(test_cli_baseline_failure_does_not_set_cache);
  RUN_TEST(test_cli_csv_command_emits_header);
  RUN_TEST(test_cli_csv_baseline_emits_single_record);
  RUN_TEST(test_cli_csv_sample_emits_record_with_ph);
  RUN_TEST(test_cli_csv_invalid_sample_thermistor_leaves_fields_empty);
  RUN_TEST(test_cli_csv_errors_use_commas);
  RUN_TEST(test_cli_table_command_restores_table_output);
  RUN_TEST(test_cli_initialize_resets_output_mode);
  RUN_TEST(test_cli_version_reports_output_mode);
  RUN_TEST(test_cli_dispatch_accepts_calibrate_command);
  // NOTE: test_cli_calibrate_clears_baseline_cache and test_cli_calibrate_outputs_recommended_wipers
  // are integration tests that require full hardware initialization. Run them separately.

  UNITY_END();
}

void loop() {
}
