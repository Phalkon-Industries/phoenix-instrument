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
constexpr const char* k_firmware_version = "phoenix-cli 1.0.0";

struct CliCommandEntry {
  const char* name;
  int (*handler)(void);
  const char* help;
};

static Print* g_cli_output = &Serial;

static void cli_emit_error(const char* label, int error_code);

#define CLI_GUARD_EMIT(label, expression) GUARD_EMIT(cli_emit_error, label, expression)

static int  handle_help(void);
static int  handle_version(void);
static int  handle_baseline(void);
static int  handle_sample(void);
static int  handle_calibrate(void);
static void reset_baseline_cache(void);
static void emit_channel_summary(const char* channel_name, const LightReadingsStatisticSummary& summary);
static void emit_baseline_success(const LightReadingsSweepStats& stats);
static void emit_sample_success(const LightReadingsSweepStats& stats, float sample_temperature_c,
                                float enclosure_temperature_c, double absorbance_blue, double absorbance_green,
                                double r_ratio, double ph_value, bool ph_valid);
static int  compute_channel_absorbance(const LightReadingsStatisticSummary& reference_channel,
                                       const LightReadingsStatisticSummary& reference_drain,
                                       const LightReadingsStatisticSummary& sample_channel,
                                       const LightReadingsStatisticSummary& sample_drain, double* absorbance_out);
static int  compute_absorbance_pair(const LightReadingsSweepStats& baseline_stats,
                                    const LightReadingsSweepStats& sample_stats, double* absorbance_blue_out,
                                    double* absorbance_green_out);

constexpr CliCommandEntry k_cli_commands[] = {
    {"b", handle_baseline, "Capture baseline sweep"}, {"s", handle_sample, "Capture sample sweep + pH"},
    {"c", handle_calibrate, "Run light calibration"}, {"v", handle_version, "Print firmware version"},
    {"help", handle_help, "List commands"},           {NULL, NULL, NULL},
};

static bool                    g_cli_ready      = false;
static bool                    g_baseline_valid = false;
static LightReadingsSweepStats g_baseline_stats = {0u,
                                                   {0u, 0.0, 0.0, 0, 0, 0.0, false},
                                                   {0u, 0.0, 0.0, 0, 0, 0.0, false},
                                                   {0u, 0.0, 0.0, 0, 0, 0.0, false},
                                                   {0u, 0.0, 0.0, 0, 0, 0.0, false}};

static const CliMeasurementHooks k_default_measurement_hooks = {
    light_readings_pwm_sweep_n, light_readings_compute_sweep_stats, thermistor_reader_measure_celsius};

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

// Reports firmware version for host sanity checks and diagnostic logging.
static int handle_version(void) {
  g_cli_output->println(k_firmware_version);
  return PHX_OK;
}

// Captures a baseline sweep and caches the resulting statistics for later samples.
static int handle_baseline(void) {
  g_cli_output->println("Taking baseline...");
  delay(1);  // needed for print to happen when it needs to and not after measurement is finished. I think compiler
             // overoptimizes
  LightReadingsSweepCollection sweeps = {0u, g_light_readings_sweep_storage};
  CLI_GUARD_EMIT("sweep", g_measurement_hooks.sweep_n(k_baseline_sweep_count, &sweeps));

  LightReadingsSweepStats stats = {};
  CLI_GUARD_EMIT("stats", g_measurement_hooks.compute_stats(&sweeps, &stats));

  g_baseline_stats = stats;
  g_baseline_valid = true;
  emit_baseline_success(g_baseline_stats);
  return PHX_OK;
}

