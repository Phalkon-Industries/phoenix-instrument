#include "ad524x.hpp"

#include <Arduino.h>
#include <Wire.h>

// ===================== Driver expectations ====================================
// - Application code must call Wire.begin() exactly once before initialising the
//   driver. Reissuing Wire.begin() inside a library can disturb existing bus
//   users, so we treat the TwoWire pointer as a dependency that is injected at
//   init time.
// - Firmware targets the dual-channel AD5242 configuration. Phoenix boards tie
//   AD0/AD1 low, so the default 7-bit address is 0x2C; channel selection relies
//   on the A/B bit in the instruction byte. Single-channel AD5241 boards are
//   treated as the channel-0 subset.
// - The silicon exposes two general-purpose outputs (O1/O2). Phoenix hardware
//   leaves them unconnected, so the driver will document the omission and skip
//   any control hooks for those pins.

static uint8_t  g_i2c_address = 0u;
static TwoWire* g_wire_bus    = NULL;
static bool     g_initialized = false;

int ad524x_initialize(uint8_t i2c_address, TwoWire* wire_bus) {
  if (wire_bus == NULL) {
    return AD524X_ERR_INVALID_ARG;
  }

  // AD0/AD1 strap pins map to addresses 0x2C-0x2F (01011ab). Anything else is a
  // wiring/configuration error that should be surfaced to callers immediately.
  if (i2c_address < 0x2Cu || i2c_address > 0x2Fu) {
    return AD524X_ERR_INVALID_ARG;
  }

  g_i2c_address = i2c_address;
  g_wire_bus    = wire_bus;
  g_initialized = true;
  return AD524X_OK;
}
