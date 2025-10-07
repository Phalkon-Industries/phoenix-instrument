#include "led_router.hpp"

#include <Arduino.h>

namespace {

LedRouterConfig g_router_config  = {0, 0};
LedRouterState  g_current_state  = LedRouterState::LED_ROUTER_STATE_OFF;
bool            g_is_initialized = false;

bool is_valid_state(LedRouterState state) {
  switch (state) {
    case LedRouterState::LED_ROUTER_STATE_OFF:
    case LedRouterState::LED_ROUTER_STATE_LED1:
    case LedRouterState::LED_ROUTER_STATE_LED2:
    case LedRouterState::LED_ROUTER_STATE_DRAIN:
      return true;
    default:
      return false;
  }
}

void apply_state_to_pins(LedRouterState state) {
  // Avoid redundant writes that would repeatedly toggle the TS5A3359 control pins.
  if (g_current_state == state) {
    return;
  }

  int pin_in1_level = LOW;
  int pin_in2_level = LOW;

  // TS5A3359 truth table: IN1/IN2 -> OFF (0/0), LED1 (1/0), LED2 (0/1), DRAIN (1/1).
  switch (state) {
    case LedRouterState::LED_ROUTER_STATE_OFF:
      pin_in1_level = LOW;
      pin_in2_level = LOW;
      break;
    case LedRouterState::LED_ROUTER_STATE_LED1:
      pin_in1_level = HIGH;
      pin_in2_level = LOW;
      break;
    case LedRouterState::LED_ROUTER_STATE_LED2:
      pin_in1_level = LOW;
      pin_in2_level = HIGH;
      break;
    case LedRouterState::LED_ROUTER_STATE_DRAIN:
      pin_in1_level = HIGH;
      pin_in2_level = HIGH;
      break;
    default:
      // This branch is unreachable because callers validate the state.
      break;
  }

  digitalWrite(g_router_config.switch_in1_pin, pin_in1_level);
  digitalWrite(g_router_config.switch_in2_pin, pin_in2_level);
  g_current_state = state;
}

}  // namespace

int led_router_initialize(const LedRouterConfig* config) {
  if (config == NULL) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  if (config->switch_in1_pin == config->switch_in2_pin) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  pinMode(config->switch_in1_pin, OUTPUT);
  pinMode(config->switch_in2_pin, OUTPUT);

  g_router_config  = *config;
  g_is_initialized = true;
  apply_state_to_pins(LedRouterState::LED_ROUTER_STATE_DRAIN);

  return LED_ROUTER_OK;
}

int led_router_set_state(LedRouterState state) {
  if (!g_is_initialized) {
    return LED_ROUTER_ERR_NOT_INITIALIZED;
  }

  if (!is_valid_state(state)) {
    return LED_ROUTER_ERR_INVALID_STATE;
  }

  apply_state_to_pins(state);
  return LED_ROUTER_OK;
}

int led_router_get_state(LedRouterState* state_out) {
  if (state_out == NULL) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  if (!g_is_initialized) {
    return LED_ROUTER_ERR_NOT_INITIALIZED;
  }

  *state_out = g_current_state;
  return LED_ROUTER_OK;
}

int led_router_shutdown(void) {
  if (!g_is_initialized) {
    return LED_ROUTER_ERR_NOT_INITIALIZED;
  }

  digitalWrite(g_router_config.switch_in1_pin, LOW);
  digitalWrite(g_router_config.switch_in2_pin, LOW);

  g_is_initialized = false;
  g_current_state  = LedRouterState::LED_ROUTER_STATE_OFF;
  return LED_ROUTER_OK;
}

void led_router_reset_for_test(void) {
  g_is_initialized               = false;
  g_router_config.switch_in1_pin = 0;
  g_router_config.switch_in2_pin = 0;
  g_current_state                = LedRouterState::LED_ROUTER_STATE_OFF;
}
