// PWM GPIOTE probe sketch that logs hardware edge events while NRF_PWM3 drives
// the LED router control lines. This sketch exercises the production
// led_router helper so the timing observed here matches firmware behaviour.

#include "device_setup.hpp"
#include "led_router.hpp"
#include <Arduino.h>
#include <nrf.h>
#include <nrf_pwm.h>

extern const uint32_t g_ADigitalPinMap[];

namespace {

constexpr uint32_t k_pwm_minimum_period_us = 30000u;
constexpr uint32_t k_report_interval_ms    = 1000u;
constexpr size_t   k_edge_log_capacity     = 64u;

static_assert((k_edge_log_capacity > 0u) && ((k_edge_log_capacity & (k_edge_log_capacity - 1u)) == 0u),
              "Edge log must have a power-of-two capacity to simplify the ring buffer implementation.");

// Flags that track whether the probe has observed the expected edge shapes.
constexpr uint8_t  k_in1_pin            = static_cast<uint8_t>(TS5A3359_IN1);
constexpr uint8_t  k_in2_pin            = static_cast<uint8_t>(TS5A3359_IN2);
constexpr uint32_t k_edge_flag_in1_rise = 1u << 0;
constexpr uint32_t k_edge_flag_in1_fall = 1u << 1;
constexpr uint32_t k_edge_flag_in2_rise = 1u << 2;
constexpr uint32_t k_edge_flag_in2_fall = 1u << 3;

struct EdgeEvent {
  uint32_t timestamp_us;
  uint32_t pwm_period_count;
  uint8_t  pin;
  uint8_t  new_level;
  uint16_t reserved;
};

EdgeEvent         g_edge_log[k_edge_log_capacity];
volatile uint32_t g_edge_write_index         = 0u;
volatile uint32_t g_edge_read_index          = 0u;
volatile uint32_t g_edge_drop_count          = 0u;
volatile uint32_t g_edge_seen_flags          = 0u;
volatile uint32_t g_pwm_period_counter       = 0u;
volatile uint32_t g_last_period_timestamp_us = 0u;
uint32_t          g_last_report_ms           = 0u;
bool              g_gpiote_ready             = false;

uint8_t read_pin_level(uint8_t board_pin) {
  const uint32_t physical_pin = g_ADigitalPinMap[board_pin];
  const uint32_t port_index   = (physical_pin >> 5) & 0x01u;
  const uint32_t pin_index    = physical_pin & 0x1Fu;

  if (port_index == 0u) {
    return (NRF_P0->IN >> pin_index) & 0x01u;
  }

  return (NRF_P1->IN >> pin_index) & 0x01u;
}

void append_edge_event(uint8_t board_pin, uint8_t new_level) {
  const uint32_t next_index = g_edge_write_index + 1u;
  if ((next_index - g_edge_read_index) > k_edge_log_capacity) {
    g_edge_drop_count++;
    return;
  }

  const uint32_t slot = g_edge_write_index & (k_edge_log_capacity - 1u);

  EdgeEvent event        = {};
  event.timestamp_us     = micros();
  event.pwm_period_count = g_pwm_period_counter;
  event.pin              = board_pin;
  event.new_level        = new_level;

  g_edge_log[slot]   = event;
  g_edge_write_index = next_index;

  uint32_t   edge_flag = 0u;
  const bool is_rising = (new_level != 0u);

  if (board_pin == k_in1_pin) {
    edge_flag = is_rising ? k_edge_flag_in1_rise : k_edge_flag_in1_fall;
  }
  else if (board_pin == k_in2_pin) {
    edge_flag = is_rising ? k_edge_flag_in2_rise : k_edge_flag_in2_fall;
  }

  if (edge_flag != 0u) {
    g_edge_seen_flags |= edge_flag;
  }
}

void configure_pwm_interrupts(void) {
  // Step 1: Clear lingering events before arming the period-end interrupt vector.
  NRF_PWM3->EVENTS_PWMPERIODEND = 0u;
  NRF_PWM3->EVENTS_STOPPED      = 0u;
  NRF_PWM3->INTENCLR            = 0xFFFFFFFFu;

  // Step 2: Enable the period-end interrupt so firmware can correlate GPIOTE edges with PWM cadence.
  NRF_PWM3->INTENSET = PWM_INTENSET_PWMPERIODEND_Msk;
  NVIC_ClearPendingIRQ(PWM3_IRQn);
  NVIC_SetPriority(PWM3_IRQn, 5u);
  NVIC_EnableIRQ(PWM3_IRQn);
}

void handle_pin_edge(uint8_t board_pin) {
  append_edge_event(board_pin, read_pin_level(board_pin));
}

void in1_edge_isr(void) {
  handle_pin_edge(k_in1_pin);
}

void in2_edge_isr(void) {
  handle_pin_edge(k_in2_pin);
}

bool configure_gpiote(void) {
  // Step 1: Clear any existing interrupt bindings so the probe owns the channels.
  detachInterrupt(k_in1_pin);
  detachInterrupt(k_in2_pin);

  // Step 2: Attach CHANGE handlers to both PWM-driven pins without disturbing their drive mode.
  const int in1_mask = attachInterrupt(k_in1_pin, in1_edge_isr, CHANGE);
  if (in1_mask == 0) {
    Serial.println("[probe] attachInterrupt on IN1 failed.");
    return false;
  }

  const int in2_mask = attachInterrupt(k_in2_pin, in2_edge_isr, CHANGE);
  if (in2_mask == 0) {
    Serial.println("[probe] attachInterrupt on IN2 failed.");
    detachInterrupt(k_in1_pin);
    return false;
  }

  g_gpiote_ready = true;
  return true;
}

void flush_edge_log(void) {
  while (true) {
    EdgeEvent event = {};

    noInterrupts();
    if (g_edge_read_index == g_edge_write_index) {
      interrupts();
      break;
    }

    const uint32_t slot = g_edge_read_index & (k_edge_log_capacity - 1u);
    event               = g_edge_log[slot];
    g_edge_read_index++;
    interrupts();

    const char* pin_label = "UNK";
    if (event.pin == k_in1_pin) {
      pin_label = "IN1";
    }
    else if (event.pin == k_in2_pin) {
      pin_label = "IN2";
    }

    const char* edge_label = (event.new_level != 0u) ? "rise" : "fall";

    Serial.print("[edge] ");
    Serial.print(pin_label);
    Serial.print(" ");
    Serial.print(edge_label);
    Serial.print(" t=");
    Serial.print(event.timestamp_us);
    Serial.print("us period=");
    Serial.print(event.pwm_period_count);
    Serial.println();
  }
}

void report_status(void) {
  LedRouterPwmTestSnapshot snapshot = {};
  led_router_get_pwm_test_snapshot(&snapshot);

  noInterrupts();
  const uint32_t pwm_periods              = g_pwm_period_counter;
  const uint32_t last_period_timestamp_us = g_last_period_timestamp_us;
  const uint32_t edge_flags               = g_edge_seen_flags;
  const uint32_t drop_count               = g_edge_drop_count;
  interrupts();

  Serial.print("[status] periods=");
  Serial.print(pwm_periods);
  Serial.print(" last_period_us=");
  Serial.print(last_period_timestamp_us);
  Serial.print(" countertop=");
  Serial.print(snapshot.countertop);
  Serial.print(" in1=");
  Serial.print((edge_flags & k_edge_flag_in1_rise) ? "rise" : "-");
  Serial.print("/");
  Serial.print((edge_flags & k_edge_flag_in1_fall) ? "fall" : "-");
  Serial.print(" in2=");
  Serial.print((edge_flags & k_edge_flag_in2_rise) ? "rise" : "-");
  Serial.print("/");
  Serial.print((edge_flags & k_edge_flag_in2_fall) ? "fall" : "-");
  Serial.print(" base_freq_hz=");
  Serial.print(snapshot.base_frequency_hz);
  if (drop_count != 0u) {
    Serial.print(" dropped=");
    Serial.print(drop_count);
  }
  Serial.println();
}

}  // namespace

