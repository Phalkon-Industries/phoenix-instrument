#include "phoenix_ble_server.hpp"

#include "phoenix_guard.hpp"

PhoenixBleStatus phoenix_ble_server_initialize(PhoenixBleServerContext* context, const PhoenixBleConfig* config)
{
    (void) context;
    (void) config;
    return PHX_ERR_NOT_IMPLEMENTED;
}

PhoenixBleStatus phoenix_ble_server_start_advertising(PhoenixBleServerContext* context)
{
    (void) context;
    return PHX_ERR_NOT_IMPLEMENTED;
}

PhoenixBleStatus phoenix_ble_server_stop_advertising(PhoenixBleServerContext* context)
{
    (void) context;
    return PHX_ERR_NOT_IMPLEMENTED;
}

PhoenixBleStatus phoenix_ble_server_is_connected(const PhoenixBleServerContext* context, uint8_t* is_connected)
{
    (void) context;
    (void) is_connected;
    return PHX_ERR_NOT_IMPLEMENTED;
}

PhoenixBleStatus phoenix_ble_server_send_notification(PhoenixBleServerContext* context, const uint8_t* payload, uint16_t payload_length)
{
    (void) context;
    (void) payload;
    (void) payload_length;
    return PHX_ERR_NOT_IMPLEMENTED;
}

PhoenixBleStatus phoenix_ble_server_register_command_handler(PhoenixBleServerContext* context, PhoenixBleCommandHandler handler)
{
    (void) context;
    (void) handler;
    return PHX_ERR_NOT_IMPLEMENTED;
}
