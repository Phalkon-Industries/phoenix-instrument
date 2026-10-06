#include "cli.hpp"

#include "light_calibration.hpp"
#include "ph_equations.hpp"
#include "phoenix_guard.hpp"
#include "phoenix_settings.hpp"
#include <Arduino.h>
#include <string.h>

namespace {
constexpr size_t   k_cli_max_command_length   = 32u;
constexpr uint32_t k_baseline_sweep_count     = 500u;
constexpr double   k_cli_default_salinity_psu = 35.0;

// Firmware version string reported by the 'v' command for host sanity checks.
constexpr const char* k_firmware_version = "phoenix-cli 1.1.0";

// Column labels for every thermistor, indexed by ThermistorId so they line up with ThermistorSweepResult.
constexpr size_t      k_cli_thermistor_count                           = 5u;
constexpr const char* k_cli_temperature_labels[k_cli_thermistor_count] = {
    "temp_sample", "temp_blue", "temp_green", "temp_gain", "temp_drive",
};
static_assert(sizeof(ThermistorSweepResult::valid) / sizeof(ThermistorSweepResult::valid[0]) == k_cli_thermistor_count,
              "CLI temperature labels must cover every thermistor");

constexpr size_t k_cli_sample_thermistor_index = static_cast<size_t>(ThermistorId::THERMISTOR_ID_SAMPLE);

// Channel order shared by the stats table and the CSV columns; see channel_summary_at().
constexpr size_t      k_cli_channel_count                      = 4u;
constexpr const char* k_cli_channel_names[k_cli_channel_count] = {"drain_blue", "drain_green", "blue", "green"};

// Per-channel statistic columns emitted in CSV mode, in row order.
constexpr size_t      k_cli_channel_stat_count                           = 5u;
constexpr const char* k_cli_channel_stat_names[k_cli_channel_stat_count] = {"mean", "stddev", "min", "max", "drift"};

// Selects how baseline/sample results are printed. Table is the human-readable default; CSV emits one
// comma-separated record per command under a fixed header so host scripts can log results directly.
enum class CliOutputMode : uint8_t {
  CLI_OUTPUT_MODE_TABLE = 0u,
  CLI_OUTPUT_MODE_CSV,
};

struct CliCommandEntry {
  const char* name;
  int (*handler)(void);
  const char* help;
};

// Derived values from a sample run; ph_valid is false when pH could not be computed.
struct CliSampleResult {
  double absorbance_blue;
  double absorbance_green;
  double r_ratio;
  double ph_value;
  bool   ph_valid;
};

static Print* g_cli_output = &Serial;

static void cli_emit_error(const char* label, int error_code);

#define CLI_GUARD_EMIT(label, expression) GUARD_EMIT(cli_emit_error, label, expression)

static int                                  handle_help(void);
static int                                  handle_version(void);
static int                                  handle_baseline(void);
static int                                  handle_sample(void);
static int                                  handle_calibrate(void);
static int                                  handle_csv_mode(void);
static int                                  handle_table_mode(void);
static void                                 reset_baseline_cache(void);
static char                                 cli_field_separator(void);
static void                                 emit_status(const char* message);
static const LightReadingsStatisticSummary& channel_summary_at(const LightReadingsSweepStats& stats, size_t index);
static void                                 emit_channel_stats(const LightReadingsSweepStats& stats);
static void emit_channel_summary(const char* channel_name, const LightReadingsStatisticSummary& summary);
static void emit_temperatures(const ThermistorSweepResult& temperatures);
static void emit_baseline_success(const LightReadingsSweepStats& stats, const ThermistorSweepResult& temperatures);
static void emit_sample_success(const LightReadingsSweepStats& stats, const ThermistorSweepResult& temperatures,
                                const CliSampleResult& result);
static void emit_csv_header(void);
static void emit_csv_row(const char* record_type, const LightReadingsSweepStats& stats,
                         const ThermistorSweepResult& temperatures, const CliSampleResult* result);
static int  compute_channel_absorbance(const LightReadingsStatisticSummary& reference_channel,
                                       const LightReadingsStatisticSummary& reference_drain,
                                       const LightReadingsStatisticSummary& sample_channel,
                                       const LightReadingsStatisticSummary& sample_drain, double* absorbance_out);
static int  compute_absorbance_pair(const LightReadingsSweepStats& baseline_stats,
                                    const LightReadingsSweepStats& sample_stats, double* absorbance_blue_out,
                                    double* absorbance_green_out);

constexpr CliCommandEntry k_cli_commands[] = {
    {"b", handle_baseline, "Capture baseline sweep"},
    {"s", handle_sample, "Capture sample sweep + pH"},
    {"c", handle_calibrate, "Run light calibration"},
    {"v", handle_version, "Print firmware version"},
    {"csv", handle_csv_mode, "Print b/s results as CSV records (emits header)"},
    {"table", handle_table_mode, "Print b/s results as tables (default)"},
    {"help", handle_help, "List commands"},
    {NULL, NULL, NULL},
};

static CliOutputMode           g_cli_output_mode      = CliOutputMode::CLI_OUTPUT_MODE_TABLE;
static bool                    g_cli_ready            = false;
static bool                    g_ready_banner_sent    = false;
static bool                    g_serial_was_connected = false;
static bool                    g_baseline_valid       = false;
static LightReadingsSweepStats g_baseline_stats       = {0u,
                                                         {0u, 0.0, 0.0, 0, 0, 0.0, false},
                                                         {0u, 0.0, 0.0, 0, 0, 0.0, false},
                                                         {0u, 0.0, 0.0, 0, 0, 0.0, false},
                                                         {0u, 0.0, 0.0, 0, 0, 0.0, false}};

static const CliMeasurementHooks k_default_measurement_hooks = {
    light_readings_pwm_sweep_n, light_readings_compute_sweep_stats, thermistor_reader_measure_all};

static CliMeasurementHooks g_measurement_hooks = k_default_measurement_hooks;

// Lists all CLI commands so operators can discover the available shortcuts.
static int handle_help(void) {
  for (size_t index = 0u; k_cli_commands[index].name != NULL; ++index) {
    g_cli_output->print(k_cli_commands[index].name);
    g_cli_output->print("\t");
    g_cli_output->println(k_cli_commands[index].help);
  }

  return PHX_OK;
}

// Reports firmware version and current settings for host sanity checks and diagnostic logging.
static int handle_version(void) {
  g_cli_output->println(k_firmware_version);

  // Step 1: Display current calibration settings from flash.
  const PhoenixSettings* settings = phoenix_settings_get();
  if (settings != nullptr) {
    char line[80];
    snprintf(line, sizeof(line), "blue_wiper_code:  0x%02X", static_cast<unsigned>(settings->blue_wiper_code));
    g_cli_output->println(line);
    snprintf(line, sizeof(line), "green_wiper_code: 0x%02X", static_cast<unsigned>(settings->green_wiper_code));
    g_cli_output->println(line);
  }
  else {
    g_cli_output->println("settings: not initialized");
  }

  // Step 2: Report the active output mode so hosts can confirm how b/s results will be formatted.
  g_cli_output->println((g_cli_output_mode == CliOutputMode::CLI_OUTPUT_MODE_CSV) ? "output_mode:      csv" :
                                                                                    "output_mode:      table");

  return PHX_OK;
}

// Switches b/s output to CSV and prints the header so a host log starts with column names.
static int handle_csv_mode(void) {
  g_cli_output_mode = CliOutputMode::CLI_OUTPUT_MODE_CSV;
  emit_csv_header();
  return PHX_OK;
}

// Restores the human-readable table output for b/s.
static int handle_table_mode(void) {
  g_cli_output_mode = CliOutputMode::CLI_OUTPUT_MODE_TABLE;
  g_cli_output->println("output_mode: table");
  return PHX_OK;
}

// Captures a baseline sweep and caches the resulting statistics for later samples.
static int handle_baseline(void) {
  emit_status("Taking baseline...");
  delay(1);  // needed for print to happen when it needs to and not after measurement is finished. I think compiler
             // overoptimizes
  LightReadingsSweepCollection sweeps = {0u, g_light_readings_sweep_storage};
  CLI_GUARD_EMIT("sweep", g_measurement_hooks.sweep_n(k_baseline_sweep_count, &sweeps));

  LightReadingsSweepStats stats = {};
  CLI_GUARD_EMIT("stats", g_measurement_hooks.compute_stats(&sweeps, &stats));

  // Record every thermistor so LED and board temperatures can be compared against the sample run.
  ThermistorSweepResult temperatures = {};
  CLI_GUARD_EMIT("temperature", g_measurement_hooks.measure_all_temperatures(&temperatures));

  g_baseline_stats = stats;
  g_baseline_valid = true;
  emit_baseline_success(g_baseline_stats, temperatures);
  return PHX_OK;
}

// Executes the end-to-end sample workflow, including temps, absorbance, and pH.
static int handle_sample(void) {
  // Step 1: Abort immediately when no baseline is present because absorbance math depends on it.
  if (!g_baseline_valid) {
    cli_emit_error("missing_baseline", PHX_ERR_NOT_INITIALIZED);
    return PHX_ERR_NOT_INITIALIZED;
  }

  emit_status("Taking sample...");
  delay(1);  // needed for print to happen when it needs to and not after measurement is finished. I think compiler
             // overoptimizes
  LightReadingsSweepCollection sweeps       = {0u, g_light_readings_sweep_storage};
  LightReadingsSweepStats      sample_stats = {};

  // Step 2: Capture the raw sample sweeps so statistics can be derived.
  CLI_GUARD_EMIT("sweep", g_measurement_hooks.sweep_n(k_baseline_sweep_count, &sweeps));

  // Step 3: Compute per-channel statistics for downstream absorbance math.
  CLI_GUARD_EMIT("stats", g_measurement_hooks.compute_stats(&sweeps, &sample_stats));

  // Step 4: Sweep every thermistor in one rail pulse. The sample thermistor feeds the pH computation; the LED,
  //         gain-stage, and LED-drive-stage readings let operators track thermal drift. Individual sensor failures
  //         are reported per column instead of aborting the sample.
  ThermistorSweepResult temperatures = {};
  CLI_GUARD_EMIT("temperature", g_measurement_hooks.measure_all_temperatures(&temperatures));

  // Step 5: Compute absorbance on both wavelengths using the cached baseline reference.
  CliSampleResult result = {0.0, 0.0, 0.0, 0.0, false};
  CLI_GUARD_EMIT("absorbance", compute_absorbance_pair(g_baseline_stats, sample_stats, &result.absorbance_blue,
                                                       &result.absorbance_green));

  // Step 6: Convert the absorbance pair into the r-ratio expected by the pH library.
  // If this fails (e.g. negative ratio), we still emit the measurements for diagnostics.
  result.ph_valid    = temperatures.valid[k_cli_sample_thermistor_index];
  int r_ratio_result = ph_equations_calc_r_ratio(result.absorbance_green, result.absorbance_blue, &result.r_ratio);
  if (r_ratio_result != PH_EQUATIONS_OK) {
    result.ph_valid = false;
  }

  // Step 7: Use the r-ratio plus sample temperature and salinity to produce the final pH reading.
  if (result.ph_valid) {
    const double sample_temperature_c = static_cast<double>(temperatures.temperatures_c[k_cli_sample_thermistor_index]);
    int          ph_result =
        ph_equations_compute_ph(result.r_ratio, sample_temperature_c, k_cli_default_salinity_psu, &result.ph_value);
    if (ph_result != PH_EQUATIONS_OK) {
      result.ph_valid = false;
    }
  }

  emit_sample_success(sample_stats, temperatures, result);

  return PHX_OK;
}

// Clears cached baseline data so the next run forces a new measurement.
static void reset_baseline_cache(void) {
  g_baseline_valid = false;

  memset(&g_baseline_stats, 0, sizeof(g_baseline_stats));
}

// Progress callback for calibration; prints each wiper result in a table row.
static void calibration_progress_callback(uint16_t wiper_code, int32_t blue_max, int32_t green_max, bool blue_sat,
                                          bool green_sat) {
  char line[80];
  snprintf(line, sizeof(line), "%5u %10ld %10ld   %s   %s", static_cast<unsigned>(wiper_code), (long) blue_max,
           (long) green_max, blue_sat ? "YES" : " NO", green_sat ? "YES" : " NO");
  g_cli_output->println(line);
}

// Runs a light calibration sweep and saves the recommended wiper codes to flash.
static int handle_calibrate(void) {
  g_cli_output->println("Running light calibration...");
  delay(1);  // Allow print to flush before long operation.

  // Step 1: Print table header.
  g_cli_output->println("wiper   blue_max  green_max  b_sat  g_sat");

  // Step 2: Run calibration with progress reporting.
  LightCalibrationResult result = light_calibration_run_with_progress(nullptr, calibration_progress_callback);

  // Step 3: Check for errors.
  if (!result.success) {
    g_cli_output->print("error\tcalibration_failed\t");
    g_cli_output->println(result.error_message != nullptr ? result.error_message : "unknown");
    return PHX_ERR_COMMUNICATION;
  }

  // Step 4: Print recommendations.
  g_cli_output->println();
  g_cli_output->println("Recommended wiper codes:");
  char line[80];
  if (result.blue_valid) {
    snprintf(line, sizeof(line), "  blue:  0x%02X (max code: %ld)", static_cast<unsigned>(result.blue_wiper_code),
             (long) result.blue_max_code);
  }
  else {
    snprintf(line, sizeof(line), "  blue:  0x%02X (all saturated, using fallback)",
             static_cast<unsigned>(result.blue_wiper_code));
  }
  g_cli_output->println(line);

  if (result.green_valid) {
    snprintf(line, sizeof(line), "  green: 0x%02X (max code: %ld)", static_cast<unsigned>(result.green_wiper_code),
             (long) result.green_max_code);
  }
  else {
    snprintf(line, sizeof(line), "  green: 0x%02X (all saturated, using fallback)",
             static_cast<unsigned>(result.green_wiper_code));
  }
  g_cli_output->println(line);

  // Step 5: Save calibrated values to flash.
  g_cli_output->print("Saving calibration... ");
  PhoenixSettings new_settings  = *phoenix_settings_get();
  new_settings.blue_wiper_code  = result.blue_wiper_code;
  new_settings.green_wiper_code = result.green_wiper_code;
  int save_result               = phoenix_settings_save(&new_settings);
  if (save_result != PHOENIX_SETTINGS_OK) {
    g_cli_output->println("FAILED");
    return save_result;
  }
  g_cli_output->println("OK");

  // Step 6: Apply new wiper codes to hardware.
  g_cli_output->print("Applying wiper codes... ");
  int apply_result = phoenix_settings_apply_wiper_codes();
  if (apply_result != PHOENIX_SETTINGS_OK) {
    g_cli_output->println("FAILED");
    return apply_result;
  }
  g_cli_output->println("OK");

  // Step 7: Invalidate baseline since wiper codes changed.
  reset_baseline_cache();
  g_cli_output->println("Note: Baseline cleared. Run 'b' before next sample.");

  return PHX_OK;
}

// Returns the field separator for the active output mode so error lines match the surrounding stream.
static char cli_field_separator(void) {
  return (g_cli_output_mode == CliOutputMode::CLI_OUTPUT_MODE_CSV) ? ',' : '\t';
}

// Prints a progress message in table mode only, keeping the CSV stream limited to header/records/errors.
static void emit_status(const char* message) {
  if (g_cli_output_mode == CliOutputMode::CLI_OUTPUT_MODE_TABLE) {
    g_cli_output->println(message);
  }
}

// Returns channel summaries in k_cli_channel_names order.
static const LightReadingsStatisticSummary& channel_summary_at(const LightReadingsSweepStats& stats, size_t index) {
  switch (index) {
    case 0u:
      return stats.drain_blue;
    case 1u:
      return stats.drain_green;
    case 2u:
      return stats.blue;
    default:
      return stats.green;
  }
}

// Emits the per-channel stats table shared by baseline and sample output.
static void emit_channel_stats(const LightReadingsSweepStats& stats) {
  char line[120];
  snprintf(line, sizeof(line), "%-12s %14s %10s %10s %10s %10s", "channel", "mean", "stddev", "min", "max", "drift");
  g_cli_output->println(line);

  for (size_t index = 0u; index < k_cli_channel_count; ++index) {
    emit_channel_summary(k_cli_channel_names[index], channel_summary_at(stats, index));
  }
}

// Emits fixed-width summary stats for a single channel to ensure columns align.
static void emit_channel_summary(const char* channel_name, const LightReadingsStatisticSummary& summary) {
  char line[120];
  snprintf(line, sizeof(line), "%-12s %14.2f %10.2f %10ld %10ld %10.4f", channel_name, summary.mean,
           summary.standard_deviation, (long) summary.min_value, (long) summary.max_value, summary.drift_slope);
  g_cli_output->println(line);
}

// Emits every thermistor reading in °C as a header row plus a value row; failed sensors print ERR.
static void emit_temperatures(const ThermistorSweepResult& temperatures) {
  char cell[16];
  for (size_t index = 0u; index < k_cli_thermistor_count; ++index) {
    snprintf(cell, sizeof(cell), "%12s", k_cli_temperature_labels[index]);
    g_cli_output->print(cell);
  }
  g_cli_output->println();

  for (size_t index = 0u; index < k_cli_thermistor_count; ++index) {
    if (temperatures.valid[index]) {
      snprintf(cell, sizeof(cell), "%12.2f", static_cast<double>(temperatures.temperatures_c[index]));
    }
    else {
      snprintf(cell, sizeof(cell), "%12s", "ERR");
    }
    g_cli_output->print(cell);
  }
  g_cli_output->println();
}

// Emits baseline channel stats followed by the temperatures captured alongside them.
static void emit_baseline_success(const LightReadingsSweepStats& stats, const ThermistorSweepResult& temperatures) {
  if (g_cli_output_mode == CliOutputMode::CLI_OUTPUT_MODE_CSV) {
    emit_csv_row("baseline", stats, temperatures, NULL);
    return;
  }

  emit_channel_stats(stats);
  g_cli_output->println();
  emit_temperatures(temperatures);
}

// Emits the CSV header shared by baseline and sample records. Columns: record type, then mean/stddev/min/max/drift
// per channel, then every thermistor, then absorbance/r-ratio/pH (left empty on baseline records).
static void emit_csv_header(void) {
  g_cli_output->print("type");
  for (size_t channel = 0u; channel < k_cli_channel_count; ++channel) {
    for (size_t stat = 0u; stat < k_cli_channel_stat_count; ++stat) {
      g_cli_output->print(',');
      g_cli_output->print(k_cli_channel_names[channel]);
      g_cli_output->print('_');
      g_cli_output->print(k_cli_channel_stat_names[stat]);
    }
  }
  for (size_t index = 0u; index < k_cli_thermistor_count; ++index) {
    g_cli_output->print(',');
    g_cli_output->print(k_cli_temperature_labels[index]);
  }
  g_cli_output->println(",abs_blue,abs_green,r_ratio,ph");
}

// Emits one CSV record matching emit_csv_header(). Missing values (no samples, failed thermistor, baseline-only
// records, or an invalid pH) are written as empty fields so CSV readers treat them as null.
static void emit_csv_row(const char* record_type, const LightReadingsSweepStats& stats,
                         const ThermistorSweepResult& temperatures, const CliSampleResult* result) {
  char cell[96];
  g_cli_output->print(record_type);

  for (size_t channel = 0u; channel < k_cli_channel_count; ++channel) {
    const LightReadingsStatisticSummary& summary = channel_summary_at(stats, channel);
    if (summary.has_samples) {
      snprintf(cell, sizeof(cell), ",%.2f,%.2f,%ld,%ld,%.4f", summary.mean, summary.standard_deviation,
               (long) summary.min_value, (long) summary.max_value, summary.drift_slope);
    }
    else {
      snprintf(cell, sizeof(cell), ",,,,,");
    }
    g_cli_output->print(cell);
  }

  for (size_t index = 0u; index < k_cli_thermistor_count; ++index) {
    if (temperatures.valid[index]) {
      snprintf(cell, sizeof(cell), ",%.2f", static_cast<double>(temperatures.temperatures_c[index]));
    }
    else {
      snprintf(cell, sizeof(cell), ",");
    }
    g_cli_output->print(cell);
  }

  if (result == NULL) {
    g_cli_output->println(",,,,");
    return;
  }

  snprintf(cell, sizeof(cell), ",%.6f,%.6f,%.6f", result->absorbance_blue, result->absorbance_green, result->r_ratio);
  g_cli_output->print(cell);
  if (result->ph_valid) {
    snprintf(cell, sizeof(cell), ",%.4f", result->ph_value);
  }
  else {
    snprintf(cell, sizeof(cell), ",");
  }
  g_cli_output->println(cell);
}

// Emits the shared error format used by GUARD_EMIT callers; comma-separated in CSV mode, tab-separated otherwise.
static void cli_emit_error(const char* label, int error_code) {
  const char separator = cli_field_separator();
  g_cli_output->print("error");
  g_cli_output->print(separator);
  g_cli_output->print(label);
  g_cli_output->print(separator);
  g_cli_output->println(error_code);
}

// Computes the absorbance for a single color channel using baseline and sample summaries.
static int compute_channel_absorbance(const LightReadingsStatisticSummary& reference_channel,
                                      const LightReadingsStatisticSummary& reference_drain,
                                      const LightReadingsStatisticSummary& sample_channel,
                                      const LightReadingsStatisticSummary& sample_drain, double* absorbance_out) {
  if (absorbance_out == NULL) {
    return PH_EQUATIONS_ERR_INVALID_ARG;
  }

  if ((!reference_channel.has_samples) || (!reference_drain.has_samples) || (!sample_channel.has_samples) ||
      (!sample_drain.has_samples)) {
    return PH_EQUATIONS_ERR_INVALID_ARG;
  }

  const double reference_intensity = reference_channel.mean - reference_drain.mean;
  const double sample_intensity    = sample_channel.mean - sample_drain.mean;
  if ((reference_intensity <= 0.0) || (sample_intensity <= 0.0)) {
    return PH_EQUATIONS_ERR_INVALID_ARG;
  }

  return ph_equations_calc_absorbance(reference_intensity, sample_intensity, absorbance_out);
}

// Computes absorbance for both color channels, short-circuiting on the first failure.
static int compute_absorbance_pair(const LightReadingsSweepStats& baseline_stats,
                                   const LightReadingsSweepStats& sample_stats, double* absorbance_blue_out,
                                   double* absorbance_green_out) {
  if ((absorbance_blue_out == NULL) || (absorbance_green_out == NULL)) {
    return PH_EQUATIONS_ERR_INVALID_ARG;
  }

  GUARD(compute_channel_absorbance(baseline_stats.blue, baseline_stats.drain_blue, sample_stats.blue,
                                   sample_stats.drain_blue, absorbance_blue_out));

  return compute_channel_absorbance(baseline_stats.green, baseline_stats.drain_green, sample_stats.green,
                                    sample_stats.drain_green, absorbance_green_out);
}

// Emits the full sample success payload with headers for readability. The pH row is always last so hosts can
// read the final result from the last line.
static void emit_sample_success(const LightReadingsSweepStats& stats, const ThermistorSweepResult& temperatures,
                                const CliSampleResult& result) {
  if (g_cli_output_mode == CliOutputMode::CLI_OUTPUT_MODE_CSV) {
    emit_csv_row("sample", stats, temperatures, &result);
    return;
  }

  emit_channel_stats(stats);
  g_cli_output->println();
  emit_temperatures(temperatures);
  g_cli_output->println();

  char line[120];
  snprintf(line, sizeof(line), "%12s%12s%12s%12s", "abs_blue", "abs_green", "r_ratio", "pH");
  g_cli_output->println(line);
  if (result.ph_valid) {
    snprintf(line, sizeof(line), "%12.6f%12.6f%12.6f%12.4f", result.absorbance_blue, result.absorbance_green,
             result.r_ratio, result.ph_value);
  }
  else {
    snprintf(line, sizeof(line), "%12.6f%12.6f%12.6f%12s", result.absorbance_blue, result.absorbance_green,
             result.r_ratio, "ERROR");
  }
  g_cli_output->println(line);
}
}  // namespace

