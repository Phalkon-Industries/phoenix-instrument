#ifndef PHOENIX_BLE_PHOENIX_BLE_BLUEFRUIT_BACKEND_HPP
#define PHOENIX_BLE_PHOENIX_BLE_BLUEFRUIT_BACKEND_HPP

#include "phoenix_ble_server.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the Bluefruit-backed implementation with the Phoenix BLE facade.
 *
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_bluefruit_install_backend(void);

#ifdef __cplusplus
}
#endif

#endif  // PHOENIX_BLE_PHOENIX_BLE_BLUEFRUIT_BACKEND_HPP
