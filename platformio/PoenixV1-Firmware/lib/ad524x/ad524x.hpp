#ifndef AD524X_HPP
#define AD524X_HPP

#include <stdint.h>

class TwoWire;

// ===================== Return Codes ============================================
#define AD524X_OK 0
#define AD524X_ERR_INVALID_ARG -1
#define AD524X_ERR_I2C -2
#define AD524X_ERR_TIMEOUT -3
#define AD524X_ERR_NOT_INITIALIZED -4
#define AD524X_ERR_NOT_IMPLEMENTED -5

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

#endif  // AD524X_HPP
