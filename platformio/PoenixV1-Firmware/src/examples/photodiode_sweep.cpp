#include "adc_hal.hpp"
#include "led_router.hpp"
#include "main.hpp"
#include "power_control.hpp"

struct PhotodiodeSample {
  uint8_t wiper_code;
  int32_t channel4_code;
  int32_t channel5_code;
};

static const uint32_t k_spi_clock_hz          = 500000UL;
static const uint32_t k_settle_delay_ms       = 1UL;
static const uint8_t  k_digipot_channels[]    = {0u, 1u};
static const size_t   k_digipot_channel_count = sizeof(k_digipot_channels) / sizeof(k_digipot_channels[0]);
static const uint8_t  k_wiper_codes[]         = {0x00, 0x10, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xFF};
static const size_t   k_sample_count          = sizeof(k_wiper_codes) / sizeof(k_wiper_codes[0]);

static PhotodiodeSample g_samples[k_sample_count];

struct LedSweepRequest {
  const char*    label;
  LedRouterState state;
};

static const LedSweepRequest k_led_sweep_sequence[] = {
    {"Drain", LedRouterState::LED_ROUTER_STATE_DRAIN},
    {"LED1", LedRouterState::LED_ROUTER_STATE_LED1},
    {"LED2", LedRouterState::LED_ROUTER_STATE_LED2},
};
static const size_t k_led_sweep_count = sizeof(k_led_sweep_sequence) / sizeof(k_led_sweep_sequence[0]);

static const LedRouterConfig k_led_router_config = {
    TS5A3359_IN1,
    TS5A3359_IN2,
};

static const AdcHalConfig k_adc_hal_config = {
    PIN_ADC_CS,
    k_spi_clock_hz,
    PIN_ADC_IRQ,
};

#if defined(LED_RED)
static constexpr int k_indicator_red_pin = LED_RED;
#else
static constexpr int k_indicator_red_pin = -1;
#endif

#if defined(LED_BLUE)
static constexpr int k_indicator_blue_pin = LED_BLUE;
#else
static constexpr int k_indicator_blue_pin = -1;
#endif

static PowerControlConfig g_power_control_config = {
    .led_router_config  = &k_led_router_config,
    .adc_config         = &k_adc_hal_config,
    .wire_bus           = &Wire,
    .digipot_address    = AD5242_I2C_ADDRESS,
    .power_enable_pin   = PIN_ENABLE_POWER,
    .indicator_red_pin  = k_indicator_red_pin,
    .indicator_blue_pin = k_indicator_blue_pin,
};

static void wait_for_serial(void) {
  // Step 1: Remember the start time so we can time out the wait loop.
  unsigned long start_ms = millis();
  // Step 2: Poll for the serial interface while respecting the timeout window.
  while (!Serial && (millis() - start_ms) < 2000UL) {
    delay(50);
  }
}

static bool select_led_switch_state(LedRouterState state) {
  // Step 1: Command the router to the requested state and log any errors.
  const int return_code = led_router_set_state(state);
  if (return_code != LED_ROUTER_OK) {
    Serial.print(F("led_router_set_state failed: "));
    Serial.println(return_code);
    return false;
  }

  return true;
}

static bool configure_digipot_midscale(void) {
  for (size_t i = 0; i < k_digipot_channel_count; ++i) {
    const int return_code = ad524x_set_midscale(k_digipot_channels[i]);
    if (return_code != AD524X_OK) {
      Serial.print(F("ad524x_set_midscale failed on channel "));
      Serial.print(k_digipot_channels[i]);
      Serial.print(F(": "));
      Serial.println(return_code);
      return false;
    }
  }
  return true;
}

static bool program_wipers(uint8_t wiper_code) {
  // Step 1: Iterate across each digi-pot channel and update its wiper position.
  for (size_t i = 0; i < k_digipot_channel_count; ++i) {
    int return_code = ad524x_set_wiper(k_digipot_channels[i], wiper_code);
    if (return_code != AD524X_OK) {
      Serial.print(F("ad524x_set_wiper failed on channel "));
      Serial.print(k_digipot_channels[i]);
      Serial.print(F(": "));
      Serial.println(return_code);
      return false;
    }
  }
  return true;
}

