#ifndef PIO_UNIT_TESTING  // To allow device settings to work in tests (couldn't find a better way other than using
                          // PIO Shared Code).

#include "cli.hpp"
#include "device_setup.hpp"
#include <Arduino.h>

void setup() {
  Serial.begin(115200);

  // Step 1: Configure the instrument (power domains, ADC, PWM LEDs) before accepting commands.
  const int return_code = device_setup_initialize();
  if (return_code != LIGHT_READINGS_OK) {
    Serial.print("device_setup_initialize failed: ");
    Serial.println(return_code);
    return;
  }

  // Step 2: Bring up the CLI so baseline/sample commands can be received.
  cli_initialize();
}

void loop() {
  // Step 1: Service CLI commands.
  cli_poll();
}

#endif
