
#ifndef LIGHT_READINGS_HPP
#define LIGHT_READINGS_HPP

#include "adc_hal.hpp"
#include "led_router.hpp"
#include "phoenix_guard.hpp"
#include <stdint.h>

// Return codes surfaced by the light readings helper.
#define LIGHT_READINGS_OK PHX_OK
#define LIGHT_READINGS_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define LIGHT_READINGS_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED (PHX_ERR_MODULE_BASE - 1)
#define LIGHT_READINGS_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED

// Default sweep capacity used when callers do not provide an override at build time.
#ifndef LIGHT_READINGS_MAX_SWEEP_COUNT
#define LIGHT_READINGS_MAX_SWEEP_COUNT 16u
#endif

/**
 * @brief Logical photodiode channels supported by the light readings helper.
 */
enum class LightReadingsChannel : uint8_t {
  LIGHT_READINGS_CHANNEL_A = 0u,
  LIGHT_READINGS_CHANNEL_B,
};

/**
 * @brief Sweep phases executed when collecting a complete light readings sample.
 */
enum class LightReadingsSweepPhase : uint8_t {
  LIGHT_READINGS_SWEEP_PHASE_DRAIN = 0u,
  LIGHT_READINGS_SWEEP_PHASE_CHANNEL_A,
  LIGHT_READINGS_SWEEP_PHASE_CHANNEL_B,
};

/**
 * @brief Configuration required to route LEDs and sample the ADC.
 */
struct LightReadingsConfig {
  LedRouterState drain_state;     /**< Router state that exposes the shared drain. */
  LedRouterState channel_a_state; /**< Router state used when sampling channel A directly. */
  LedRouterState channel_b_state; /**< Router state used when sampling channel B directly. */
  AdcHalChannel  channel_a_adc;   /**< ADC channel wired to photodiode A. */
  AdcHalChannel  channel_b_adc;   /**< ADC channel wired to photodiode B. */
  uint32_t       adc_timeout_us;  /**< Timeout passed to adc_hal reads. */
};

/**
 * @brief Raw ADC codes captured for a single sweep.
 */
struct LightReadingsSweepSample {
  int32_t drain_channel_a_code; /**< Drain-state reading taken on photodiode A. */
  int32_t drain_channel_b_code; /**< Drain-state reading taken on photodiode B. */
  int32_t channel_a_code;       /**< Direct reading taken with channel A routed. */
  int32_t channel_b_code;       /**< Direct reading taken with channel B routed. */
};

/**
 * @brief Fixed-capacity buffer used to accumulate multiple sweep samples.
 */
struct LightReadingsSweepCollection {
  uint32_t                 sweep_count; /**< Number of valid sweep entries populated in @p sweeps. */
  LightReadingsSweepSample sweeps[LIGHT_READINGS_MAX_SWEEP_COUNT];
};

/**
 * @brief Prepare the light readings helper for use.
 *
 * @param config Configuration describing router states, ADC channels, and timing.
 * @return LIGHT_READINGS_OK on success, or a negative error code on failure.
 */
int light_readings_initialize(const LightReadingsConfig* config);

/**
 * @brief Sample the requested photodiode and return the raw ADC code.
 *
 * @param channel Logical photodiode channel to measure.
 * @param code_out Destination pointer that receives the raw ADC reading.
 * @return LIGHT_READINGS_OK on success, or a propagated error code from adc_hal or led_router.
 */
int light_readings_read_channel(LightReadingsChannel channel, int32_t* code_out);

/**
 * @brief Execute a full drain / channel A / channel B sweep and capture the results.
 *
 * @param result_out Destination pointer populated with the raw ADC codes collected for each phase.
 * @return LIGHT_READINGS_OK on success, or a propagated error code from adc_hal or led_router.
 */
int light_readings_sweep(LightReadingsSweepSample* result_out);

/**
 * @brief Perform multiple sweeps back-to-back, storing the results in a fixed-capacity buffer.
 *
 * @param sweep_count Number of sweeps to execute.
 * @param results_out Pointer to a statically allocated collection that receives the sweep data.
 * @return LIGHT_READINGS_OK when all sweeps complete, LIGHT_READINGS_ERR_INVALID_ARG for null pointers,
 *         LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED when @p sweep_count exceeds the buffer capacity,
 *         or a propagated error code from dependent modules.
 */
int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out);

/**
 * @brief Release internal state and park the router in a safe configuration.
 */
int light_readings_shutdown(void);

/**
 * @brief Unit-test hook that clears cached state and counters.
 */
void light_readings_reset_for_test(void);

#endif  // LIGHT_READINGS_HPP