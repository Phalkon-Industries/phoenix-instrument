#ifndef MCP41U83_HPP
#define MCP41U83_HPP

#include "digipot_hal.hpp"
#include "phoenix_guard.hpp"
#include <stdint.h>

class TwoWire;

// ===================== Return Codes ============================================
#ifdef MCP41U83_OK
#undef MCP41U83_OK
#endif
#define MCP41U83_OK PHX_OK

#ifdef MCP41U83_ERR_INVALID_ARG
#undef MCP41U83_ERR_INVALID_ARG
#endif
#define MCP41U83_ERR_INVALID_ARG PHX_ERR_INVALID_ARG

#ifdef MCP41U83_ERR_I2C
#undef MCP41U83_ERR_I2C
#endif
#define MCP41U83_ERR_I2C PHX_ERR_COMMUNICATION

#ifdef MCP41U83_ERR_TIMEOUT
#undef MCP41U83_ERR_TIMEOUT
#endif
#define MCP41U83_ERR_TIMEOUT PHX_ERR_TIMEOUT

#ifdef MCP41U83_ERR_NOT_INITIALIZED
#undef MCP41U83_ERR_NOT_INITIALIZED
#endif
#define MCP41U83_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED

#ifdef MCP41U83_ERR_NOT_IMPLEMENTED
#undef MCP41U83_ERR_NOT_IMPLEMENTED
#endif
#define MCP41U83_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED

/**
 * @brief Initialise the MCP41U83T 10-bit digital potentiometer driver.
 *
 * This helper validates arguments, caches the target address, and records the
 * caller-provided I²C handle for future transactions. Application firmware is
 * responsible for invoking `Wire.begin()` (or equivalent) *before* calling this
 * API so bus configuration happens exactly once at the system level.
 *
 * The MCP41U83T is a single-channel 10-bit digital potentiometer with I²C
 * interface. The device address is determined by A0/A1 strap pins (0x2C-0x2F).
 *
 * @param i2c_address 7-bit device address (0x2C-0x2F depending on A0/A1).
 * @param wire_bus    Pointer to an already initialised Wire/TWI interface.
 * @return MCP41U83_OK once arguments are accepted and state cached, or a negative
 *         error code on invalid inputs.
 */
int mcp41u83_initialize(uint8_t i2c_address, TwoWire* wire_bus);

/**
 * @brief Report whether the driver has been initialised successfully.
 *
 * Returns true only after `mcp41u83_initialize()` accepts valid arguments. Callers
 * should gate all other driver APIs on this helper so we avoid issuing bus
 * transactions before the hardware address and Wire handle are configured.
 */
bool mcp41u83_is_initialized(void);

/**
 * @brief Deinitialise the driver, clearing cached address and bus handle.
 *
 * This helper is primarily intended for tests or shutdown flows that need to
 * reset the driver to an uninitialised state without touching the I²C bus.
 */
void mcp41u83_deinitialize(void);

/**
 * @brief Build a write frame for the MCP41U83T volatile wiper register.
 *
 * Per datasheet §6.7.6.1 and Table 4-3, the write command uses:
 * - Command byte: 0x00 (Volatile Wiper0 address 00001b, write command 000b)
 * - Data high byte: upper 2 bits of 10-bit code (bits D9:D8)
 * - Data low byte: lower 8 bits of 10-bit code (bits D7:D0)
 *
 * @param code        10-bit wiper code (0-1023).
 * @param cmd_out     Destination for command byte (always 0x00 for volatile write).
 * @param data_hi_out Destination for high data byte.
 * @param data_lo_out Destination for low data byte.
 * @return MCP41U83_OK on success, MCP41U83_ERR_INVALID_ARG on invalid inputs.
 */
int mcp41u83_build_write_frame(uint16_t code, uint8_t* cmd_out, uint8_t* data_hi_out, uint8_t* data_lo_out);

/**
 * @brief Set the wiper position to a 10-bit code (0-1023).
 *
 * @param code 10-bit wiper position (0-1023).
 * @return MCP41U83_OK on success, or a negative error code when the driver is not
 *         initialised, the code is out of range, or the I²C transaction fails.
 */
int mcp41u83_set_wiper(uint16_t code);

/**
 * @brief Read the current wiper position.
 *
 * Per datasheet §6.7.7.1, the read command uses:
 * - Command byte: 0x0C (Volatile Wiper0 address 00001b, read command 110b)
 * - Repeated start, then read two data bytes
 *
 * @param code_out Destination for the 10-bit wiper code.
 * @return MCP41U83_OK on success, or a negative error code when the driver is not
 *         initialised or the I²C transaction fails.
 */
int mcp41u83_read_wiper(uint16_t* code_out);

#endif  // MCP41U83_HPP
