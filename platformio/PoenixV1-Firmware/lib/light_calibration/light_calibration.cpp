#include "light_calibration.hpp"

#include "ad524x.hpp"
#include "light_readings.hpp"
#include <cstdint>
#include <cstring>
#include <limits>

// ===================== Constants ================================================
// Saturation threshold: 90% of 24-bit signed ADC full scale (0.9 * 8,388,607).
static constexpr int32_t k_saturation_threshold = 7549746;

// Error messages returned on failure.
static constexpr const char* k_error_sweep_failed = "sweep measurement failed";
static constexpr const char* k_error_wiper_failed = "wiper setting failed";
static constexpr const char* k_error_stats_failed = "statistics computation failed";

// ===================== Default Configuration ====================================
const LightCalibrationConfig k_light_calibration_default_config = {
    .start_wiper          = 0u,
    .end_wiper            = 255u,
    .sweeps_per_wiper     = 5u,
    .saturation_threshold = k_saturation_threshold,
};

namespace {

// ===================== Test Hook State ==========================================
typedef int (*SweepRunner)(uint32_t, LightReadingsSweepCollection*);
typedef int (*WiperSetter)(uint8_t);

static SweepRunner g_sweep_runner = nullptr;
static WiperSetter g_wiper_setter = nullptr;

// ===================== Production Implementations ===============================

// Runs PWM sweeps using the light_readings module.
static int production_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  return light_readings_pwm_sweep_n(sweep_count, results_out);
}

// Sets both digipot wipers to the same value via light_readings runtime settings.
static int production_set_wiper(uint8_t wiper_code) {
  LightReadingsRuntimeSettings settings = {
      .apply_dwell_override = false,
      .dwell_us             = 0u,
      .apply_wiper_override = true,
      .wiper_code           = wiper_code,
  };
  return light_readings_modify_settings(&settings);
}

// ===================== Dispatch Helpers =========================================

// Returns the active sweep runner (test hook or production).
static SweepRunner get_sweep_runner(void) {
  return (g_sweep_runner != nullptr) ? g_sweep_runner : production_sweep_n;
}

// Returns the active wiper setter (test hook or production).
static WiperSetter get_wiper_setter(void) {
  return (g_wiper_setter != nullptr) ? g_wiper_setter : production_set_wiper;
}

// ===================== Sweep Analysis ===========================================

// Finds the maximum code in a sweep collection for a given channel.
static int32_t find_max_code(const LightReadingsSweepCollection* sweeps, bool is_blue) {
  if ((sweeps == nullptr) || (sweeps->sweeps == nullptr) || (sweeps->sweep_count == 0u)) {
    return 0;
  }

  int32_t max_code = std::numeric_limits<int32_t>::min();
  for (uint32_t i = 0u; i < sweeps->sweep_count; ++i) {
    const int32_t code = is_blue ? sweeps->sweeps[i].blue_code : sweeps->sweeps[i].green_code;
    if (code > max_code) {
      max_code = code;
    }
  }
  return max_code;
}

}  // namespace

LightCalibrationResult light_calibration_run(const LightCalibrationConfig* config) {
  return light_calibration_run_with_progress(config, nullptr);
}

LightCalibrationResult light_calibration_run_with_progress(const LightCalibrationConfig*    config,
                                                           LightCalibrationProgressCallback callback) {
  // Step 1: Use defaults if no config provided.
  LightCalibrationConfig effective_config = (config != nullptr) ? *config : k_light_calibration_default_config;

  // Step 2: Initialize result structure.
  LightCalibrationResult result = {};
  result.success                = false;
  result.blue_valid             = false;
  result.blue_wiper_code        = 0u;
  result.blue_max_code          = 0;
  result.green_valid            = false;
  result.green_wiper_code       = 0u;
  result.green_max_code         = 0;
  result.error_message          = nullptr;

  // Step 3: Allocate sweep storage on stack.
  static LightReadingsSweepSample sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT];
  LightReadingsSweepCollection    sweeps = {
      .sweep_count = 0u,
      .sweeps      = sweep_storage,
  };

  // Step 4: Track best (highest non-saturated) wiper per channel.
  int32_t blue_best_max  = std::numeric_limits<int32_t>::min();
  int32_t green_best_max = std::numeric_limits<int32_t>::min();

  // Step 5: Iterate through wiper range.
  const SweepRunner sweep_runner = get_sweep_runner();
  const WiperSetter wiper_setter = get_wiper_setter();

  for (uint32_t wiper = effective_config.start_wiper; wiper <= effective_config.end_wiper; ++wiper) {
    const uint8_t wiper_code = static_cast<uint8_t>(wiper & 0xFFu);

    // Step 5a: Set both wipers to current test value.
    const int wiper_result = wiper_setter(wiper_code);
    if (wiper_result != LIGHT_READINGS_OK) {
      result.error_message = k_error_wiper_failed;
      return result;
    }

    // Step 5b: Run PWM sweeps at this wiper setting.
    sweeps.sweep_count     = 0u;
    const int sweep_result = sweep_runner(effective_config.sweeps_per_wiper, &sweeps);
    if (sweep_result != LIGHT_READINGS_OK) {
      result.error_message = k_error_sweep_failed;
      return result;
    }

    // Step 5c: Find max codes for each channel.
    const int32_t blue_max  = find_max_code(&sweeps, true);
    const int32_t green_max = find_max_code(&sweeps, false);

    // Step 5d: Check saturation status.
    const bool blue_saturated  = (blue_max >= effective_config.saturation_threshold);
    const bool green_saturated = (green_max >= effective_config.saturation_threshold);

    // Step 5e: Update best wiper if this reading is valid and higher than previous best.
    if (!blue_saturated) {
      if (!result.blue_valid || (blue_max > blue_best_max)) {
        result.blue_valid      = true;
        result.blue_wiper_code = wiper_code;
        result.blue_max_code   = blue_max;
        blue_best_max          = blue_max;
      }
    }

    if (!green_saturated) {
      if (!result.green_valid || (green_max > green_best_max)) {
        result.green_valid      = true;
        result.green_wiper_code = wiper_code;
        result.green_max_code   = green_max;
        green_best_max          = green_max;
      }
    }

    // Step 5f: Report progress if callback provided.
    if (callback != nullptr) {
      callback(wiper_code, blue_max, green_max, blue_saturated, green_saturated);
    }
  }

  // Step 6: Mark calibration as successful.
  result.success = true;
  return result;
}

int32_t light_calibration_saturation_threshold(void) {
  return k_saturation_threshold;
}

void light_calibration_set_sweep_runner_for_test(int (*runner)(uint32_t, LightReadingsSweepCollection*)) {
  g_sweep_runner = runner;
}

void light_calibration_set_wiper_setter_for_test(int (*setter)(uint8_t)) {
  g_wiper_setter = setter;
}

void light_calibration_clear_test_hooks(void) {
  g_sweep_runner = nullptr;
  g_wiper_setter = nullptr;
}
