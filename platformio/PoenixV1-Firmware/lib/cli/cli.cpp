#include "cli.hpp"

#include <Arduino.h>
#include <string.h>

namespace {
constexpr size_t   k_cli_max_command_length = 32u;
constexpr uint32_t k_baseline_sweep_count   = 500u;

struct CliCommandEntry {
  const char* name;
  void (*handler)(void);
  const char* help;
};

static void handle_help(void);
static void handle_baseline(void);
static void handle_sample(void);
static void reset_baseline_cache(void);
static void emit_channel_summary(const LightReadingsStatisticSummary& summary);
static void emit_baseline_success(const LightReadingsSweepStats& stats);
static void emit_baseline_error(int return_code);
static int  capture_baseline_and_cache(void);

constexpr CliCommandEntry k_cli_commands[] = {
    {"b", handle_baseline, "Capture baseline (placeholder)"},
    {"s", handle_sample, "Capture sample + pH (placeholder)"},
    {"help", handle_help, "List commands"},
    {NULL, NULL, NULL},
};

static bool                    g_cli_ready      = false;
static bool                    g_baseline_valid = false;
static LightReadingsSweepStats g_baseline_stats = {0u,
                                                   {0u, 0.0, 0.0, 0, 0, 0.0, false},
                                                   {0u, 0.0, 0.0, 0, 0, 0.0, false},
                                                   {0u, 0.0, 0.0, 0, 0, 0.0, false},
                                                   {0u, 0.0, 0.0, 0, 0, 0.0, false}};

static const CliMeasurementHooks k_default_measurement_hooks = {light_readings_sweep_n,
                                                                light_readings_compute_sweep_stats};

static CliMeasurementHooks g_measurement_hooks = k_default_measurement_hooks;

// Step 1: Emit the available commands for quick discovery.
static void handle_help(void) {
  for (size_t index = 0u; k_cli_commands[index].name != NULL; ++index) {
    Serial.print(k_cli_commands[index].name);
    Serial.print("\t");
    Serial.println(k_cli_commands[index].help);
  }
}

// Step 1: Placeholder for baseline capture; will be wired to measurement flow.
static void handle_baseline(void) {
  const int return_code = capture_baseline_and_cache();
  if (return_code != LIGHT_READINGS_OK) {
    emit_baseline_error(return_code);
    return;
  }

  emit_baseline_success(g_baseline_stats);
}

// Step 1: Placeholder for sample capture; will be wired to measurement flow.
static void handle_sample(void) {
  if (!g_baseline_valid) {
    Serial.println("s\terror\tmissing_baseline");
    return;
  }

  Serial.println("s\tok\tstats_placeholder\tabsorbance_placeholder\tph_placeholder");
}

// Step 1: Clear baseline cache so each session starts from a known state.
static void reset_baseline_cache(void) {
  g_baseline_valid = false;

  memset(&g_baseline_stats, 0, sizeof(g_baseline_stats));
}

// Step 1: Emit a single channel summary using tab-delimited fields.
static void emit_channel_summary(const LightReadingsStatisticSummary& summary) {
  Serial.print(summary.sample_count);
  Serial.print("\t");
  Serial.print(summary.mean, 6);
  Serial.print("\t");
  Serial.print(summary.standard_deviation, 6);
  Serial.print("\t");
  Serial.print(summary.min_value);
  Serial.print("\t");
  Serial.print(summary.max_value);
  Serial.print("\t");
  Serial.print(summary.drift_slope, 6);
}

// Step 1: Emit the baseline success line with cached statistics.
static void emit_baseline_success(const LightReadingsSweepStats& stats) {
  Serial.print("b\tok\t");
  Serial.print(stats.sweep_count);
  Serial.print("\t");

  emit_channel_summary(stats.drain_blue);
  Serial.print("\t");
  emit_channel_summary(stats.drain_green);
  Serial.print("\t");
  emit_channel_summary(stats.blue);
  Serial.print("\t");
  emit_channel_summary(stats.green);
  Serial.println();
}

// Step 1: Emit an error response when baseline capture fails.
static void emit_baseline_error(int return_code) {
  Serial.print("b\terror\treturn_code=");
  Serial.println(return_code);
}

// Step 1: Capture a baseline sweep, compute statistics, and cache the results.
static int capture_baseline_and_cache(void) {
  LightReadingsSweepCollection sweeps = {0u, g_light_readings_sweep_storage};

  const int sweep_return_code = g_measurement_hooks.sweep_n(k_baseline_sweep_count, &sweeps);
  if (sweep_return_code != LIGHT_READINGS_OK) {
    return sweep_return_code;
  }

  LightReadingsSweepStats stats             = {};
  const int               stats_return_code = g_measurement_hooks.compute_stats(&sweeps, &stats);
  if (stats_return_code != LIGHT_READINGS_OK) {
    return stats_return_code;
  }

  g_baseline_stats = stats;
  g_baseline_valid = true;
  return LIGHT_READINGS_OK;
}
}  // namespace

CliDispatchResult cli_dispatch_command(const char* command_token) {
  if ((command_token == NULL) || (command_token[0] == '\0')) {
    return CliDispatchResult::empty_command;
  }

  for (size_t index = 0u; k_cli_commands[index].name != NULL; ++index) {
    if (strcmp(command_token, k_cli_commands[index].name) == 0) {
      k_cli_commands[index].handler();
      return CliDispatchResult::ok;
    }
  }

  Serial.print("error\tunknown_command\t");
  Serial.println(command_token);
  return CliDispatchResult::unknown_command;
}

void cli_initialize(void) {
  reset_baseline_cache();
  g_measurement_hooks = k_default_measurement_hooks;
  g_cli_ready         = true;
  Serial.println("phoenix-cli ready (commands: b, s, help)");
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
