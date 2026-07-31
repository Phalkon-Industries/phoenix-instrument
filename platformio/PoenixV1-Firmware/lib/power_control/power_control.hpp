#ifndef POWER_CONTROL_HPP
#define POWER_CONTROL_HPP

#include "phoenix_guard.hpp"
#include <stdint.h>

// Return codes surfaced by the power control helper.
#define POWER_CONTROL_OK PHX_OK
#define POWER_CONTROL_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define POWER_CONTROL_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define POWER_CONTROL_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED

/**
 * @brief Configuration for power domain control.
 */
struct PowerControlConfig {
  int power_enable_pin;      /**< GPIO that asserts the shared analog power rail when driven HIGH. */
  int neg_bias_shutdown_pin; /**< Active-low shutdown pin for the negative bias generator (set -1 when unused). */
  int indicator_red_pin;     /**< Optional indicator LED forced low during bring-up (set -1 when unused). */
  int indicator_blue_pin;    /**< Optional indicator LED forced low during bring-up (set -1 when unused). */
};

/**
 * @brief Power the analog domains.
 */
int power_control_prepare_power_domains(const PowerControlConfig* config);

/**
 * @brief Drop the shared power rail to place the analog front-end in a low-power state.
 */
int power_control_enter_low_power(void);

/**
 * @brief Assert a full shutdown, releasing cached state and powered peripherals.
 */
int power_control_shutdown(void);

/**
 * @brief Test-only hook that clears cached state between Unity cases.
 */
void power_control_reset_for_test(void);

/**
 * @brief Report whether the 5V rail and LM7705 negative-bias generator are both asserted.
 *
 * Returns true only after power_control_prepare_power_domains() has succeeded and before
 * power_control_enter_low_power() or power_control_shutdown() has dropped the rails.
 * LED consumers should gate their routing on this predicate so the hardware invariant
 * "LEDs never flash without both rails asserted" is enforced in firmware.
 */
bool power_control_led_power_is_ready(void);

#endif  // POWER_CONTROL_HPP
