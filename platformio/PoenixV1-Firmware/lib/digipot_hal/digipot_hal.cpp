#include "digipot_hal.hpp"

#include "ad524x.hpp"
#include "mcp41u83.hpp"
#include <stddef.h>

// Blue LED digipot (MCP41U83T) - 10-bit resolution
int digipot_blue_initialize(uint8_t i2c_address, void* wire_bus) {
  return mcp41u83_initialize(i2c_address, static_cast<TwoWire*>(wire_bus));
}

int digipot_blue_set_wiper(uint16_t code) {
  // Validate 10-bit range
  if (code > 1023u) {
    return DIGIPOT_HAL_ERR_INVALID_ARG;
  }
  return mcp41u83_set_wiper(code);
}

int digipot_blue_read_wiper(uint16_t* code_out) {
  GUARD_NONNULL(code_out);
  return mcp41u83_read_wiper(code_out);
}

// Green LED digipot (AD5242) - 8-bit resolution
int digipot_green_initialize(uint8_t i2c_address, uint8_t channel, void* wire_bus) {
  return ad524x_initialize(i2c_address, static_cast<TwoWire*>(wire_bus));
}

int digipot_green_set_wiper(uint16_t code) {
  // Validate 8-bit range
  if (code > 255u) {
    return DIGIPOT_HAL_ERR_INVALID_ARG;
  }
  return ad524x_set_wiper(1, static_cast<uint8_t>(code));  // Channel 1 for green LED
}

int digipot_green_read_wiper(uint16_t* code_out) {
  GUARD_NONNULL(code_out);
  uint8_t code   = 0;
  int     result = ad524x_get_wiper(1, &code);  // Channel 1 for green LED
  if (result == AD524X_OK) {
    *code_out = static_cast<uint16_t>(code);
  }
  return result;
}

int digipot_green_shutdown(bool enable) {
  return ad524x_shutdown(1, enable);  // Channel 1 for green LED
}
