#include "device_setup.hpp"
#include "led_router.hpp"
#include <Arduino.h>
#include <nrf_pwm.h>

extern const uint32_t g_ADigitalPinMap[];

namespace {

constexpr uint16_t         k_pwm_top_value            = 1600u;  // 10 kHz when using the 16 MHz base clock.
constexpr uint16_t         k_pwm_high_count           = 800u;   // 50% duty cycle.
constexpr uint16_t         k_pwm_polarity_invert_mask = 0x8000u;
constexpr uint32_t         k_pwm_pin_in1              = TS5A3359_IN1;
constexpr uint32_t         k_pwm_pin_in2              = TS5A3359_IN2;
constexpr uint32_t         k_pwm_pin_not_used         = NRF_PWM_PIN_NOT_CONNECTED;
nrf_pwm_values_wave_form_t g_pwm_waveforms[]          = {
    {k_pwm_high_count, static_cast<uint16_t>(k_pwm_polarity_invert_mask | k_pwm_high_count), 0u, k_pwm_top_value},
};

NRF_PWM_Type* const k_pwm_instance = NRF_PWM0;

void configure_pwm_pins(void) {
  // Step 1: Map the Arduino pins to the encoded NRF port/pin values expected by the PWM peripheral.
  const uint32_t in1_psel = g_ADigitalPinMap[k_pwm_pin_in1];
  const uint32_t in2_psel = g_ADigitalPinMap[k_pwm_pin_in2];

  // Step 2: Bind both switch control pins and disconnect the remaining channels.
  k_pwm_instance->PSEL.OUT[0] = in1_psel;
  k_pwm_instance->PSEL.OUT[1] = in2_psel;
  k_pwm_instance->PSEL.OUT[2] = k_pwm_pin_not_used;
  k_pwm_instance->PSEL.OUT[3] = k_pwm_pin_not_used;
}

void configure_pwm_core(void) {
  // Step 1: Disable the PWM instance while registers are being updated.
  k_pwm_instance->ENABLE = (PWM_ENABLE_ENABLE_Disabled << PWM_ENABLE_ENABLE_Pos);

  // Step 2: Route the PWM counter to the 16 MHz base clock in edge-aligned mode.
  k_pwm_instance->PRESCALER  = PWM_PRESCALER_PRESCALER_DIV_1;
  k_pwm_instance->MODE       = PWM_MODE_UPDOWN_Up;
  k_pwm_instance->COUNTERTOP = k_pwm_top_value;

  // Step 3: Configure the decoder to play a waveform sequence with per-channel polarity control.
  k_pwm_instance->DECODER =
      (PWM_DECODER_LOAD_WaveForm << PWM_DECODER_LOAD_Pos) | (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);

  // Step 4: Disable looping; the sequence will repeat automatically when SEQSTART is retriggered via shorts.
  k_pwm_instance->LOOP = 0u;
}

void configure_pwm_sequence(void) {
  // Step 1: Present the inverted-pair waveform to the PWM peripheral.
  k_pwm_instance->SEQ[0].PTR      = reinterpret_cast<uint32_t>(&g_pwm_waveforms[0]);
  k_pwm_instance->SEQ[0].CNT      = NRF_PWM_VALUES_LENGTH(g_pwm_waveforms);
  k_pwm_instance->SEQ[0].REFRESH  = 0u;
  k_pwm_instance->SEQ[0].ENDDELAY = 0u;

  // Step 2: Clear the unused secondary sequence so the hardware loops on sequence 0.
  k_pwm_instance->SEQ[1].PTR = 0u;
  k_pwm_instance->SEQ[1].CNT = 0u;

  // Step 3: Request automatic restart when the first sequence completes.
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
  // Step 1: Ensure the device wiring is initialised so the switch control pins are configured for output.
  device_setup_initialize();

  // Step 2: Set up the PWM peripheral so the blue and green paths share the cycle equally.
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
