#include "device_setup.hpp"
#include "led_router.hpp"
#include <Arduino.h>
#include <nrf_pwm.h>

namespace {

constexpr uint32_t k_pwm_minimum_period_us = 3000u;
constexpr uint16_t k_state_dwell_ms        = 750u;

LedRouterPwmConfig g_pwm_config    = {true, NRF_PWM3};
LedRouterConfig    g_router_config = {TS5A3359_IN1, TS5A3359_IN2, g_pwm_config};

void trap_on_error(int return_code) {
  if (return_code == LED_ROUTER_OK) {
    return;
  }

  // Step 1: Halt execution so the debugger can inspect the failing return code.
  while (true) {
    delay(1000);
  }
}

}  // namespace

void setup() {
  // Step 1: Initialise the board wiring so the router pins are configured for output.
  device_setup_initialize();

  // Step 2: Bring the LED router online and apply the PWM configuration helper.
  trap_on_error(led_router_initialize(&g_router_config));
  trap_on_error(led_router_pwm_configure(k_pwm_minimum_period_us));

  // Step 3: Default to the drain state so both LEDs discharge between active windows.
  trap_on_error(led_router_set_state(LedRouterState::LED_ROUTER_STATE_DRAIN));
}

void loop() {
  // Step 1: Drive LED1 while PWM inverts IN2 to maintain the 25% / 75% waveform split.
  trap_on_error(led_router_set_state(LedRouterState::LED_ROUTER_STATE_LED1));
  delay(k_state_dwell_ms);

  // Step 2: Return to the drain path to clear residual charge before switching.
  trap_on_error(led_router_set_state(LedRouterState::LED_ROUTER_STATE_DRAIN));
  delay(k_state_dwell_ms);

  // Step 3: Drive LED2 using the same PWM waveform and hold briefly.
  trap_on_error(led_router_set_state(LedRouterState::LED_ROUTER_STATE_LED2));
  delay(k_state_dwell_ms);

  // Step 4: Park back in the drain state to complete the cycle.
  trap_on_error(led_router_set_state(LedRouterState::LED_ROUTER_STATE_DRAIN));
  delay(k_state_dwell_ms);
}
