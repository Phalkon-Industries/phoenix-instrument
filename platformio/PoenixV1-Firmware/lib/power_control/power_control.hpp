#ifndef POWER_CONTROL_HPP
#define POWER_CONTROL_HPP

#include "adc_hal.hpp"
#include "led_router.hpp"
#include "phoenix_guard.hpp"
#include <stdint.h>

class TwoWire;

// Return codes surfaced by the power control helper.
#define POWER_CONTROL_OK PHX_OK
#define POWER_CONTROL_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define POWER_CONTROL_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define POWER_CONTROL_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED

/**
 * @brief Wiring and peripheral dependencies required to energise the analog front-end.
 */
struct PowerControlConfig {
  const LedRouterConfig* led_router_config; /**< Pin map used to initialise the LED router helper. */
  const AdcHalConfig*    adc_config;        /**< ADC HAL configuration describing SPI and IRQ wiring. */
  TwoWire*               wire_bus;          /**< I2C bus instance used to communicate with the digi-pot. */
  uint8_t                digipot_address;   /**< 7-bit address assigned to the AD524x device on this board. */
  int                    power_enable_pin;  /**< GPIO that asserts the shared analog power rail when driven HIGH. */
  int indicator_red_pin;  /**< Optional indicator LED forced low during bring-up (set -1 when unused). */
  int indicator_blue_pin; /**< Optional indicator LED forced low during bring-up (set -1 when unused). */
};

/**
 * @brief Power the analog domains and initialise peripheral drivers on demand.
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

#endif  // POWER_CONTROL_HPP
