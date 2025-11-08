#ifndef MOCK_MAIN_BLE_BRIDGE_HPP
#define MOCK_MAIN_BLE_BRIDGE_HPP

#include "phoenix_ble_server.hpp"

#ifdef __cplusplus
extern "C" {
#endif

PhoenixBleStatus mock_main_ble_bridge_initialize(PhoenixBleServerContext* context);
PhoenixBleStatus mock_main_ble_bridge_process_command(const uint8_t* payload, uint16_t payload_length);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_MAIN_BLE_BRIDGE_HPP */
