
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
#define LIGHT_READINGS_ERR_PWM_DISABLED (PHX_ERR_MODULE_BASE - 2)
#define LIGHT_READINGS_ERR_PWM_NOT_CONFIGURED (PHX_ERR_MODULE_BASE - 3)
#define LIGHT_READINGS_ERR_PWM_UNSUPPORTED_INSTANCE (PHX_ERR_MODULE_BASE - 4)
#define LIGHT_READINGS_ERR_PWM_NOT_RUNNING (PHX_ERR_MODULE_BASE - 5)
#define LIGHT_READINGS_ERR_POWER_NOT_READY (PHX_ERR_MODULE_BASE - 6)
#define LIGHT_READINGS_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED
#define LIGHT_READINGS_ERR_TIMEOUT PHX_ERR_TIMEOUT

// Default sweep capacity used when callers do not provide an override at build time.
#ifndef LIGHT_READINGS_MAX_SWEEP_COUNT
#define LIGHT_READINGS_MAX_SWEEP_COUNT 500u
#endif

/**
 * @brief Logical photodiode channels supported by the light readings helper.
 */
enum class LightReadingsChannel : uint8_t {
  LIGHT_READINGS_CHANNEL_BLUE = 0u,
  LIGHT_READINGS_CHANNEL_GREEN,
};

/**
 * @brief Per-colour routing, ADC, and digipot configuration.
 */
struct LightReadingsChannelConfig {
  LedRouterState router_state; /**< Router state used when sampling this colour directly. */
  AdcHalChannel  adc_channel;  /**< ADC channel wired to this photodiode. */
  uint32_t       dwell_us;     /**< Delay applied after routing before sampling. */
  uint8_t        wiper_code;   /**< Digipot wiper code applied during initialisation. */
};

/**
 * @brief Configuration describing the PWM resources used to drive sweep timing.
 */
struct LightReadingsPwmConfig {
  bool          pwm_enabled;       /**< Enables PWM-driven sweep timing when true. */
  NRF_PWM_Type* pwm_instance;      /**< PWM instance that owns the router pins during playback. */
  int           router_in1_pin;    /**< Board pin routed to TS5A3359 IN1. */
  int           router_in2_pin;    /**< Board pin routed to TS5A3359 IN2. */
  uint32_t      minimum_period_us; /**< Minimum PWM period requested when starting playback. */
  uint32_t      period_timeout_us; /**< Timeout applied when waiting for PWM edges or periods. */
};

/**
 * @brief Configuration required to route LEDs and sample the ADC.
 */
struct LightReadingsConfig {
  LedRouterState             drain_state;    /**< Router state that exposes the shared drain. */
  LightReadingsChannelConfig green_channel;  /**< Runtime configuration for the green photodiode. */
  LightReadingsChannelConfig blue_channel;   /**< Runtime configuration for the blue photodiode. */
  uint32_t                   adc_timeout_us; /**< Timeout passed to adc_hal reads. */
  LightReadingsPwmConfig     pwm_config;     /**< PWM resources used when executing timed sweeps. */
};

/**
 * @brief Runtime overrides applied after the helper has been initialised.
 */
struct LightReadingsRuntimeSettings {
  bool     apply_dwell_override; /**< When true, updates both channel dwell timings. */
  uint32_t dwell_us;             /**< Replacement dwell interval expressed in microseconds. */
  bool     apply_wiper_override; /**< When true, applies a new digipot wiper code to both LEDs. */
  uint8_t  wiper_code;           /**< Digipot wiper code routed to each colour when overriding. */
};

/**
 * @brief Raw ADC codes captured for a single sweep.
 */
struct LightReadingsSweepSample {
  int32_t drain_blue_code;  /**< Drain-state reading taken on the blue photodiode. */
  int32_t drain_green_code; /**< Drain-state reading taken on the green photodiode. */
  int32_t blue_code;        /**< Direct reading taken with the blue channel routed. */
  int32_t green_code;       /**< Direct reading taken with the green channel routed. */
};

/**
 * @brief Fixed-capacity buffer used to accumulate multiple sweep samples.
 */
struct LightReadingsSweepCollection {
  uint32_t                  sweep_count; /**< Number of valid sweep entries populated in @p sweeps. */
  LightReadingsSweepSample* sweeps;      /**< Pointer to sweep storage allocated by the caller. */
};

/**
 * @brief Global sweep storage used when callers do not provide their own backing buffer.
 *
 * Applications that cannot dedicate static storage elsewhere may bind @ref LightReadingsSweepCollection::sweeps to
 * this array before invoking @ref light_readings_sweep_n.
 */
extern LightReadingsSweepSample g_light_readings_sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT];

/**
 * @brief Aggregated statistics derived from a sequence of sweep samples.
 */