CliDispatchResult cli_dispatch_command(const char* command_token) {
  if ((command_token == NULL) || (command_token[0] == '\0')) {
    return CLI_DISPATCH_EMPTY_COMMAND;
  }

  for (size_t index = 0u; k_cli_commands[index].name != NULL; ++index) {
    if (strcmp(command_token, k_cli_commands[index].name) == 0) {
      (void) k_cli_commands[index].handler();
      return CLI_DISPATCH_OK;
    }
  }

  const char separator = cli_field_separator();
  g_cli_output->print("error");
  g_cli_output->print(separator);
  g_cli_output->print("unknown_command");
  g_cli_output->print(separator);
  g_cli_output->println(command_token);
  return CLI_DISPATCH_UNKNOWN_COMMAND;
}

void cli_initialize(void) {
  reset_baseline_cache();
  g_cli_output_mode   = CliOutputMode::CLI_OUTPUT_MODE_TABLE;
  g_measurement_hooks = k_default_measurement_hooks;
  g_cli_ready         = true;
  g_ready_banner_sent = false;
  g_cli_output        = &Serial;
  // Ready banner deferred until Serial is connected (checked in cli_poll).
}

bool cli_test_is_baseline_cached(void) {
  return g_baseline_valid;
}

void cli_test_get_baseline_stats(LightReadingsSweepStats* stats_out) {
  if (stats_out == NULL) {
    return;
  }

  *stats_out = g_baseline_stats;
}

