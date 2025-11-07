#ifndef PHOENIX_BLE_PHOENIX_BLE_DATA_PACKING_HPP
#define PHOENIX_BLE_PHOENIX_BLE_DATA_PACKING_HPP

#include <stddef.h>

#include "phoenix_ble_server.hpp"

#ifdef __cplusplus
extern "C" {
#endif

PhoenixBleStatus phoenix_ble_pack_notification_message(const char* command,
                                                       const char* parameters_json,
                                                       char* buffer,
                                                       size_t buffer_capacity,
                                                       size_t* bytes_written);

#ifdef __cplusplus
}
#endif

#endif  // PHOENIX_BLE_PHOENIX_BLE_DATA_PACKING_HPP
