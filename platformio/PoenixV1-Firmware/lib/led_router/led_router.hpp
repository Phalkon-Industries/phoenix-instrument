#ifndef LED_ROUTER_HPP
#define LED_ROUTER_HPP

#include <stdint.h>

// Return codes for the LED router helper.
#define LED_ROUTER_OK 0
#define LED_ROUTER_ERR_INVALID_ARG -1
#define LED_ROUTER_ERR_NOT_INITIALIZED -2
#define LED_ROUTER_ERR_INVALID_STATE -3

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
struct LedRouterConfig {
  int switch_in1_pin;
  int switch_in2_pin;
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

#endif  // LED_ROUTER_HPP
