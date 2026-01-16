#ifndef PHOENIX_SETTINGS_HPP
#define PHOENIX_SETTINGS_HPP

#include "phoenix_guard.hpp"
#include <stdint.h>

// ===================== Return Codes =============================================
#define PHOENIX_SETTINGS_OK PHX_OK
#define PHOENIX_SETTINGS_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define PHOENIX_SETTINGS_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define PHOENIX_SETTINGS_ERR_STORAGE PHX_ERR_COMMUNICATION

// ===================== Default Wiper Codes ======================================
// Factory default digipot wiper codes. These values are used when no settings file
// exists or when settings are explicitly reset. They match the compile-time defaults
// in device_setup.cpp for consistency.
#define PHOENIX_SETTINGS_DEFAULT_BLUE_WIPER 0xFFu
#define PHOENIX_SETTINGS_DEFAULT_GREEN_WIPER 0xD3u

// ===================== Settings Version =========================================
// Hardcoded settings format version. Increment this when the PhoenixSettings struct
// changes in a way that requires migration. Settings files with a different version
// are discarded and replaced with defaults.
#define PHOENIX_SETTINGS_FORMAT_VERSION 1u

/**
 * @brief Persistent device settings stored in internal flash.
 *
 * This structure is serialized to flash storage. Fields are ordered for alignment
 * efficiency and include reserved bytes for future expansion without breaking
 * existing stored data.
 */
struct PhoenixSettings {
  uint8_t blue_wiper_code;  /**< Calibrated digipot wiper code for blue LED channel. */
  uint8_t green_wiper_code; /**< Calibrated digipot wiper code for green LED channel. */
  uint8_t reserved[6];      /**< Reserved for future fields; zeroed on creation. */
};

/**
 * @brief Initialize the settings module and load from flash.
 *
 * If no settings file exists or the stored version differs from the current format
 * version, default settings are created and persisted. This function must be called
 * before any other settings API.
 *
 * @return PHOENIX_SETTINGS_OK on success, or a negative error code on failure.
 */
int phoenix_settings_initialize(void);

/**
 * @brief Check whether the settings module has been initialized.
 *
 * @return True if initialized, false otherwise.
 */
bool phoenix_settings_is_initialized(void);

/**
 * @brief Get a const pointer to the cached settings in RAM.
 *
 * This returns the current in-memory settings without accessing flash. The pointer
 * remains valid until the module is deinitialized.
 *
 * @return Pointer to cached settings, or NULL if not initialized.
 */
const PhoenixSettings* phoenix_settings_get(void);

/**
 * @brief Update settings in RAM and persist to flash.
 *
 * The provided settings are copied to the internal cache and then written to flash
 * storage. The operation is atomic with respect to power loss (LittleFS guarantee).
 *
 * @param settings Pointer to the new settings to save.
 * @return PHOENIX_SETTINGS_OK on success, or a negative error code on failure.
 */
int phoenix_settings_save(const PhoenixSettings* settings);

/**
 * @brief Apply cached wiper codes to the digipot hardware.
 *
 * Reads the blue and green wiper codes from cached settings and writes them to the
 * AD524x digipot. Typically called after initialization or calibration.
 *
 * @return PHOENIX_SETTINGS_OK on success, or a negative error code on hardware failure.
 */
int phoenix_settings_apply_wiper_codes(void);

/**
 * @brief Reset settings to factory defaults and persist to flash.
 *
 * Overwrites the current settings with compile-time defaults and saves them.
 *
 * @return PHOENIX_SETTINGS_OK on success, or a negative error code on failure.
 */
int phoenix_settings_reset_to_defaults(void);

/**
 * @brief Deinitialize the settings module.
 *
 * Clears the internal cache and marks the module as uninitialized. Primarily used
 * for testing or controlled shutdown scenarios.
 */
void phoenix_settings_deinitialize(void);

#endif  // PHOENIX_SETTINGS_HPP
