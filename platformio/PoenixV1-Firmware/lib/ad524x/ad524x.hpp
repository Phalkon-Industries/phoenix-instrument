#ifndef AD524X_HPP
#define AD524X_HPP

#include "phoenix_guard.hpp"
#include <AD524x.h>
#include <stdint.h>

class TwoWire;

// ===================== Return Codes ============================================
#ifdef AD524X_OK
#undef AD524X_OK
#endif
#define AD524X_OK PHX_OK

#ifdef AD524X_ERR_INVALID_ARG
#undef AD524X_ERR_INVALID_ARG
#endif
#define AD524X_ERR_INVALID_ARG PHX_ERR_INVALID_ARG

#ifdef AD524X_ERR_I2C
#undef AD524X_ERR_I2C
#endif
#define AD524X_ERR_I2C PHX_ERR_COMMUNICATION

#ifdef AD524X_ERR_TIMEOUT
#undef AD524X_ERR_TIMEOUT
#endif
#define AD524X_ERR_TIMEOUT PHX_ERR_TIMEOUT

#ifdef AD524X_ERR_NOT_INITIALIZED
#undef AD524X_ERR_NOT_INITIALIZED
#endif
#define AD524X_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED

#ifdef AD524X_ERR_NOT_IMPLEMENTED
#undef AD524X_ERR_NOT_IMPLEMENTED
#endif
#define AD524X_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED

/**
 * @brief Initialise shared driver state for the dual-channel AD524x family.
 *
 * This helper validates arguments, caches the target address, and records the
 * caller-provided I²C handle for future transactions. Application firmware is
 * responsible for invoking `Wire.begin()` (or equivalent) *before* calling this
 * API so bus configuration happens exactly once at the system level. The
 * hardware exposes two independent RDAC channels; @p i2c_address encodes the
 * AD0/AD1 strap selection that distinguishes them. The general-purpose outputs
 * O1/O2 are intentionally left unsupported.
 *
 * @param i2c_address 7-bit device address (0x2C-0x2F depending on AD0/AD1).
 * @param wire_bus    Pointer to an already initialised Wire/TWI interface.
 * @return AD524X_OK once arguments are accepted and state cached, or a negative
 *         error code on invalid inputs.
 */
int ad524x_initialize(uint8_t i2c_address, TwoWire* wire_bus);

/**
 * @brief Report whether the driver has been initialised successfully.
 *
 * Returns true only after `ad524x_initialize()` accepts valid arguments. Callers
 * should gate all other driver APIs on this helper so we avoid issuing bus
 * transactions before the hardware address and Wire handle are configured.
 */
bool ad524x_is_initialized(void);

/**
 * @brief Deinitialise the driver, clearing cached address and bus handle.
 *
 * This helper is primarily intended for tests or shutdown flows that need to
 * reset the driver to an uninitialised state without touching the I²C bus.
 */
void ad524x_deinitialize(void);

/**
 * @brief Build an instruction byte for the AD524x command set.
 *
 * @param channel    RDAC channel to address (0 or 1).
 * @param midscale   When true, set the midscale reset bit (RS) in the
 *                   instruction.
 * @param shutdown   When true, assert the shutdown bit (SD) in the instruction.
 * @param instruction_out Destination pointer that receives the assembled
 *                         instruction byte.
 * @return AD524X_OK on success, AD524X_ERR_INVALID_ARG on invalid inputs.
 */
int ad524x_build_instruction(uint8_t channel, bool midscale, bool shutdown, uint8_t* instruction_out);

/**
 * @brief Write an instruction/data frame to the device.
 *
 * @param instruction Instruction byte assembled via `ad524x_build_instruction`.
 * @param data        Data byte to write (wiper code on standard operations).
 * @return AD524X_OK on success, or a negative error code when the driver is not
 *         initialised or the I²C transaction fails.
 */
int ad524x_write_frame(uint8_t instruction, uint8_t data);

/**
 * @brief Read a single data byte from the device using a repeated-start
 *        transaction.
 *
 * @param instruction Instruction byte to send before the read phase.
 * @param data_out    Destination pointer that receives the read byte.
 * @return AD524X_OK on success, AD524X_ERR_INVALID_ARG for null buffers,
 *         AD524X_ERR_NOT_INITIALIZED if the driver is not ready, or a negative
 *         error code representing bus failures/timeouts.
 */
int ad524x_read_frame(uint8_t instruction, uint8_t* data_out);

/**
 * @brief Write a new wiper code to the selected RDAC channel.
 *
 * @param channel Target RDAC channel (0 or 1).
 * @param value   8-bit wiper code to program.
 * @return AD524X_OK on success or a negative error code on validation or bus
 *         failures.
 */
int ad524x_set_wiper(uint8_t channel, uint8_t value);

/**
 * @brief Read the current wiper code from the selected channel.
 *
 * @param channel   Target RDAC channel (0 or 1).
 * @param value_out Destination pointer that receives the wiper code.
 * @return AD524X_OK on success or a negative error code on validation or bus
 *         failures.
 */
int ad524x_get_wiper(uint8_t channel, uint8_t* value_out);

/**
 * @brief Command the device to jump the specified channel to midscale (0x80).
 *
 * @param channel Target RDAC channel (0 or 1).
 * @return AD524X_OK on success or a negative error code.
 */
int ad524x_set_midscale(uint8_t channel);

/**
 * @brief Toggle the shutdown bit for the selected channel while preserving the
 *        current wiper code.
 *
 * When @p enable is true the channel enters shutdown; when false it resumes
 * normal operation.
 *
 * @param channel Target RDAC channel (0 or 1).
 * @param enable  true to assert shutdown, false to clear it.
 * @return AD524X_OK on success or a negative error code.
 */
int ad524x_shutdown(uint8_t channel, bool enable);

#endif  // AD524X_HPP
