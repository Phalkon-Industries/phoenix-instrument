#include "main.hpp"

// Define NeoPixel parameters

Adafruit_NeoPixel strip(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// Enum for LED Switch
enum LedSwitchState { LED1, LED2, OFF, DRAIN };

void changeSwitch(LedSwitchState state) {
  // Step 1: Drive the TS5A3359 control pins to select the requested LED path.
  switch (state) {
    case LED1:
      digitalWrite(TS5A3359_IN1, HIGH);
      digitalWrite(TS5A3359_IN2, LOW);
      break;
    case LED2:
      digitalWrite(TS5A3359_IN1, LOW);
      digitalWrite(TS5A3359_IN2, HIGH);
      break;
    case OFF:
      digitalWrite(TS5A3359_IN1, LOW);
      digitalWrite(TS5A3359_IN2, LOW);
      break;
    case DRAIN:
      digitalWrite(TS5A3359_IN1, HIGH);
      digitalWrite(TS5A3359_IN2, HIGH);
      break;
  }
}

// Helper to interpolate between two colors
uint32_t fadeColor(uint32_t color1, uint32_t color2, uint8_t step, uint8_t maxSteps) {
  // Step 1: Extract RGB components from the starting and ending colors.
  uint8_t r1 = (color1 >> 16) & 0xFF;
  uint8_t g1 = (color1 >> 8) & 0xFF;
  uint8_t b1 = color1 & 0xFF;
  uint8_t r2 = (color2 >> 16) & 0xFF;
  uint8_t g2 = (color2 >> 8) & 0xFF;
  uint8_t b2 = color2 & 0xFF;

  // Step 2: Linearly interpolate each channel based on the current fade step.
  uint8_t r = r1 + ((r2 - r1) * step) / maxSteps;
  uint8_t g = g1 + ((g2 - g1) * step) / maxSteps;
  uint8_t b = b1 + ((b2 - b1) * step) / maxSteps;

  // Step 3: Recombine the blended color for the NeoPixel driver.
  return strip.Color(r, g, b);
}

void setup() {
  // Step 1: Initialize the NeoPixel strip and ensure LEDs start off.
  strip.begin();
  strip.show();  // Initialize all pixels to 'off'
  // Step 2: Enable the shared power rail feeding the LED hardware.
  pinMode(PIN_ENABLE_5V_POWER, OUTPUT);
  digitalWrite(PIN_ENABLE_5V_POWER, HIGH);

  // Step 3: Configure on-board status LEDs and default them to off.
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, LOW);   // Turn off initially
  digitalWrite(LED_BLUE, LOW);  // Turn off initially

  // Step 4: Prepare the analog switch pins and leave the path open by default.
  pinMode(TS5A3359_IN1, OUTPUT);
  pinMode(TS5A3359_IN2, OUTPUT);
  digitalWrite(TS5A3359_IN1, LOW);  // Set initial state as needed
  digitalWrite(TS5A3359_IN2, LOW);  // Set initial state as needed
}

void loop() {
  // Step 1: Render and display the current interpolated color.
  static uint8_t colorIndex     = 0;
  static uint8_t nextColorIndex = 1;
  static uint8_t step           = 0;
  const uint8_t  maxSteps       = 100;

  static uint16_t ledFlipCounter  = 0;
  const uint16_t  ledFlipInterval = 50;  // Flip every 50 cycles (~1s)

  uint32_t colors[] = {
      strip.Color(255, 0, 0),  // Red
      strip.Color(0, 255, 0),  // Green
      strip.Color(0, 0, 255)   // Blue
  };

  strip.clear();
  strip.setPixelColor(0, fadeColor(colors[colorIndex], colors[nextColorIndex], step, maxSteps));
  strip.show();
  delay(20);

  // Step 2: Periodically toggle indicator LEDs and rotate the switch state.
  // Occasionally flip red and blue LEDs
  ledFlipCounter++;
  if (ledFlipCounter >= ledFlipInterval) {
    static bool ledState = false;
    ledState             = !ledState;
    digitalWrite(LED_RED, ledState ? HIGH : LOW);
    digitalWrite(LED_BLUE, !ledState ? HIGH : LOW);
    ledFlipCounter                    = 0;
    static LedSwitchState switchState = LED1;
    switchState                       = static_cast<LedSwitchState>((switchState + 1) % 4);
    changeSwitch(switchState);
  }

  // Step 3: Advance the fade indices so colors cycle smoothly.
  step++;
  if (step > maxSteps) {
    step           = 0;
    colorIndex     = nextColorIndex;
    nextColorIndex = (nextColorIndex + 1) % 3;
  }
}