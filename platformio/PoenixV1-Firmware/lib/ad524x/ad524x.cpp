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
  // Step 1: Reject initialisation with a missing I2C dependency.
  if (wire_bus == NULL) {
    return AD524X_ERR_INVALID_ARG;
  }

  // AD0/AD1 strap pins map to addresses 0x2C-0x2F (01011ab). Anything else is a
  // wiring/configuration error that should be surfaced to callers immediately.
  // Step 2: Guard against invalid strap combinations so the caller sees an explicit error code.
  if (i2c_address < 0x2Cu || i2c_address > 0x2Fu) {
    return AD524X_ERR_INVALID_ARG;
  }

  // Step 3: Record the configuration so later operations can talk to the device.
  g_driver_state.i2c_address = i2c_address;
  g_driver_state.wire_bus    = wire_bus;
  g_driver_state.initialized = true;
  return AD524X_OK;
}

bool ad524x_is_initialized(void) {
  return g_driver_state.initialized;
}

void ad524x_deinitialize(void) {
  // Step 1: Clear the cached configuration so future calls fail fast.
  g_driver_state.i2c_address = 0u;
  g_driver_state.wire_bus    = NULL;
  g_driver_state.initialized = false;
}

int ad524x_build_instruction(uint8_t channel, bool midscale, bool shutdown, uint8_t* instruction_out) {
  // Step 1: Guard against missing output storage.
  if (instruction_out == NULL) {
    return AD524X_ERR_INVALID_ARG;
  }

  // Step 2: Channels other than 0/1 are not supported on the AD5242 package.
  if (channel > 1u) {
    return AD524X_ERR_INVALID_ARG;
  }

  // Step 3: Construct the instruction byte with the requested modifiers.
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
  // Step 1: Ensure the driver was initialised before touching hardware.
  if (!g_driver_state.initialized) {
    return AD524X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Stage the transaction on the configured I2C address.
  TwoWire* wire = g_driver_state.wire_bus;
  wire->beginTransmission(g_driver_state.i2c_address);

  // Step 3: Send the instruction byte and abort if it fails to queue.
  const size_t instruction_written = wire->write(instruction);
  if (instruction_written != 1u) {
    wire->endTransmission(true);
    return AD524X_ERR_I2C;
  }

  // Step 4: Send the payload byte and close out the transfer.
  const size_t  data_written = wire->write(data);
  const uint8_t tx_status    = wire->endTransmission(true);

  // Step 5: Translate the hardware responses into driver error codes.
  if (data_written != 1u) {
    return AD524X_ERR_I2C;
  }

  if (tx_status != 0u) {
    return AD524X_ERR_I2C;
  }

  return AD524X_OK;
}

int ad524x_read_frame(uint8_t instruction, uint8_t* data_out) {
  // Step 1: Confirm we have somewhere to store the returned byte.
  if (data_out == NULL) {
    return AD524X_ERR_INVALID_ARG;
  }

  // Step 2: All transactions require a prior initialisation call.
  if (!g_driver_state.initialized) {
    return AD524X_ERR_NOT_INITIALIZED;
  }

  // Step 3: Queue the instruction byte using a repeated-start so we can read the result.
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

  // Step 4: Request the data byte and translate common failure modes.
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

int ad524x_set_wiper(uint8_t channel, uint8_t value) {
  // Step 1: Require initialisation before touching the hardware.
  if (!g_driver_state.initialized) {
    return AD524X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Build the instruction for the requested channel.
  uint8_t instruction = 0u;
  int     status      = ad524x_build_instruction(channel, false, false, &instruction);
  if (status != AD524X_OK) {
    return status;
  }

  // Step 3: Dispatch the new wiper value to the device.
  return ad524x_write_frame(instruction, value);
}

int ad524x_get_wiper(uint8_t channel, uint8_t* value_out) {
  // Step 1: Reject calls lacking storage for the readback value.
  if (value_out == NULL) {
    return AD524X_ERR_INVALID_ARG;
  }

  // Step 2: Ensure the driver has been initialised.
  if (!g_driver_state.initialized) {
    return AD524X_ERR_NOT_INITIALIZED;
  }

  // Step 3: Build the instruction and issue a read transaction.
  uint8_t instruction = 0u;
  int     status      = ad524x_build_instruction(channel, false, false, &instruction);
  if (status != AD524X_OK) {
    return status;
  }

  return ad524x_read_frame(instruction, value_out);
}

int ad524x_set_midscale(uint8_t channel) {
  // Step 1: Ensure the hardware handle was configured.
  if (!g_driver_state.initialized) {
    return AD524X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Build an instruction that targets the requested channel in midscale mode.
  uint8_t instruction = 0u;
  int     status      = ad524x_build_instruction(channel, true, false, &instruction);
  if (status != AD524X_OK) {
    return status;
  }

  // Step 3: Write the midscale command; data payload is ignored per datasheet.
  return ad524x_write_frame(instruction, 0x00u);
}

int ad524x_shutdown(uint8_t channel, bool enable) {
  // Step 1: Verify the driver is initialised before issuing commands.
  if (!g_driver_state.initialized) {
    return AD524X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Capture the current wiper value so shutdown cycles preserve state.
  uint8_t wiper  = 0u;
  int     status = ad524x_get_wiper(channel, &wiper);
  if (status != AD524X_OK) {
    return status;
  }

  // Step 3: Construct the shutdown instruction and honour the enable flag.
  uint8_t instruction = 0u;
  status              = ad524x_build_instruction(channel, false, enable, &instruction);
  if (status != AD524X_OK) {
    return status;
  }

  // Step 4: Re-send the prior wiper code under the new shutdown configuration.
  return ad524x_write_frame(instruction, wiper);
}