// Executes the end-to-end sample workflow, including temps, absorbance, and pH.
static int handle_sample(void) {
  // Step 1: Abort immediately when no baseline is present because absorbance math depends on it.
  if (!g_baseline_valid) {
    cli_emit_error("missing_baseline", PHX_ERR_NOT_INITIALIZED);
    return PHX_ERR_NOT_INITIALIZED;
  }

  g_cli_output->println("Taking sample...");
  delay(1);  // needed for print to happen when it needs to and not after measurement is finished. I think compiler
             // overoptimizes
  LightReadingsSweepCollection sweeps       = {0u, g_light_readings_sweep_storage};
  LightReadingsSweepStats      sample_stats = {};

  // Step 2: Capture the raw sample sweeps so statistics can be derived.
  CLI_GUARD_EMIT("sweep", g_measurement_hooks.sweep_n(k_baseline_sweep_count, &sweeps));

  // Step 3: Compute per-channel statistics for downstream absorbance math.
  CLI_GUARD_EMIT("stats", g_measurement_hooks.compute_stats(&sweeps, &sample_stats));

  // Step 4: Record enclosure temperature to help operators track thermal drift.
  float enclosure_temperature_c = 0.0f;
  CLI_GUARD_EMIT("temperature_board",
                 g_measurement_hooks.measure_temperature(ThermistorId::THERMISTOR_ID_BOARD, &enclosure_temperature_c));

  // Step 5: Measure the water probe temperature for the pH computation.
  float sample_temperature_c = 0.0f;
  CLI_GUARD_EMIT("temperature_water",
                 g_measurement_hooks.measure_temperature(ThermistorId::THERMISTOR_ID_WATER, &sample_temperature_c));

  // Step 6: Compute absorbance on both wavelengths using the cached baseline reference.
  double absorbance_blue  = 0.0;
  double absorbance_green = 0.0;
  CLI_GUARD_EMIT("absorbance",
                 compute_absorbance_pair(g_baseline_stats, sample_stats, &absorbance_blue, &absorbance_green));

  // Step 7: Convert the absorbance pair into the r-ratio expected by the pH library.
  // If this fails (e.g. negative ratio), we still emit the measurements for diagnostics.
  double r_ratio        = 0.0;
  bool   ph_valid       = true;
  int    r_ratio_result = ph_equations_calc_r_ratio(absorbance_green, absorbance_blue, &r_ratio);
  if (r_ratio_result != PH_EQUATIONS_OK) {
    ph_valid = false;
  }

  // Step 8: Use the r-ratio plus temperature and salinity to produce the final pH reading.
  double ph_value = 0.0;
  if (ph_valid) {
    int ph_result = ph_equations_compute_ph(r_ratio, static_cast<double>(sample_temperature_c),
                                            k_cli_default_salinity_psu, &ph_value);
    if (ph_result != PH_EQUATIONS_OK) {
      ph_valid = false;
    }
  }

  emit_sample_success(sample_stats, sample_temperature_c, enclosure_temperature_c, absorbance_blue, absorbance_green,
                      r_ratio, ph_value, ph_valid);

  return PHX_OK;
}

// Clears cached baseline data so the next run forces a new measurement.
static void reset_baseline_cache(void) {
  g_baseline_valid = false;

  memset(&g_baseline_stats, 0, sizeof(g_baseline_stats));
}

// Progress callback for calibration; prints each wiper result in a table row.
static void calibration_progress_callback(uint8_t wiper_code, int32_t blue_max, int32_t green_max, bool blue_sat,
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

// Emits fixed-width summary stats for a single channel to ensure columns align.
static void emit_channel_summary(const char* channel_name, const LightReadingsStatisticSummary& summary) {
  char line[120];
  snprintf(line, sizeof(line), "%-12s %5lu %14.2f %10.2f %10ld %10ld %10.4f", channel_name,
           (unsigned long) summary.sample_count, summary.mean, summary.standard_deviation, (long) summary.min_value,
           (long) summary.max_value, summary.drift_slope);
  g_cli_output->println(line);
}

// Emits a baseline success row with a header for readability.
static void emit_baseline_success(const LightReadingsSweepStats& stats) {
  g_cli_output->println("channel      count           mean     stddev        min        max      drift");

  emit_channel_summary("drain_blue", stats.drain_blue);
  emit_channel_summary("drain_green", stats.drain_green);
  emit_channel_summary("blue", stats.blue);
  emit_channel_summary("green", stats.green);
}

// Emits the shared error format used by GUARD_EMIT callers.
static void cli_emit_error(const char* label, int error_code) {
  g_cli_output->print("error\t");
  g_cli_output->print(label);
  g_cli_output->print("\t");
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

// Emits the full sample success payload with headers for readability.
static void emit_sample_success(const LightReadingsSweepStats& stats, float sample_temperature_c,
                                float enclosure_temperature_c, double absorbance_blue, double absorbance_green,
                                double r_ratio, double ph_value, bool ph_valid) {
  g_cli_output->println("channel      count           mean     stddev        min        max      drift");

  emit_channel_summary("drain_blue", stats.drain_blue);
  emit_channel_summary("drain_green", stats.drain_green);
  emit_channel_summary("blue", stats.blue);
  emit_channel_summary("green", stats.green);

  g_cli_output->println();
  g_cli_output->println("temp_sample  temp_encl   abs_blue   abs_green    r_ratio        pH");
  char result_line[120];
  if (ph_valid) {
    snprintf(result_line, sizeof(result_line), "%11.2f %10.2f %10.6f %11.6f %10.6f %9.4f",
             static_cast<double>(sample_temperature_c), static_cast<double>(enclosure_temperature_c), absorbance_blue,
             absorbance_green, r_ratio, ph_value);
  }
  else {
    snprintf(result_line, sizeof(result_line), "%11.2f %10.2f %10.6f %11.6f %10.6f     ERROR",
             static_cast<double>(sample_temperature_c), static_cast<double>(enclosure_temperature_c), absorbance_blue,
             absorbance_green, r_ratio);
  }
  g_cli_output->println(result_line);
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

  g_cli_output->print("error\tunknown_command\t");
  g_cli_output->println(command_token);
  return CLI_DISPATCH_UNKNOWN_COMMAND;
}

void cli_initialize(void) {
  reset_baseline_cache();
  g_measurement_hooks = k_default_measurement_hooks;
  g_cli_ready         = true;
  g_cli_output        = &Serial;
  g_cli_output->println("phoenix-cli ready (commands: b, s, c, v, help)");
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
