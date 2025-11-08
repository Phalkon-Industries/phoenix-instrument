#include "phoenix_ble_stack.hpp"

#include "phoenix_ble_bluefruit_backend.hpp"
#include <string.h>

namespace {
static const char     k_device_name[]                      = "Phoenix Mock";
static const char     k_service_uuid[]                     = "b5c5d4f4-7207-4e8d-9d6b-60b98e65ce09";
static const char     k_command_characteristic_uuid[]      = "c1883ec3-d984-4dcd-9d67-41ade54c5f2a";
static const char     k_notification_characteristic_uuid[] = "cd1411cc-4ab8-46e8-9ad5-c41a871caf41";
static const uint16_t k_preferred_mtu                      = 247U;
static const uint16_t k_connection_interval_min_ms         = 20U;
static const uint16_t k_connection_interval_max_ms         = 40U;
}  // namespace

PhoenixBleStatus phoenix_ble_stack_install_default_backend(void) {
  return phoenix_ble_bluefruit_install_backend();
}

PhoenixBleConfig phoenix_ble_stack_make_default_config(void) {
  PhoenixBleConfig config                     = {};
  config.device_name                          = k_device_name;
  config.service_uuid                         = k_service_uuid;
  config.command_characteristic_uuid          = k_command_characteristic_uuid;
  config.notification_characteristic_uuid     = k_notification_characteristic_uuid;
  config.preferred_mtu                        = k_preferred_mtu;
  config.preferred_connection_interval_min_ms = k_connection_interval_min_ms;
  config.preferred_connection_interval_max_ms = k_connection_interval_max_ms;
  return config;
}

PhoenixBleStatus phoenix_ble_stack_initialize(PhoenixBleServerContext* context) {
  GUARD_NONNULL(context);

  GUARD(phoenix_ble_stack_install_default_backend());

  PhoenixBleConfig config = phoenix_ble_stack_make_default_config();

  return phoenix_ble_server_initialize(context, &config);
}

PhoenixBleStatus phoenix_ble_stack_start_advertising(PhoenixBleServerContext* context) {
  return phoenix_ble_server_start_advertising(context);
}

PhoenixBleStatus phoenix_ble_stack_stop_advertising(PhoenixBleServerContext* context) {
  return phoenix_ble_server_stop_advertising(context);
}
