#include "main.hpp"

struct PhotodiodeSample {
  uint8_t wiper_code;
  int32_t channel4_code;
  int32_t channel5_code;
};

static const uint32_t k_spi_clock_hz          = 500000UL;
static const uint32_t k_settle_delay_ms       = 100UL;
static const uint8_t  k_digipot_channels[]    = {0u, 1u};
static const size_t   k_digipot_channel_count = sizeof(k_digipot_channels) / sizeof(k_digipot_channels[0]);
static const uint8_t  k_wiper_codes[]         = {0x00, 0x10, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0, 0xFF};
static const size_t   k_sample_count          = sizeof(k_wiper_codes) / sizeof(k_wiper_codes[0]);

static PhotodiodeSample g_samples[k_sample_count];

enum class LedSwitchState : uint8_t {
  kOff,
  kLed1,
  kLed2,
  kDrain,
};

struct LedSweepRequest {
  const char*    label;
  LedSwitchState state;
};

static const LedSweepRequest k_led_sweep_sequence[] = {
    {"Drain", LedSwitchState::kDrain},
    {"LED1", LedSwitchState::kLed1},
    {"LED2", LedSwitchState::kLed2},
};
static const size_t k_led_sweep_count = sizeof(k_led_sweep_sequence) / sizeof(k_led_sweep_sequence[0]);

static void wait_for_serial(void) {
  unsigned long start_ms = millis();
  while (!Serial && (millis() - start_ms) < 2000UL) {
    delay(50);
  }
}

static void configure_led_paths_off(void) {
  pinMode(TS5A3359_IN1, OUTPUT);
  pinMode(TS5A3359_IN2, OUTPUT);
  digitalWrite(TS5A3359_IN1, LOW);
  digitalWrite(TS5A3359_IN2, LOW);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_BLUE, LOW);
}

static void select_led_switch_state(LedSwitchState state) {
  switch (state) {
    case LedSwitchState::kOff:
      digitalWrite(TS5A3359_IN1, LOW);
      digitalWrite(TS5A3359_IN2, LOW);
      break;
    case LedSwitchState::kLed1:
      digitalWrite(TS5A3359_IN1, HIGH);
      digitalWrite(TS5A3359_IN2, LOW);
      break;
    case LedSwitchState::kLed2:
      digitalWrite(TS5A3359_IN1, LOW);
      digitalWrite(TS5A3359_IN2, HIGH);
      break;
    case LedSwitchState::kDrain:
      digitalWrite(TS5A3359_IN1, HIGH);
      digitalWrite(TS5A3359_IN2, HIGH);
      break;
  }
}

static void enable_power_domains(void) {
  pinMode(PIN_ENABLE_POWER, OUTPUT);
  digitalWrite(PIN_ENABLE_POWER, HIGH);
}

static bool initialise_ad524x(void) {
  Wire.begin();
  int return_code = ad524x_initialize(AD5242_I2C_ADDRESS, &Wire);
  if (return_code != AD524X_OK) {
    Serial.print(F("ad524x_initialize failed: "));
    Serial.println(return_code);
    return false;
  }

  for (size_t i = 0; i < k_digipot_channel_count; ++i) {
    return_code = ad524x_set_midscale(k_digipot_channels[i]);
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

static bool initialise_mcp356x(void) {
  int return_code = mcp356x_initialize(PIN_ADC_CS, k_spi_clock_hz);
  if (return_code != MCP356X_OK) {
    Serial.print(F("mcp356x_initialize failed: "));
    Serial.println(return_code);
    return false;
  }

  return_code = mcp356x_full_reset(NULL);
  if (return_code != MCP356X_OK) {
    Serial.print(F("mcp356x_full_reset failed: "));
    Serial.println(return_code);
    return false;
  }
  delay(2);  // Allow registers to settle after the reset command.

  return_code = mcp356x_apply_default_config();
  if (return_code != MCP356X_OK) {
    Serial.print(F("mcp356x_apply_default_config failed: "));
    Serial.println(return_code);
    return false;
  }

  return true;
}

static bool program_wipers(uint8_t wiper_code) {
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
  if (!program_wipers(wiper_code)) {
    return false;
  }

  delay(k_settle_delay_ms);  // Allow the transimpedance amplifiers to settle after the resistance step.

  PhotodiodeSample sample = {};
  sample.wiper_code       = wiper_code;

  int return_code = mcp356x_read_single_ended_channel(4u, 200u, &sample.channel4_code);
  if (return_code != MCP356X_OK) {
    Serial.print(F("read channel 4 failed: "));
    Serial.println(return_code);
    return false;
  }

  return_code = mcp356x_read_single_ended_channel(5u, 200u, &sample.channel5_code);
  if (return_code != MCP356X_OK) {
    Serial.print(F("read channel 5 failed: "));
    Serial.println(return_code);
    return false;
  }

  g_samples[index] = sample;
  return true;
}

static bool collect_samples(void) {
  for (size_t i = 0; i < k_sample_count; ++i) {
    if (!capture_sample(i, k_wiper_codes[i])) {
      return false;
    }
  }
  return true;
}

static void print_samples_inline(const char* label) {
  Serial.print(label);
  Serial.print('\t');

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
  for (size_t i = 0; i < k_digipot_channel_count; ++i) {
    (void) ad524x_set_midscale(k_digipot_channels[i]);
  }

  int standby_return_code = mcp356x_enter_standby(NULL);
  if (standby_return_code != MCP356X_OK) {
    Serial.print(F("mcp356x_enter_standby failed: "));
    Serial.println(standby_return_code);
  }
}

void setup() {
  Serial.begin(115200);
  wait_for_serial();

  enable_power_domains();
  configure_led_paths_off();

  if (!initialise_ad524x()) {
    return;
  }
  if (!initialise_mcp356x()) {
    return;
  }

  select_led_switch_state(LedSwitchState::kDrain);
  Serial.println(F("# Sweep format per line: state\t(wiper_hex,ch4_code,ch5_code)*"));
}

void loop() {
  bool sweep_failed = false;
  for (size_t i = 0; i < k_led_sweep_count; ++i) {
    const LedSweepRequest& request = k_led_sweep_sequence[i];
    select_led_switch_state(request.state);

    if (collect_samples()) {
      print_samples_inline(request.label);
    }
    else {
      Serial.print(F("# Photodiode sweep failed for state: "));
      Serial.println(request.label);
      sweep_failed = true;
    }
  }

  if (!sweep_failed) {
    Serial.println();  // Blank line separator between sweep cycles.
  }

  park_hardware();
  select_led_switch_state(LedSwitchState::kDrain);
  delay(1000);
}
