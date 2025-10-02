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

struct Ad524xDriverState {
  uint8_t  i2c_address;
  TwoWire* wire_bus;
  bool     initialized;
};

static Ad524xDriverState g_driver_state = {0u, NULL, false};

static const uint8_t k_instruction_channel_bit = 0x80u;
static const uint8_t k_instruction_midscale    = 0x40u;
static const uint8_t k_instruction_shutdown    = 0x20u;

int ad524x_initialize(uint8_t i2c_address, TwoWire* wire_bus) {
  if (wire_bus == NULL) {
    return AD524X_ERR_INVALID_ARG;
  }

  // AD0/AD1 strap pins map to addresses 0x2C-0x2F (01011ab). Anything else is a
  // wiring/configuration error that should be surfaced to callers immediately.
  if (i2c_address < 0x2Cu || i2c_address > 0x2Fu) {
    return AD524X_ERR_INVALID_ARG;
  }

  g_driver_state.i2c_address = i2c_address;
  g_driver_state.wire_bus    = wire_bus;
  g_driver_state.initialized = true;
  return AD524X_OK;
}

bool ad524x_is_initialized(void) {
  return g_driver_state.initialized;
}

int ad524x_build_instruction(uint8_t channel, bool midscale, bool shutdown, uint8_t* instruction_out) {
  if (instruction_out == NULL) {
    return AD524X_ERR_INVALID_ARG;
  }

  if (channel > 1u) {
    return AD524X_ERR_INVALID_ARG;
  }

  uint8_t instruction = 0u;
  if (channel == 1u) {
    instruction |= k_instruction_channel_bit;
  }
  if (midscale) {
    instruction |= k_instruction_midscale;
  }
  if (shutdown) {
    instruction |= k_instruction_shutdown;
  }

  *instruction_out = instruction;
  return AD524X_OK;
}

int ad524x_write_frame(uint8_t instruction, uint8_t data) {
  if (!g_driver_state.initialized) {
    return AD524X_ERR_NOT_INITIALIZED;
  }

  TwoWire* wire = g_driver_state.wire_bus;
  wire->beginTransmission(g_driver_state.i2c_address);

  const size_t instruction_written = wire->write(instruction);
  if (instruction_written != 1u) {
    wire->endTransmission(true);
    return AD524X_ERR_I2C;
  }

  const size_t  data_written = wire->write(data);
  const uint8_t tx_status    = wire->endTransmission(true);

  if (data_written != 1u) {
    return AD524X_ERR_I2C;
  }

  if (tx_status != 0u) {
    return AD524X_ERR_I2C;
  }

  return AD524X_OK;
}

int ad524x_read_frame(uint8_t instruction, uint8_t* data_out) {
  if (data_out == NULL) {
    return AD524X_ERR_INVALID_ARG;
  }

  if (!g_driver_state.initialized) {
    return AD524X_ERR_NOT_INITIALIZED;
  }

  TwoWire* wire = g_driver_state.wire_bus;
  wire->beginTransmission(g_driver_state.i2c_address);

  const size_t instruction_written = wire->write(instruction);
  if (instruction_written != 1u) {
    wire->endTransmission(true);
    return AD524X_ERR_I2C;
  }

  const uint8_t tx_status = wire->endTransmission(false);
  if (tx_status != 0u) {
    return AD524X_ERR_I2C;
  }

  const size_t bytes_read = wire->requestFrom(g_driver_state.i2c_address, static_cast<uint8_t>(1u), true);
  if (bytes_read != 1u) {
    return AD524X_ERR_TIMEOUT;
  }

  const int value = wire->read();
  if (value < 0) {
    return AD524X_ERR_TIMEOUT;
  }

  *data_out = static_cast<uint8_t>(value);
  return AD524X_OK;
}
