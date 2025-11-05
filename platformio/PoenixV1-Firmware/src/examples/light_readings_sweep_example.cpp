// clang-format off
#include "light_readings.hpp"
// clang-format on
#include "device_setup.hpp"
#include <Arduino.h>

static constexpr uint32_t k_sweep_count = 500u;

static LightReadingsSweepCollection g_sweep_collection = {0u, g_light_readings_sweep_storage};
static LightReadingsSweepStats      g_sweep_stats      = {};

static void halt_with_error(const __FlashStringHelper* label, int return_code) {
  Serial.print(label);
  Serial.print(F(": error="));
  Serial.println(return_code);
  while (true) {
    delay(1000);
  }
}

static void print_summary_row(const __FlashStringHelper* label, const LightReadingsStatisticSummary& summary) {
  Serial.print(label);
  Serial.print('\t');
  Serial.print(summary.sample_count);
  Serial.print('\t');
  Serial.print(summary.mean, 6);
  Serial.print('\t');
  Serial.print(summary.standard_deviation, 6);
  Serial.print('\t');
  Serial.print(summary.min_value);
  Serial.print('\t');
  Serial.println(summary.max_value);
}

void setup() {
  // Step 1: Start the USB serial interface so sweep logs stream to the host.
  Serial.begin(115200);

  delay(100);

  Serial.println(F("Phoenix Light Readings Sweep Example"));

  // Step 2: Energise shared power domains and prime the light readings helper.
  const int return_code = device_setup_initialize();
  if (return_code != LIGHT_READINGS_OK) {
    halt_with_error(F("device_setup_initialize failed"), return_code);
  }

  Serial.println(F("Initialisation complete; starting sweep loop."));
}

void loop() {
  // Step 1: Reset the sweep collection and run a fixed-count acquisition.
  g_sweep_collection.sweep_count = 0u;
  int return_code                = light_readings_sweep_n(k_sweep_count, &g_sweep_collection);
  if (return_code != LIGHT_READINGS_OK) {
    halt_with_error(F("light_readings_sweep_n failed"), return_code);
  }

  // Step 2: Compute sweep statistics so the log focuses on aggregate behaviour.
  return_code = light_readings_compute_sweep_stats(&g_sweep_collection, &g_sweep_stats);
  if (return_code != LIGHT_READINGS_OK) {
    halt_with_error(F("light_readings_compute_sweep_stats failed"), return_code);
  }

  // Step 3: Emit a tab-delimited summary for downstream inspection.
  Serial.println();
  Serial.println(F("channel\tsamples\tmean\tstd_dev\tmin\tmax"));
  print_summary_row(F("drain_blue"), g_sweep_stats.drain_blue);
  print_summary_row(F("drain_green"), g_sweep_stats.drain_green);
  print_summary_row(F("blue"), g_sweep_stats.blue);
  print_summary_row(F("green"), g_sweep_stats.green);

  // Step 4: Flag when any sweep reached ADC saturation so behaviour changes are obvious in logs.
  if (light_readings_last_sweep_detected_saturation()) {
    Serial.println(F("Warning: saturation detected in last sweep batch."));
  }

  // Step 5: Yield briefly so the USB CDC stack can drain the transmit buffer.
  delay(10);
}
