#ifndef PHOENIX_BLE_PHOENIX_BLE_STACK_HPP
#define PHOENIX_BLE_PHOENIX_BLE_STACK_HPP

#include "phoenix_ble_server.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Install the default hardware backend used by the Phoenix BLE stack.
 *
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_stack_install_default_backend(void);

/**
 * @brief Generate the shared Phoenix BLE configuration used by production and mock applications.
 *
 * @return PhoenixBleConfig Configuration populated with device identity, UUIDs, and connection preferences.
 */
PhoenixBleConfig phoenix_ble_stack_make_default_config(void);

/**
 * @brief Initialise the Phoenix BLE server facade with the shared backend and default configuration.
 *
 * @param context Pointer to the caller-owned server context that tracks runtime state.
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_stack_initialize(PhoenixBleServerContext* context);

/**
 * @brief Begin advertising using the shared Phoenix BLE configuration.
 *
 * @param context Pointer to the initialised Phoenix BLE server context.
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_stack_start_advertising(PhoenixBleServerContext* context);

/**
 * @brief Stop advertising that was previously started by the Phoenix BLE stack helpers.
 *
 * @param context Pointer to the Phoenix BLE server context controlling advertising state.
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_stack_stop_advertising(PhoenixBleServerContext* context);

#ifdef __cplusplus
}
#endif

#endif  // PHOENIX_BLE_PHOENIX_BLE_STACK_HPP
