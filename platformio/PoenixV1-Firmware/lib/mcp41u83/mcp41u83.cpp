#include "mcp41u83.hpp"

#include <Arduino.h>
#include <Wire.h>

// ===================== Driver expectations ====================================
// - Application code must call Wire.begin() exactly once before initialising the
//   driver. Reissuing Wire.begin() inside a library can disturb existing bus
//   users, so we treat the TwoWire pointer as a dependency that is injected at
//   init time.
// - Firmware targets the MCP41U83T single-channel 10-bit digital potentiometer.
//   The 7-bit I2C address is determined by the A0/A1 strap pins (0x2C-0x2F).
// - The device uses a 3-byte protocol for writes (command + 2 data bytes) and
//   a repeated-start read protocol (command + repeated start + 2 data bytes).
// - CRC is disabled by default (POR state, CRCEN=0).

struct Mcp41u83DriverState {
  uint8_t  i2c_address;
  TwoWire* wire_bus;
  bool     initialized;
};

static Mcp41u83DriverState g_driver_state = {0u, NULL, false};

// Command byte constants per datasheet Table 4-3
static const uint8_t k_cmd_volatile_wiper0_write = 0b00001000;  // Address 00001b, command 000b
static const uint8_t k_cmd_volatile_wiper0_read  = 0b00001110;  // Address 00001b, command 110b

int mcp41u83_initialize(uint8_t i2c_address, TwoWire* wire_bus) {
  // Step 1: Reject initialisation with a missing I2C dependency.
  GUARD_NONNULL(wire_bus);

  // A0/A1 strap pins map to addresses 0x2C-0x2F (01011ab). Anything else is a
  // wiring/configuration error that should be surfaced to callers immediately.
  // Step 2: Guard against invalid strap combinations so the caller sees an explicit error code.
  if (i2c_address < 0x2Cu || i2c_address > 0x2Fu) {
    return MCP41U83_ERR_INVALID_ARG;
  }

  // Step 3: Record the configuration so later operations can talk to the device.
  g_driver_state.i2c_address = i2c_address;
  g_driver_state.wire_bus    = wire_bus;
  g_driver_state.initialized = true;
  return MCP41U83_OK;
}

bool mcp41u83_is_initialized(void) {
  return g_driver_state.initialized;
}

void mcp41u83_deinitialize(void) {
  // Step 1: Clear the cached configuration so future calls fail fast.
  g_driver_state.i2c_address = 0u;
  g_driver_state.wire_bus    = NULL;
  g_driver_state.initialized = false;
}

int mcp41u83_build_write_frame(uint16_t code, uint8_t* cmd_out, uint8_t* data_hi_out, uint8_t* data_lo_out) {
  // Step 1: Guard against missing output storage.
  GUARD_NONNULL(cmd_out);
  GUARD_NONNULL(data_hi_out);
  GUARD_NONNULL(data_lo_out);

  // Step 2: Validate the 10-bit code range (0-1023).
  if (code > 1023u) {
    return MCP41U83_ERR_INVALID_ARG;
  }

  // Step 3: Build the frame per datasheet §6.7.6.1.
  // Command byte is always 0x00 for volatile wiper write.
  *cmd_out = k_cmd_volatile_wiper0_write;

  // High byte contains bits D9:D8 in positions 1:0.
  *data_hi_out = static_cast<uint8_t>((code >> 8u) & 0x03u);

  // Low byte contains bits D7:D0.
  *data_lo_out = static_cast<uint8_t>(code & 0xFFu);

  return MCP41U83_OK;
}

int mcp41u83_set_wiper(uint16_t code) {
  // Step 1: Ensure the driver was initialised before touching hardware.
  GUARD_INITIALIZED(g_driver_state.initialized);

  // Step 2: Validate the 10-bit code range.
  if (code > 1023u) {
    return MCP41U83_ERR_INVALID_ARG;
  }

  // Step 3: Build the write frame.
  uint8_t cmd     = 0u;
  uint8_t data_hi = 0u;
  uint8_t data_lo = 0u;
  int     result  = mcp41u83_build_write_frame(code, &cmd, &data_hi, &data_lo);
  if (result != MCP41U83_OK) {
    return result;
  }

  // Step 4: Stage the transaction on the configured I2C address.
  TwoWire* wire = g_driver_state.wire_bus;
  wire->beginTransmission(g_driver_state.i2c_address);

  // Step 5: Send the command byte and abort if it fails to queue.
  const size_t cmd_written = wire->write(cmd);
  if (cmd_written != 1u) {
    wire->endTransmission(true);
    return MCP41U83_ERR_I2C;
  }

  // Step 6: Send the high data byte.
  const size_t hi_written = wire->write(data_hi);
  if (hi_written != 1u) {
    wire->endTransmission(true);
    return MCP41U83_ERR_I2C;
  }

  // Step 7: Send the low data byte and close out the transfer.
  const size_t  lo_written = wire->write(data_lo);
  const uint8_t tx_status  = wire->endTransmission(true);

  // Step 8: Translate the hardware responses into driver error codes.
  if (lo_written != 1u) {
    return MCP41U83_ERR_I2C;
  }

  if (tx_status != 0u) {
    return MCP41U83_ERR_I2C;
  }

  return MCP41U83_OK;
}

int mcp41u83_read_wiper(uint16_t* code_out) {
  // Step 1: Confirm we have somewhere to store the returned code.
  GUARD_NONNULL(code_out);

  // Step 2: All transactions require a prior initialisation call.
  GUARD_INITIALIZED(g_driver_state.initialized);

  // Step 3: Queue the command byte using a repeated-start so we can read the result.
  TwoWire* wire = g_driver_state.wire_bus;
  wire->beginTransmission(g_driver_state.i2c_address);

  const size_t cmd_written = wire->write(k_cmd_volatile_wiper0_read);
  if (cmd_written != 1u) {
    wire->endTransmission(true);
    return MCP41U83_ERR_I2C;
  }

  const uint8_t tx_status = wire->endTransmission(false);
  if (tx_status != 0u) {
    return MCP41U83_ERR_I2C;
  }

  // Step 4: Request two data bytes (high and low) per datasheet §6.7.7.1.
  const size_t bytes_read = wire->requestFrom(g_driver_state.i2c_address, static_cast<uint8_t>(2u), true);
  if (bytes_read != 2u) {
    return MCP41U83_ERR_TIMEOUT;
  }

  const int hi_value = wire->read();
  if (hi_value < 0) {
    return MCP41U83_ERR_TIMEOUT;
  }

  const int lo_value = wire->read();
  if (lo_value < 0) {
    return MCP41U83_ERR_TIMEOUT;
  }

  // Step 5: Reconstruct the 10-bit code from the two bytes.
  const uint8_t data_hi = static_cast<uint8_t>(hi_value);
  const uint8_t data_lo = static_cast<uint8_t>(lo_value);
  *code_out             = static_cast<uint16_t>(((data_hi & 0x03u) << 8u) | data_lo);

  return MCP41U83_OK;
}
