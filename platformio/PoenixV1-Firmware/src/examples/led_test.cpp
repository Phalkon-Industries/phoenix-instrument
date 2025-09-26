#include "main.hpp"

// Define NeoPixel parameters

Adafruit_NeoPixel strip(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();
  strip.show();  // Initialize all pixels to 'off'
}

void loop() {
  strip.clear();
  strip.setPixelColor(0, strip.Color(255, 0, 0));  // Red
  strip.show();
  delay(200);
}