void setup() {
  // Step 1: Bring up the serial console for probe telemetry.
  Serial.begin(115200);
  uint32_t serial_wait_ms = 0u;
  while (!Serial && (serial_wait_ms < 2000u)) {
    delay(10);
    serial_wait_ms += 10u;
  }

  Serial.println();
  Serial.println("[probe] PWM GPIOTE edge monitor starting...");

  // Step 2: Initialise board wiring so the production helpers can claim their pins.
  (void) device_setup_initialize();

  int return_code = led_router_initialize(&g_device_led_router_config);
  if (return_code != LED_ROUTER_OK) {
    Serial.print("[probe] led_router_initialize failed: ");
    Serial.println(return_code);
  }

  return_code = led_router_set_state(LedRouterState::LED_ROUTER_STATE_DRAIN);
  if (return_code != LED_ROUTER_OK) {
    Serial.print("[probe] led_router_set_state failed: ");
    Serial.println(return_code);
  }

  // Step 3: Start the production PWM waveform so NRF_PWM3 owns the switch lines.
  return_code = led_router_pwm_start(k_pwm_minimum_period_us);
  if (return_code != LED_ROUTER_OK) {
    Serial.print("[probe] led_router_pwm_start failed: ");
    Serial.println(return_code);
  }
  configure_pwm_interrupts();

  // Step 4: Configure GPIOTE channels to watch both edges on the PWM-driven pins.
  g_gpiote_ready = configure_gpiote();
  if (!g_gpiote_ready) {
    Serial.println("[probe] GPIOTE setup failed; edge capture disabled.");
  }

  // Step 5: Report the programmed PWM parameters for reference during trace review.
  LedRouterPwmTestSnapshot snapshot = {};
  led_router_get_pwm_test_snapshot(&snapshot);
  Serial.print("[probe] PWM countertop=");
  Serial.print(snapshot.countertop);
  Serial.print(" ch0=");
  Serial.print(snapshot.channel0_level);
  Serial.print(" ch1=");
  Serial.print(snapshot.channel1_level);
  Serial.print(" inverted=");
  Serial.print(snapshot.channel1_is_inverted ? "yes" : "no");
  Serial.print(" base_freq_hz=");
  Serial.println(snapshot.base_frequency_hz);

  Serial.println("[probe] Configuration complete; logging edges.");
}

void loop() {
  // Step 1: Drain any captured edge events to the serial console.
  flush_edge_log();

  // Step 2: Emit a periodic heartbeat so hardware captures can align with firmware observations.
  const uint32_t now_ms = millis();
  if ((now_ms - g_last_report_ms) >= k_report_interval_ms) {
    g_last_report_ms = now_ms;
    report_status();
  }

  // Step 3: Yield briefly so the PWM and GPIOTE ISRs can operate without saturation.
  delay(5);
}

extern "C" void PWM3_IRQHandler(void) {
  if (NRF_PWM3->EVENTS_PWMPERIODEND != 0u) {
    NRF_PWM3->EVENTS_PWMPERIODEND = 0u;
    g_pwm_period_counter++;
    g_last_period_timestamp_us = micros();
  }
}
