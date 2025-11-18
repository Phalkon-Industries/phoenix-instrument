// clang-format off
#include "light_readings.hpp"
// clang-format on
#include "device_setup.hpp"
#include <Arduino.h>

namespace {

constexpr uint32_t k_pwm_sweep_count  = 500u;
constexpr uint32_t k_loop_delay_ms    = 1000u;
constexpr uint32_t k_serial_baud_rate = 115200u;
constexpr uint32_t k_serial_warmup_ms = 100u;
constexpr uint32_t k_serial_wait_ms   = 2000u;

LightReadingsSweepCollection g_sweep_collection = {0u, g_light_readings_sweep_storage};
LightReadingsSweepStats      g_sweep_stats      = {};

void halt_with_error(const __FlashStringHelper* label, int return_code) {
  Serial.print(label);
  Serial.print(F(": error="));
  Serial.println(return_code);
  while (true) {
    delay(1000);
  }
}

void wait_for_serial(void) {
  const uint32_t deadline_ms = millis() + k_serial_wait_ms;
  while (!Serial && (millis() < deadline_ms)) {
    delay(10);
  }
}

void print_summary_row(const char* label, const LightReadingsStatisticSummary& summary) {
  Serial.printf("%-13s %7lu %12.6f %12.6f %10ld %10ld\r\n", label, summary.sample_count, summary.mean,
                summary.standard_deviation, summary.min_value, summary.max_value);
}

}  // namespace

void setup() {
  // Step 1: Start the USB serial transport so logs can stream to the host.
  Serial.begin(k_serial_baud_rate);
  delay(k_serial_warmup_ms);
  wait_for_serial();
  Serial.println(F("Phoenix PWM sweep example"));

  // Step 2: Initialise power domains, ADC, LED router, and light readings helpers.
  int return_code = device_setup_initialize();
  if (return_code != LIGHT_READINGS_OK) {
    halt_with_error(F("device_setup_initialize failed"), return_code);
  }

  // Step 3: Start PWM playback; the sweep helper requires the waveform to be running already.
  const uint32_t minimum_period_us = g_device_light_readings_config.pwm_config.minimum_period_us;
  return_code                      = led_router_pwm_start(minimum_period_us);
  if (return_code != LED_ROUTER_OK) {
    halt_with_error(F("led_router_pwm_start failed"), return_code);
  }

  Serial.println(F("PWM playback active; entering sweep loop."));
}

void loop() {
  // Step 1: Reset the sweep collection and capture 500 PWM-synchronised samples.
  g_sweep_collection.sweep_count = 0u;
  const int return_code          = light_readings_pwm_sweep_n(k_pwm_sweep_count, &g_sweep_collection);
  if (return_code != LIGHT_READINGS_OK) {
    halt_with_error(F("light_readings_pwm_sweep_n failed"), return_code);
  }

  // Step 2: Derive summary statistics from the completed sweep batch.
  const int stats_return_code = light_readings_compute_sweep_stats(&g_sweep_collection, &g_sweep_stats);
  if (stats_return_code != LIGHT_READINGS_OK) {
    halt_with_error(F("light_readings_compute_sweep_stats failed"), stats_return_code);
  }

  Serial.println();
  Serial.print(F("Completed "));
  Serial.print(g_sweep_collection.sweep_count);
  Serial.println(F(" PWM sweeps."));
  Serial.println(F("channel        samples        mean      std_dev        min        max"));
  print_summary_row("drain_blue", g_sweep_stats.drain_blue);
  print_summary_row("drain_green", g_sweep_stats.drain_green);
  print_summary_row("blue", g_sweep_stats.blue);
  print_summary_row("green", g_sweep_stats.green);

  if (light_readings_last_sweep_detected_saturation()) {
    Serial.println(F("Warning: saturation detected during last sweep."));
  }

  // Step 3: Pause for one second before repeating the sweep batch.
  delay(k_loop_delay_ms);
}