static bool capture_sample(size_t index, uint8_t wiper_code) {
  // Step 1: Program the wiper code before sampling.
  if (!program_wipers(wiper_code)) {
    return false;
  }

  // Step 2: Allow the analog front-end time to settle after the resistance change.
  delay(k_settle_delay_ms);  // Allow the transimpedance amplifiers to settle after the resistance step.

  // Step 3: Initialize the sample record with the applied wiper code.
  PhotodiodeSample sample = {};
  sample.wiper_code       = wiper_code;

  // Step 4: Read both ADC channels and record failures for debugging.
  int return_code = adc_hal_read_single_ended(AdcHalChannel::ADC_HAL_CHANNEL_4, 1000000u, &sample.channel4_code);
  if (return_code != ADC_HAL_OK) {
    Serial.print(F("read channel 4 failed: "));
    Serial.println(return_code);
    return false;
  }

  return_code = adc_hal_read_single_ended(AdcHalChannel::ADC_HAL_CHANNEL_5, 1000000u, &sample.channel5_code);
  if (return_code != ADC_HAL_OK) {
    Serial.print(F("read channel 5 failed: "));
    Serial.println(return_code);
    return false;
  }

  // Step 5: Store the captured sample in the global collection.
  g_samples[index] = sample;
  return true;
}

static bool collect_samples(void) {
  // Step 1: Iterate across the wiper sweep table and capture each sample in sequence.
  for (size_t i = 0; i < k_sample_count; ++i) {
    if (!capture_sample(i, k_wiper_codes[i])) {
      return false;
    }
  }
  return true;
}

static void print_samples_inline(const char* label) {
  // Step 1: Emit the state label as the row prefix.
  Serial.print(label);
  Serial.print('\t');

  // Step 2: Print each recorded sample as a tab-separated tuple.
  for (size_t i = 0; i < k_sample_count; ++i) {
    const PhotodiodeSample& sample = g_samples[i];

    Serial.print(F("0x"));
    if (sample.wiper_code < 0x10) {
      Serial.print('0');
    }
    Serial.print(sample.wiper_code, HEX);
    Serial.print(',');
    Serial.print(sample.channel4_code);
    Serial.print(',');
    Serial.print(sample.channel5_code);

    if ((i + 1u) < k_sample_count) {
      Serial.print('\t');
    }
  }
  Serial.println();
}

static void park_hardware(void) {
  // Step 1: Return each digi-pot to midscale so the next sweep starts uniformly.
  for (size_t i = 0; i < k_digipot_channel_count; ++i) {
    (void) ad524x_set_midscale(k_digipot_channels[i]);
  }

  // Step 2: Ask the ADC to enter standby and log any failure codes.
  int standby_return_code = adc_hal_enter_standby();
  if (standby_return_code != ADC_HAL_OK) {
    Serial.print(F("adc_hal_enter_standby failed: "));
    Serial.println(standby_return_code);
  }

  // Step 3: Park the router in the drain state for the next run.
  (void) select_led_switch_state(LedRouterState::LED_ROUTER_STATE_DRAIN);
}

void setup() {
  // Step 1: Initialize serial logging and wait for the host connection.
  Serial.begin(115200);
  wait_for_serial();

  // Step 2: Power the hardware domains and initialise shared peripherals.
  const int power_return_code = power_control_prepare_power_domains(&g_power_control_config);
  if (power_return_code != POWER_CONTROL_OK) {
    Serial.print(F("power_control_prepare_power_domains failed: "));
    Serial.println(power_return_code);
    return;
  }
  if (!configure_digipot_midscale()) {
    return;
  }
  if (!select_led_switch_state(LedRouterState::LED_ROUTER_STATE_DRAIN)) {
    return;
  }
  // Step 4: Inform the operator about the sweep output format.
  Serial.println(F("# Sweep format per line: state\t(wiper_hex,ch4_code,ch5_code)*"));
}

void loop() {
  // Step 1: Sweep through each LED routing state, capturing samples per wiper code.
  bool sweep_failed = false;
  for (size_t i = 0; i < k_led_sweep_count; ++i) {
    const LedSweepRequest& request = k_led_sweep_sequence[i];
    if (!select_led_switch_state(request.state)) {
      sweep_failed = true;
      break;
    }

    if (collect_samples()) {
      print_samples_inline(request.label);
    }
    else {
      Serial.print(F("# Photodiode sweep failed for state: "));
      Serial.println(request.label);
      sweep_failed = true;
    }
  }

  // Step 2: Separate successful sweeps with a blank line for readability.
  if (!sweep_failed) {
    Serial.println();  // Blank line separator between sweep cycles.
  }

  // Step 3: Park the hardware and reinitialize routing for the next sweep.
  park_hardware();
  // Step 4: Delay briefly before restarting the sweep cycle.
  delay(1000);
}
