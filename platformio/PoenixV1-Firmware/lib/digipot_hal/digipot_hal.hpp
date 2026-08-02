#ifndef DIGIPOT_HAL_HPP
#define DIGIPOT_HAL_HPP

#include "phoenix_guard.hpp"
#include <stdint.h>

// ===================== Return Codes ============================================
#ifdef DIGIPOT_HAL_OK
#undef DIGIPOT_HAL_OK
#endif
#define DIGIPOT_HAL_OK PHX_OK

#ifdef DIGIPOT_HAL_ERR_INVALID_ARG
#undef DIGIPOT_HAL_ERR_INVALID_ARG
#endif
#define DIGIPOT_HAL_ERR_INVALID_ARG PHX_ERR_INVALID_ARG

#ifdef DIGIPOT_HAL_ERR_NOT_INITIALIZED
#undef DIGIPOT_HAL_ERR_NOT_INITIALIZED
#endif
#define DIGIPOT_HAL_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED

#ifdef DIGIPOT_HAL_ERR_NOT_IMPLEMENTED
#undef DIGIPOT_HAL_ERR_NOT_IMPLEMENTED
#endif
#define DIGIPOT_HAL_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED

// ===================== Wiper Resolution Limits =================================
#define DIGIPOT_BLUE_MAX_WIPER 1023u /**< MCP41U83T 10-bit resolution */
#define DIGIPOT_GREEN_MAX_WIPER 255u /**< AD5242 8-bit resolution */

/**
 * @brief Initialize the blue LED digipot (MCP41U83T).
 *
 * @param i2c_address 7-bit I2C address (0x2C-0x2F).
 * @param wire_bus Pointer to the I2C bus instance.
 * @return DIGIPOT_HAL_OK on success, or a negative error code.
 */
int digipot_blue_initialize(uint8_t i2c_address, void* wire_bus);

/**
 * @brief Set the blue LED wiper position.
 *
 * @param code 10-bit wiper code (0-1023).
 * @return DIGIPOT_HAL_OK on success, or a negative error code.
 */
int digipot_blue_set_wiper(uint16_t code);

/**
 * @brief Read the blue LED wiper position.
 *
 * @param code_out Destination for the 10-bit wiper code.
 * @return DIGIPOT_HAL_OK on success, or a negative error code.
 */
int digipot_blue_read_wiper(uint16_t* code_out);

/**
 * @brief Initialize the green LED digipot (AD5242).
 *
 * @param i2c_address 7-bit I2C address (0x2C-0x2F).
 * @param channel Channel selection (0 or 1).
 * @param wire_bus Pointer to the I2C bus instance.
 * @return DIGIPOT_HAL_OK on success, or a negative error code.
 */
int digipot_green_initialize(uint8_t i2c_address, uint8_t channel, void* wire_bus);

/**
 * @brief Set the green LED wiper position.
 *
 * @param code 8-bit wiper code (0-255).
 * @return DIGIPOT_HAL_OK on success, or a negative error code.
 */
int digipot_green_set_wiper(uint16_t code);

/**
 * @brief Read the green LED wiper position.
 *
 * @param code_out Destination for the 8-bit wiper code.
 * @return DIGIPOT_HAL_OK on success, or a negative error code.
 */
int digipot_green_read_wiper(uint16_t* code_out);

/**
 * @brief Enable or disable shutdown mode for the green LED digipot.
 *
 * @param enable True to enable shutdown, false to disable.
 * @return DIGIPOT_HAL_OK on success, or a negative error code.
 */
int digipot_green_shutdown(bool enable);

#endif  // DIGIPOT_HAL_HPP
