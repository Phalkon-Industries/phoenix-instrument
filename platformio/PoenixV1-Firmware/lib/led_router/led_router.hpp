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
  bool          pwm_enabled;
  NRF_PWM_Type* pwm_instance;
};

struct LedRouterPwmTestSnapshot {
  bool     pwm_configured;
  uint16_t countertop;
  uint16_t channel0_level;
  uint16_t channel1_level;
  bool     channel1_is_inverted;
  uint32_t base_frequency_hz;
};

struct LedRouterConfig {
  int                switch_in1_pin;
  int                switch_in2_pin;
  LedRouterPwmConfig pwm_config;
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

int led_router_pwm_configure(uint32_t minimum_period_us);

int led_router_pwm_stop(void);

void led_router_get_pwm_test_snapshot(LedRouterPwmTestSnapshot* snapshot_out);

#endif  // LED_ROUTER_HPP
