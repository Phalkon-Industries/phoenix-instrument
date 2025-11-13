#include "device_setup.hpp"
#include "led_router.hpp"
#include <Arduino.h>
#include <nrf_pwm.h>
#include <nrf_timer.h>

extern const uint32_t g_ADigitalPinMap[];

namespace {

constexpr uint16_t      k_pwm_top_value       = 1600u;  // 10 kHz when using the 16 MHz base clock.
constexpr uint16_t      k_pwm_high_count      = 800u;   // 50% duty cycle.
constexpr uint32_t      k_pwm_pin             = TS5A3359_IN1;
constexpr uint32_t      k_pwm_pin_not_used    = NRF_PWM_PIN_NOT_CONNECTED;
nrf_pwm_values_common_t g_pwm_sequence[]      = {k_pwm_high_count};
nrf_pwm_sequence_t      g_pwm_single_sequence = {
    .values    = {.p_common = g_pwm_sequence},
    .length    = NRF_PWM_VALUES_LENGTH(g_pwm_sequence),
    .repeats   = 0u,
    .end_delay = 0u,
};

NRF_PWM_Type* const k_pwm_instance = NRF_PWM0;

void configure_pwm_pins(void) {
  // Step 1: Map the Arduino pin to the encoded NRF port/pin value expected by the PWM peripheral.
  const uint32_t in1_psel = g_ADigitalPinMap[k_pwm_pin];

  // Step 2: Disconnect any unused channels and bind the primary output pin.
  k_pwm_instance->PSEL.OUT[0] = in1_psel;
  k_pwm_instance->PSEL.OUT[1] = k_pwm_pin_not_used;
  k_pwm_instance->PSEL.OUT[2] = k_pwm_pin_not_used;
  k_pwm_instance->PSEL.OUT[3] = k_pwm_pin_not_used;
}

void configure_pwm_core(void) {
  // Step 1: Route the PWM counter to the 16 MHz base clock in edge-aligned mode.
  k_pwm_instance->PRESCALER  = PWM_PRESCALER_PRESCALER_DIV_1;
  k_pwm_instance->MODE       = PWM_MODE_UPDOWN_Up;
  k_pwm_instance->COUNTERTOP = k_pwm_top_value;

  // Step 2: Configure the decoder to consume the same 16-bit value for all channels.
  k_pwm_instance->DECODER =
      (PWM_DECODER_LOAD_Common << PWM_DECODER_LOAD_Pos) | (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);

  // Step 3: Disable looping; the sequence will repeat automatically when SEQSTART is retriggered via shorts.
  k_pwm_instance->LOOP = 0u;
}

void configure_pwm_sequence(void) {
  // Step 1: Present the sequence buffer to the PWM peripheral.
  k_pwm_instance->SEQ[0].PTR      = reinterpret_cast<uint32_t>(g_pwm_single_sequence.values.p_common);
  k_pwm_instance->SEQ[0].CNT      = g_pwm_single_sequence.length;
  k_pwm_instance->SEQ[0].REFRESH  = g_pwm_single_sequence.repeats;
  k_pwm_instance->SEQ[0].ENDDELAY = g_pwm_single_sequence.end_delay;

  // Step 2: Request automatic restart when the first sequence completes.
  k_pwm_instance->SHORTS = PWM_SHORTS_LOOPSDONE_SEQSTART0_Msk;
}

void start_pwm(void) {
  // Step 1: Enable the PWM block and start the programmed sequence.
  k_pwm_instance->ENABLE            = (PWM_ENABLE_ENABLE_Enabled << PWM_ENABLE_ENABLE_Pos);
  k_pwm_instance->EVENTS_STOPPED    = 0u;
  k_pwm_instance->TASKS_SEQSTART[0] = 1u;
}

}  // namespace

void setup() {
  // Step 1: Ensure the device wiring is initialised so the switch control pin is configured for output.
  device_setup_initialize();

  // Step 1a: Drive the second control pin high so the TS5A3359 routes to the drain path while PWM toggles IN1.
  pinMode(TS5A3359_IN2, OUTPUT);
  digitalWrite(TS5A3359_IN2, HIGH);

  // Step 2: Set up the PWM peripheral using the raw NRF register interface.
  configure_pwm_pins();
  configure_pwm_core();
  configure_pwm_sequence();

  // Step 3: Start PWM playback; the hardware will toggle the pin without further firmware intervention.
  start_pwm();
}

void loop() {
  // Step 1: Keep the MCU responsive while PWM runs in hardware.
  delay(1000);
}