void cli_test_set_measurement_hooks(const CliMeasurementHooks* hooks) {
  if (hooks == NULL) {
    g_measurement_hooks = k_default_measurement_hooks;
    return;
  }

  g_measurement_hooks = *hooks;
}

void cli_test_set_output(Print* output) {
  if (output == NULL) {
    g_cli_output = &Serial;
    return;
  }

  g_cli_output = output;
}

void cli_poll(void) {
  if (!g_cli_ready) {
    return;
  }

  // Step 1: Detect Serial reconnection and reset banner flag.
  const bool serial_connected = static_cast<bool>(Serial);
  if (!serial_connected && g_serial_was_connected) {
    // Serial just disconnected; reset banner so it re-sends on next connection.
    g_ready_banner_sent = false;
  }
  g_serial_was_connected = serial_connected;

  // Step 2: Send ready banner when Serial connection is established.
  if (!g_ready_banner_sent && serial_connected) {
    g_cli_output->println("phoenix-cli ready (commands: b, s, c, v, csv, table, help)");
    g_ready_banner_sent = true;
  }

  static char   command_buffer[k_cli_max_command_length] = {0};
  static size_t command_length                           = 0u;

  while (Serial.available() > 0) {
    const int incoming_byte = Serial.read();
    if (incoming_byte < 0) {
      break;
    }

    const char incoming_char = static_cast<char>(incoming_byte);

    if ((incoming_char == '\n') || (incoming_char == '\r')) {
      command_buffer[command_length] = '\0';
      if (command_length > 0u) {
        (void) cli_dispatch_command(command_buffer);
      }
      command_length = 0u;
      continue;
    }

    if (command_length < (k_cli_max_command_length - 1u)) {
      command_buffer[command_length] = incoming_char;
      ++command_length;
    }
  }
}