struct LightReadingsStatisticSummary {
  uint32_t sample_count;       /**< Number of samples contributing to the metrics. */
  double   mean;               /**< Sample mean computed across captured values. */
  double   standard_deviation; /**< Sample standard deviation computed across values. */
  int32_t  min_value;          /**< Minimum observed code. */
  int32_t  max_value;          /**< Maximum observed code. */
  double   drift_slope;        /**< Least-squares slope across the sweep index domain. */
  bool     has_samples;        /**< Indicates whether the metrics represent at least one sample. */
};

/**
 * @brief Per-channel statistics summarizing an @ref LightReadingsSweepCollection.
 */
struct LightReadingsSweepStats {
  uint32_t                      sweep_count; /**< Number of sweeps represented by the statistics. */
  LightReadingsStatisticSummary drain_blue;  /**< Drain measurement captured on the blue photodiode. */
  LightReadingsStatisticSummary drain_green; /**< Drain measurement captured on the green photodiode. */
  LightReadingsStatisticSummary blue;        /**< Direct measurement with the blue photodiode routed. */
  LightReadingsStatisticSummary green;       /**< Direct measurement with the green photodiode routed. */
};

/**
 * @brief Diagnostic counters captured while PWM-driven sweeps execute.
 */
struct LightReadingsPwmDiagnostics {
  uint32_t sample_period_count;    /**< Number of PWM period completions observed. */
  uint32_t green_conversion_count; /**< Successful green-channel conversion starts. */
  uint32_t blue_conversion_count;  /**< Successful blue-channel conversion starts. */
  uint32_t drain_read_count;       /**< Drain-state dual-channel measurements captured. */
  uint32_t missed_drdy_count;      /**< Conversions that failed to produce DRDY before timing gate. */
};

/**
 * @brief Prepare the light readings helper for use.
 *
 * @param config Configuration describing router states, ADC channels, and timing.
 * @return LIGHT_READINGS_OK on success, or a negative error code on failure.
 */
int light_readings_initialize(const LightReadingsConfig* config);

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
 * @param results_out Pointer to a collection that references statically allocated sweep storage via its @p sweeps
 * field.
 * @return LIGHT_READINGS_OK when all sweeps complete, LIGHT_READINGS_ERR_INVALID_ARG for null pointers,
 *         LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED when @p sweep_count exceeds the buffer capacity,
 *         or a propagated error code from dependent modules.
 */
int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out);

/**
 * @brief Calculate per-channel statistics from a collection of sweep samples.
 *
 * @param sweep_collection Collection of sweep samples previously captured via @ref light_readings_sweep_n. The
 *        @p sweeps pointer must reference valid storage when @p sweep_collection->sweep_count exceeds zero.
 * @param stats_out Destination pointer populated with aggregate metrics for each sweep field.
 * @return LIGHT_READINGS_OK when statistics are computed, LIGHT_READINGS_ERR_INVALID_ARG when pointers are null.
 */
int light_readings_compute_sweep_stats(const LightReadingsSweepCollection* sweep_collection,
                                       LightReadingsSweepStats*            stats_out);

/**
 * @brief Report whether the most recent sweep detected ADC saturation.
 */
bool light_readings_last_sweep_detected_saturation(void);

/**
 * @brief Apply runtime overrides to dwell timing and wiper settings without reinitialising hardware.
 */
int light_readings_modify_settings(const LightReadingsRuntimeSettings* settings);

/**
 * @brief Release internal state and park the router in a safe configuration.
 */
int light_readings_shutdown(void);

/**
 * @brief Unit-test hook that clears cached state and counters.
 */
void light_readings_reset_for_test(void);

void light_readings_force_saturation_for_test(bool enabled);

void light_readings_get_config_for_test(LightReadingsConfig* config_out);

void light_readings_pwm_force_timeout_for_test(bool enabled);

/**
 * @brief Execute a PWM-driven sweep sequence.
 *
 * Callers must start the configured PWM instance before invoking this helper; the function only observes the existing
 * waveform and never starts or stops playback. The router remains under PWM control for the duration of the call.
 *
 * @param sweep_count Number of PWM periods / samples to capture.
 * @param results_out Destination collection that receives buffered samples.
 * @return LIGHT_READINGS_OK on success.
 *         LIGHT_READINGS_ERR_PWM_DISABLED when PWM support is disabled in the cached configuration.
 *         LIGHT_READINGS_ERR_PWM_NOT_CONFIGURED when required PWM metadata is missing or invalid.
 *         LIGHT_READINGS_ERR_PWM_UNSUPPORTED_INSTANCE when the cached instance is not NRF_PWM3.
 *         LIGHT_READINGS_ERR_PWM_NOT_RUNNING when the PWM instance is idle.
 *         LIGHT_READINGS_ERR_TIMEOUT when period tracking or ADC capture times out.
 */
int light_readings_pwm_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out);

/**
 * @brief Reset accumulated diagnostics for PWM-driven sweeps.
 */
void light_readings_pwm_reset_for_test(void);

/**
 * @brief Retrieve diagnostic counters captured by PWM-driven sweeps.
 */
void light_readings_pwm_get_diagnostics(LightReadingsPwmDiagnostics* diagnostics_out);

#endif  // LIGHT_READINGS_HPP