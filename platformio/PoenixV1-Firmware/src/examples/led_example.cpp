#include "main.hpp"

// Define NeoPixel parameters

Adafruit_NeoPixel strip(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// Helper to interpolate between two colors
uint32_t fadeColor(uint32_t color1, uint32_t color2, uint8_t step,
                   uint8_t maxSteps) {
  uint8_t r1 = (color1 >> 16) & 0xFF;
  uint8_t g1 = (color1 >> 8) & 0xFF;
  uint8_t b1 = color1 & 0xFF;
  uint8_t r2 = (color2 >> 16) & 0xFF;
  uint8_t g2 = (color2 >> 8) & 0xFF;
  uint8_t b2 = color2 & 0xFF;

  uint8_t r = r1 + ((r2 - r1) * step) / maxSteps;
  uint8_t g = g1 + ((g2 - g1) * step) / maxSteps;
  uint8_t b = b1 + ((b2 - b1) * step) / maxSteps;

  return strip.Color(r, g, b);
}

void setup() {
  strip.begin();
  strip.show();  // Initialize all pixels to 'off'
}

void loop() {
  static uint8_t colorIndex     = 0;
  static uint8_t nextColorIndex = 1;
  static uint8_t step           = 0;
  const uint8_t  maxSteps       = 100;

  uint32_t colors[] = {
      strip.Color(255, 0, 0),  // Red
      strip.Color(0, 255, 0),  // Green
      strip.Color(0, 0, 255)   // Blue
  };

  strip.clear();
  strip.setPixelColor(
      0, fadeColor(colors[colorIndex], colors[nextColorIndex], step, maxSteps));
  strip.show();
  delay(20);

  step++;
  if (step > maxSteps) {
    step           = 0;
    colorIndex     = nextColorIndex;
    nextColorIndex = (nextColorIndex + 1) % 3;
  }
}