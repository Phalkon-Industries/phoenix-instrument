#include "adc_hal.hpp"
#include "device_setup.hpp"
#include "digipot_hal.hpp"
#include "led_router.hpp"
#include "power_control.hpp"

struct PhotodiodeSample {
  uint16_t wiper_code;
  int32_t  channel0_code;
  int32_t  channel1_code;
};

static const uint32_t k_settle_delay_ms = 1UL;
static const uint32_t k_scan_hold_ms    = 500UL;  // Pause at each wiper step so the scope trace settles.
// Hand-picked wiper codes spanning the blue MCP41U83T's 10-bit range (0-1023).
// The green AD5242 is clamped to its 8-bit max (255) by program_wipers.
static const uint16_t k_wiper_codes[] = {0x000, 0x010, 0x020, 0x040, 0x0FF};
static const size_t   k_sample_count  = sizeof(k_wiper_codes) / sizeof(k_wiper_codes[0]);

static PhotodiodeSample g_samples[k_sample_count];

struct LedSweepRequest {
  const char*    label;
  LedRouterState state;
};

static const LedSweepRequest k_led_sweep_sequence[] = {
    {"Drain", LedRouterState::LED_ROUTER_STATE_DRAIN},
    {"Green", LedRouterState::LED_ROUTER_STATE_GREEN},
    {"Blue", LedRouterState::LED_ROUTER_STATE_BLUE},
};
static const size_t k_led_sweep_count = sizeof(k_led_sweep_sequence) / sizeof(k_led_sweep_sequence[0]);

static const LedRouterConfig k_led_router_config = {
    TS5A3359_IN1,
    TS5A3359_IN2,
    {false, nullptr},
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

static bool program_wipers(uint16_t wiper_code) {
  // Step 1: Set the blue MCP41U83T wiper (supports full 10-bit range).
  int return_code = digipot_blue_set_wiper(wiper_code);
  if (return_code != DIGIPOT_HAL_OK) {
    Serial.print(F("digipot_blue_set_wiper failed: "));
    Serial.println(return_code);
    return false;
  }

  // Step 2: Set the green AD5242 wiper, clamping to its 8-bit maximum.
  const uint16_t green_code = (wiper_code <= DIGIPOT_GREEN_MAX_WIPER) ? wiper_code : DIGIPOT_GREEN_MAX_WIPER;
  return_code               = digipot_green_set_wiper(green_code);
  if (return_code != DIGIPOT_HAL_OK) {
    Serial.print(F("digipot_green_set_wiper failed: "));
    Serial.println(return_code);
    return false;
  }
  return true;
}

static bool capture_sample(size_t index, uint16_t wiper_code) {
  // Step 1: Program the wiper code before sampling.
  if (!program_wipers(wiper_code)) {
    return false;
  }

  // Step 2: Allow the analog front-end time to settle after the resistance change.
  delay(k_settle_delay_ms);

  // Step 3: Initialize the sample record with the applied wiper code.
  PhotodiodeSample sample = {};
  sample.wiper_code       = wiper_code;

  // Step 4: Read photodiode ADC channels (ch0 = blue, ch1 = green).
  int return_code = adc_hal_read_single_ended(AdcHalChannel::ADC_HAL_CHANNEL_0, 1000000u, &sample.channel0_code);
  if (return_code != ADC_HAL_OK) {
    Serial.print(F("read channel 0 failed: "));
    Serial.println(return_code);
    return false;
  }

  return_code = adc_hal_read_single_ended(AdcHalChannel::ADC_HAL_CHANNEL_1, 1000000u, &sample.channel1_code);
  if (return_code != ADC_HAL_OK) {
    Serial.print(F("read channel 1 failed: "));
    Serial.println(return_code);
    return false;
  }

  // Step 5: Store the captured sample in the global collection.
  g_samples[index] = sample;
  return true;
}

static bool collect_samples(void) {
  for (size_t i = 0u; i < k_sample_count; ++i) {
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
  for (size_t i = 0u; i < k_sample_count; ++i) {
    const PhotodiodeSample& sample = g_samples[i];

    Serial.print(F("0x"));
    if (sample.wiper_code < 0x10u) {
      Serial.print(F("000"));
    }
    else if (sample.wiper_code < 0x100u) {
      Serial.print(F("00"));
    }
    else if (sample.wiper_code < 0x1000u) {
      Serial.print('0');
    }
    Serial.print(sample.wiper_code, HEX);
    Serial.print(',');
    Serial.print(sample.channel0_code);
    Serial.print(',');
    Serial.print(sample.channel1_code);

    if ((i + 1u) < k_sample_count) {
      Serial.print('\t');
    }
  }
  Serial.println();
}

static void park_hardware(void) {
  // Step 1: Return both digipots to mid-scale so the next sweep starts uniformly.
  (void) digipot_blue_set_wiper(DIGIPOT_BLUE_MAX_WIPER / 2u);
  (void) digipot_green_set_wiper(DIGIPOT_GREEN_MAX_WIPER / 2u);

  // Step 2: Park the router in the drain state for the next run.
  (void) select_led_switch_state(LedRouterState::LED_ROUTER_STATE_DRAIN);
}

void setup() {
  // Step 1: Initialize serial logging and wait for the host connection.
  Serial.begin(115200);
  wait_for_serial();

  // Step 2: Bring up all shared hardware via the production initialisation path.
  (void) device_setup_initialize();

  // Step 3: Park the router in the drain state before sampling.
  if (!select_led_switch_state(LedRouterState::LED_ROUTER_STATE_DRAIN)) {
    return;
  }

  // Step 4: Inform the operator about the sweep output format.
  Serial.println(F("# Sweep format per line: state\\t(wiper_hex,ch0_code,ch1_code)*"));
  Serial.println(F("# Cycle: Drain → Green → Blue, scanning wiper at each state"));
  Serial.print(F("# Wiper codes: "));
  for (size_t i = 0u; i < k_sample_count; ++i) {
    Serial.print(F("0x"));
    if (k_wiper_codes[i] < 0x10u) {
      Serial.print(F("00"));
    }
    else if (k_wiper_codes[i] < 0x100u) {
      Serial.print('0');
    }
    Serial.print(k_wiper_codes[i], HEX);
    if ((i + 1u) < k_sample_count) {
      Serial.print(',');
    }
  }
  Serial.println();
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
