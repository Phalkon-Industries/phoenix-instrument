#ifndef PIO_UNIT_TESTING  // To allow device settings to work in tests (couldn't find a better way other than using
                          // PIO Shared Code).

#include "cli.hpp"
#include "device_setup.hpp"
#include "led_router.hpp"
#include <Arduino.h>

#ifndef PHOENIX_STARTUP_WARMUP_DELAY_MS
#define PHOENIX_STARTUP_WARMUP_DELAY_MS 0u
#endif

#ifndef PHOENIX_DEVICE_SETUP_RETRY_DELAY_MS
#define PHOENIX_DEVICE_SETUP_RETRY_DELAY_MS 500u
#endif

#ifndef PHOENIX_DEVICE_SETUP_RETRY_LIMIT
#define PHOENIX_DEVICE_SETUP_RETRY_LIMIT 0u
#endif

void setup() {
  Serial.begin(115200);
  delay(100);  // short delay to allow serial to set up

  // Step 2: Configure the instrument before accepting CLI commands.
  uint32_t setup_attempt_count = 0u;
  while (true) {
    const int return_code = device_setup_initialize();
    if (return_code == LIGHT_READINGS_OK) {
      break;
    }

    Serial.print("error\tdevice_setup_initialize\t");
    Serial.println(return_code);

    ++setup_attempt_count;
    if ((PHOENIX_DEVICE_SETUP_RETRY_LIMIT > 0u) && (setup_attempt_count >= PHOENIX_DEVICE_SETUP_RETRY_LIMIT)) {
      return;
    }

    delay(PHOENIX_DEVICE_SETUP_RETRY_DELAY_MS);
  }

  // Step 3: Start PWM playback so the LED router pins remain driven for the lifetime of the app.
  const int led_router_return_code = led_router_pwm_start(g_device_light_readings_config.pwm_config.minimum_period_us);
  if (led_router_return_code != LED_ROUTER_OK) {
    Serial.print("error\tled_router_pwm_start\t");
    Serial.println(led_router_return_code);
    return;
  }

  // Step 4: Optionally allow LEDs and sensors to warm up before measurements.
  if (PHOENIX_STARTUP_WARMUP_DELAY_MS > 0u) {
    delay(PHOENIX_STARTUP_WARMUP_DELAY_MS);
  }

  // Step 5: Bring up the CLI so baseline/sample commands can be received.
  cli_initialize();
}

void loop() {
  // Step 1: Service CLI commands.
  cli_poll();
}

#endif
