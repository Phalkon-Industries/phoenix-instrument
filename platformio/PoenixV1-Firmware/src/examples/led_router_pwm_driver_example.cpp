#include "device_setup.hpp"
#include "led_router.hpp"
#include <Arduino.h>

namespace {

constexpr uint32_t k_pwm_minimum_period_us = 3000u;

}  // namespace

void setup() {
  // Step 1: Initialise the board wiring so the router pins are configured for output
  (void) device_setup_initialize();

  // Step 2: Bring the LED router online and park in the drain state before enabling PWM.
  (void) led_router_initialize(&g_device_led_router_config);
  (void) led_router_set_state(LedRouterState::LED_ROUTER_STATE_DRAIN);

  // Step 3: Start PWM playback; the hardware now owns the switch lines.
  (void) led_router_pwm_start(k_pwm_minimum_period_us);
}

void loop() {
  // Step 1: Allow the PWM peripheral to run autonomously.
  delay(1000);
}
