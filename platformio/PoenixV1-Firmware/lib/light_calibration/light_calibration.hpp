#ifndef LIGHT_CALIBRATION_HPP
#define LIGHT_CALIBRATION_HPP

#include "light_readings.hpp"
#include "phoenix_guard.hpp"
#include <stdint.h>

// ===================== Return Codes =============================================
#define LIGHT_CALIBRATION_OK PHX_OK
#define LIGHT_CALIBRATION_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define LIGHT_CALIBRATION_ERR_HARDWARE PHX_ERR_COMMUNICATION
#define LIGHT_CALIBRATION_ERR_SWEEP_FAILED PHX_ERR_TIMEOUT

/**
 * @brief Configuration for a light calibration run.
 *
 * Controls the wiper range to sweep and measurement parameters. Use
 * k_light_calibration_default_config for typical operation.
 */
struct LightCalibrationConfig {
  uint16_t start_wiper;          /**< First wiper code to test (inclusive). */
  uint16_t end_wiper;            /**< Last wiper code to test (inclusive). */
  uint32_t sweeps_per_wiper;     /**< Number of PWM sweeps per wiper position. */
  int32_t  saturation_threshold; /**< ADC code ceiling; codes at or above are saturated. */
};

/**
 * @brief Result from a calibration run.
 *
 * Contains per-channel recommendations and diagnostic information. The
 * recommended wiper is the highest value that produced ADC codes below
 * the saturation threshold.
 */
struct LightCalibrationResult {
  bool        success;          /**< True if calibration completed without errors. */
  bool        blue_valid;       /**< True if a valid (non-saturated) blue wiper was found. */
  uint16_t    blue_wiper_code;  /**< Recommended blue wiper (highest below threshold). */
  int32_t     blue_max_code;    /**< Max ADC code observed at recommended blue wiper. */
  bool        green_valid;      /**< True if a valid (non-saturated) green wiper was found. */
  uint16_t    green_wiper_code; /**< Recommended green wiper (highest below threshold). */
  int32_t     green_max_code;   /**< Max ADC code observed at recommended green wiper. */
  const char* error_message;    /**< Null on success; points to error string on failure. */
};

/**
 * @brief Progress callback invoked during calibration.
 *
 * @param wiper_code Current wiper being tested.
 * @param blue_max   Max blue ADC code at this wiper.
 * @param green_max  Max green ADC code at this wiper.
 * @param blue_sat   True if blue channel is saturated.
 * @param green_sat  True if green channel is saturated.
 */
typedef void (*LightCalibrationProgressCallback)(uint16_t wiper_code, int32_t blue_max, int32_t green_max, bool blue_sat,
                                                 bool green_sat);

/**
 * @brief Default configuration for typical calibration runs.
 *
 * Sweeps all 256 wiper values with 5 PWM sweeps each, using the standard
 * 90% saturation threshold.
 */
extern const LightCalibrationConfig k_light_calibration_default_config;

/**
 * @brief Run a light calibration sweep.
 *
 * Iterates through the configured wiper range, measuring ADC codes at each
 * position. Tracks the highest wiper code per channel that produces readings
 * below the saturation threshold.
 *
 * @param config Configuration controlling sweep parameters, or NULL for defaults.
 * @return Result struct with recommendations and error status.
 */
LightCalibrationResult light_calibration_run(const LightCalibrationConfig* config);

/**
 * @brief Run calibration with progress reporting.
 *
 * Same as light_calibration_run but invokes the callback after each wiper
 * measurement. Useful for CLI output during long sweeps.
 *
 * @param config   Configuration controlling sweep parameters, or NULL for defaults.
 * @param callback Progress callback invoked per wiper, or NULL to skip.
 * @return Result struct with recommendations and error status.
 */
LightCalibrationResult light_calibration_run_with_progress(const LightCalibrationConfig*    config,
                                                           LightCalibrationProgressCallback callback);

/**
 * @brief Get the saturation threshold used by calibration.
 *
 * @return ADC code corresponding to 90% of 24-bit full scale (~7,549,746).
 */
int32_t light_calibration_saturation_threshold(void);

// ===================== Test Hooks ===============================================

/**
 * @brief Override the sweep runner for testing.
 *
 * @param runner Replacement function, or NULL to restore production implementation.
 */
void light_calibration_set_sweep_runner_for_test(int (*runner)(uint32_t, LightReadingsSweepCollection*));

/**
 * @brief Override the wiper setter for testing.
 *
 * @param setter Replacement function, or NULL to restore production implementation.
 */
void light_calibration_set_wiper_setter_for_test(int (*setter)(uint16_t));

/**
 * @brief Restore all production implementations.
 */
void light_calibration_clear_test_hooks(void);

#endif  // LIGHT_CALIBRATION_HPP
