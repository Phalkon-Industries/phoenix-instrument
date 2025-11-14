#ifndef LED_ROUTER_HPP
#define LED_ROUTER_HPP

#include "phoenix_guard.hpp"
#include <nrf_pwm.h>
#include <stdint.h>

// Return codes for the LED router helper.
#define LED_ROUTER_OK PHX_OK
#define LED_ROUTER_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define LED_ROUTER_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define LED_ROUTER_ERR_INVALID_STATE (PHX_ERR_MODULE_BASE - 10)

/**
 * @brief Logical routing states for the photodiode LED switch matrix.
 */
enum class LedRouterState : uint8_t {
  LED_ROUTER_STATE_OFF = 0u,
  LED_ROUTER_STATE_LED1,
  LED_ROUTER_STATE_LED2,
  LED_ROUTER_STATE_DRAIN,
};

/**
 * @brief Runtime configuration details for the LED router helper.
 */
struct LedRouterPwmConfig {
  bool          pwm_enabled;   ///< Set true to enable PWM control of the switch pins.
  NRF_PWM_Type* pwm_instance;  ///< NRF PWM instance that will drive the control lines when enabled.
};

/**
 * @brief Captures the PWM configuration programmed into the hardware for diagnostics.
 */
struct LedRouterPwmTestSnapshot {
  bool     pwm_configured;        ///< True when hardware playback is active.
  uint16_t countertop;            ///< Value written to COUNTERTOP (top of the PWM period).
  uint16_t channel0_level;        ///< Duty level programmed for channel 0 (IN1, 25% window).
  uint16_t channel1_level;        ///< Duty level programmed for channel 1 (IN2 base count before inversion).
  bool     channel1_is_inverted;  ///< True when the polarity bit is asserted for channel 1.
  uint32_t base_frequency_hz;     ///< Source clock frequency selected for the PWM instance.
};

struct LedRouterConfig {
  int                switch_in1_pin;
  int                switch_in2_pin;
  LedRouterPwmConfig pwm_config;  ///< PWM configuration to apply alongside the pin assignments.
};

/**
 * @brief Prepare the TS5A3359 control pins and record the runtime configuration.
 *
 * Call this once during system start before invoking `led_router_set_state`.
 * The implementation will drive the pins described by @p config to the
 * appropriate levels for the requested LED path.
 *
 * @param config Pointer to a configuration structure describing the control pins.
 * @return LED_ROUTER_OK on success or a negative error code on failure.
 */
int led_router_initialize(const LedRouterConfig* config);

/**
 * @brief Drive the switch to the requested logical state.
 *
 * @param state Requested routing state.
 * @return LED_ROUTER_OK on success, LED_ROUTER_ERR_NOT_INITIALIZED if the module has
 *         not been initialised.
 */
int led_router_set_state(LedRouterState state);

/**
 * @brief Report the last state commanded via `led_router_set_state`.
 *
 * @param state_out Destination pointer that receives the recorded state.
 * @return LED_ROUTER_OK on success, LED_ROUTER_ERR_NOT_INITIALIZED if the module
 *         has not been initialised, or LED_ROUTER_ERR_INVALID_ARG when @p state_out
 *         is NULL.
 */
int led_router_get_state(LedRouterState* state_out);

/**
 * @brief Park the switch in the off state and release internal state tracking.
 *
 * @return LED_ROUTER_OK on success, LED_ROUTER_ERR_NOT_INITIALIZED if called before
 *         `led_router_initialize`.
 */
int led_router_shutdown(void);

/**
 * @brief Reset internal state tracking. Intended for unit tests only.
 */
void led_router_reset_for_test(void);

/**
 * @brief Configure the PWM peripheral with an inverted 25% / 75% waveform.
 *
 * When PWM has been enabled in the router configuration this helper binds the
 * TS5A3359 control pins to the supplied NRF PWM instance, selects a prescaler
 * that honours @p minimum_period_us, and starts the hardware playback.
 *
 * @param minimum_period_us Minimum period in microseconds the waveform must accommodate.
 * @return LED_ROUTER_OK on success, LED_ROUTER_ERR_INVALID_ARG if PWM is disabled,
 *         configured with NULL resources, or @p minimum_period_us is zero, and
 *         LED_ROUTER_ERR_NOT_INITIALIZED if the router has not been initialised.
 */
int led_router_pwm_start(uint32_t minimum_period_us);

/**
 * @brief Halt PWM playback and release the associated hardware resources.
 *
 * @return LED_ROUTER_OK on success, LED_ROUTER_ERR_INVALID_ARG when PWM resources
 *         are not configured, and LED_ROUTER_ERR_NOT_INITIALIZED if invoked before
 *         `led_router_initialize`.
 */
int led_router_pwm_stop(void);

/**
 * @brief Populate a snapshot with the most recent PWM configuration.
 *
 * @param snapshot_out Destination pointer for the diagnostic data; ignored when NULL.
 */
void led_router_get_pwm_test_snapshot(LedRouterPwmTestSnapshot* snapshot_out);

#endif  // LED_ROUTER_HPP
