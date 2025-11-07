#ifndef PHOENIX_BLE_PHOENIX_BLE_SERVER_HPP
#define PHOENIX_BLE_PHOENIX_BLE_SERVER_HPP

#include <stdint.h>

#include "phoenix_guard.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t PhoenixBleStatus;

/**
 * @brief Callback prototype for inbound command payloads received over the BLE command characteristic.
 *
 * @param payload Pointer to the UTF-8 encoded request body supplied by the phone app.
 * @param payload_length Number of bytes available at @p payload.
 * @return PhoenixBleStatus Phoenix return code indicating whether the command was processed successfully.
 */
typedef PhoenixBleStatus (*PhoenixBleCommandHandler)(const uint8_t* payload, uint16_t payload_length);

typedef struct PhoenixBleCharacteristicIds {
    uint16_t command_characteristic_id;
    uint16_t notification_characteristic_id;
} PhoenixBleCharacteristicIds;

/**
 * @brief Configuration parameters supplied during server initialisation.
 */
typedef struct PhoenixBleConfig {
    const char* device_name;
    const char* service_uuid;
    const char* command_characteristic_uuid;
    const char* notification_characteristic_uuid;
    uint16_t preferred_mtu;
    uint16_t preferred_connection_interval_min_ms;
    uint16_t preferred_connection_interval_max_ms;
} PhoenixBleConfig;

typedef struct PhoenixBleServerContext {
    uint8_t is_initialized;
    PhoenixBleCharacteristicIds characteristic_ids;
    PhoenixBleCommandHandler command_handler;
} PhoenixBleServerContext;

/**
 * @brief Initialise the Phoenix BLE server facade and register the primary GATT service.
 *
 * @param context Pointer to the caller-owned context that tracks BLE state.
 * @param config Pointer to the configuration describing device name, service UUIDs, and preferred connection parameters.
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_server_initialize(PhoenixBleServerContext* context, const PhoenixBleConfig* config);
/**
 * @brief Begin advertising using the configured device name and service UUID.
 *
 * @param context Pointer to the initialised BLE server context.
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_server_start_advertising(PhoenixBleServerContext* context);
/**
 * @brief Stop the current advertising session.
 *
 * @param context Pointer to the BLE server context currently advertising.
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_server_stop_advertising(PhoenixBleServerContext* context);
/**
 * @brief Query the cached connection state as tracked by the BLE server facade.
 *
 * @param context Pointer to the BLE server context managing connection state.
 * @param is_connected Out parameter receiving 1 when a phone is connected, 0 otherwise.
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_server_is_connected(const PhoenixBleServerContext* context, uint8_t* is_connected);
/**
 * @brief Send a notification payload to the connected phone client.
 *
 * @param context Pointer to the BLE server context responsible for publishing notifications.
 * @param payload Pointer to the UTF-8 encoded payload to deliver to the phone.
 * @param payload_length Number of bytes to send from @p payload.
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_server_send_notification(PhoenixBleServerContext* context, const uint8_t* payload, uint16_t payload_length);
/**
 * @brief Register the command handler invoked when the phone issues write requests.
 *
 * @param context Pointer to the BLE server context that forwards write events.
 * @param handler Function pointer invoked with each inbound command payload.
 * @return PhoenixBleStatus Phoenix return code signalling success or the guard failure encountered.
 */
PhoenixBleStatus phoenix_ble_server_register_command_handler(PhoenixBleServerContext* context, PhoenixBleCommandHandler handler);

#ifdef __cplusplus
}
#endif

#endif  // PHOENIX_BLE_PHOENIX_BLE_SERVER_HPP
