#include "power_control.hpp"

#include "ad524x.hpp"
#include <Arduino.h>
#include <Wire.h>

namespace {

// Cached bring-up state so repeated calls avoid reinitialising hardware components unnecessarily.
PowerControlConfig g_power_config              = {};
bool               g_is_initialized            = false;
bool               g_power_domains_energised   = false;
bool               g_indicator_leds_configured = false;
bool               g_led_router_ready          = false;
bool               g_adc_ready                 = false;
bool               g_digipot_ready             = false;
bool               g_neg_bias_generator_on     = false;

// Force indicator pins low so measurements happen without stray LED current.
void configure_indicator_pin(int pin) {
  if (pin < 0) {
    return;
  }
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

// Drive the active-low shutdown pin so the negative bias generator turns on.
void enable_negative_bias_generator(void) {
  if (g_power_config.neg_bias_shutdown_pin < 0) {
    g_neg_bias_generator_on = false;
    return;
  }
  if (g_neg_bias_generator_on) {
    return;
  }
  pinMode(g_power_config.neg_bias_shutdown_pin, OUTPUT);
  digitalWrite(g_power_config.neg_bias_shutdown_pin, LOW);
  g_neg_bias_generator_on = true;
}

// Force the shutdown pin high so the negative bias generator stops sourcing current.
void disable_negative_bias_generator(void) {
  if (g_power_config.neg_bias_shutdown_pin < 0) {
    g_neg_bias_generator_on = false;
    return;
  }
  if (!g_neg_bias_generator_on) {
    return;
  }
  digitalWrite(g_power_config.neg_bias_shutdown_pin, HIGH);
  g_neg_bias_generator_on = false;
}

// Raise the shared analog rail, accounting for boards that omit a dedicated enable GPIO.
void drive_5v_power_enable_high(void) {
  if (g_power_config.power_enable_pin < 0) {
    g_power_domains_energised = false;
    return;
  }
  pinMode(g_power_config.power_enable_pin, OUTPUT);
  digitalWrite(g_power_config.power_enable_pin, HIGH);
  g_power_domains_energised = true;
}

// Drop the shared analog rail so downstream peripherals enter a low-power state.
void drive_power_enable_low(void) {
  if (g_power_config.power_enable_pin < 0) {
    g_power_domains_energised = false;
    return;
  }
  digitalWrite(g_power_config.power_enable_pin, LOW);
  g_power_domains_energised = false;
}

}  // namespace

int power_control_prepare_power_domains(const PowerControlConfig* config) {
  // Step 1: Reject calls that forget to provide a configuration structure.
  GUARD_NONNULL(config);

  g_power_config = *config;

  // Step 2: Enable the shared rail before toggling any dependent peripherals.
  /* The delays are vitally important and allow the 5V to stabilize before continuing on.
    Without them, the system becomes very unstable and the light readings unusable since a big
    sawtooth wave dominates the signal.
  */
  delay(5);
  drive_5v_power_enable_high();
  delay(5);

  // Step 2.5: Assert the active-low shutdown pin so the negative bias generator wakes up cleanly.
  enable_negative_bias_generator();

  // Step 3: Pull the indicator LEDs dark exactly once per boot so sweeps start from a quiet baseline.
  if (!g_indicator_leds_configured) {
    configure_indicator_pin(config->indicator_red_pin);
    configure_indicator_pin(config->indicator_blue_pin);
    g_indicator_leds_configured = true;
  }

  // Step 4: Bring up the digipot driver if the board has not initialised it already.
  if (!g_digipot_ready) {
    if (!ad524x_is_initialized()) {
      GUARD_NONNULL(config->wire_bus);
      config->wire_bus->begin();
      GUARD(ad524x_initialize(config->digipot_address, config->wire_bus));
    }
    g_digipot_ready = true;
  }

  // Step 5: Initialise the LED router helper on first use so routes can be commanded safely.
  if (!g_led_router_ready) {
    GUARD_NONNULL(config->led_router_config);
    GUARD(led_router_initialize(config->led_router_config));
    g_led_router_ready = true;
  }

  // Step 6: Initialise the ADC HAL and apply defaults before any conversions run.
  if (!g_adc_ready) {
    GUARD_NONNULL(config->adc_config);
    GUARD(adc_hal_initialize(config->adc_config));
    GUARD(adc_hal_apply_default_configuration());
    g_adc_ready = true;
  }

  // Step 7: Record that the helper is live so low-power and shutdown calls can enforce ordering.
  g_is_initialized = true;

  return POWER_CONTROL_OK;
}

int power_control_enter_low_power(void) {
  // Step 1: Require the helper to be initialised before manipulating shared rails.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 2: Drop the power rail to place downstream hardware in a reduced-power state.
  drive_power_enable_low();

  // Step 3: Latch the negative bias generator off so the active-low pin idles high.
  disable_negative_bias_generator();

  return POWER_CONTROL_OK;
}

int power_control_shutdown(void) {
  // Step 1: Ensure shutdown only occurs after the helper has configured its dependencies.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 2: Immediately drop the rail before deinitialising peripherals.
  drive_power_enable_low();

  // Step 3: Shut down the negative bias generator so analog domains fully discharge.
  disable_negative_bias_generator();

  // Step 4: Release LED routing resources so future tests see a cold-started helper.
  if (g_led_router_ready) {
    GUARD(led_router_shutdown());
    g_led_router_ready = false;
  }

  // Step 5: Ask the ADC to shut down completely so subsequent runs reapply configuration.
  if (g_adc_ready) {
    GUARD(adc_hal_shutdown());
    g_adc_ready = false;
  }

  // Step 6: Clear the digi-pot driver so future runs perform a full initialisation sequence.
  if (g_digipot_ready) {
    ad524x_deinitialize();
    g_digipot_ready = false;
  }

  // Step 7: Reset cached state so Unity hooks observe a clean helper.
  g_indicator_leds_configured = false;
  g_is_initialized            = false;
  g_power_domains_energised   = false;
  g_neg_bias_generator_on     = false;
  g_power_config              = {};

  return POWER_CONTROL_OK;
}

void power_control_reset_for_test(void) {
  // Step 1: Clear cached state so each Unity test performs a fresh bring-up.
  g_power_config              = {};
  g_is_initialized            = false;
  g_power_domains_energised   = false;
  g_indicator_leds_configured = false;
  g_led_router_ready          = false;
  g_adc_ready                 = false;
  g_digipot_ready             = false;
  g_neg_bias_generator_on     = false;
}
