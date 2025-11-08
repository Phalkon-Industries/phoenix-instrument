#include "phoenix_ble_bluefruit_backend.hpp"

#include <bluefruit.h>
#include <string.h>

namespace {
static BLEService               g_primary_service;
static BLECharacteristic        g_command_characteristic;
static BLECharacteristic        g_notification_characteristic;
static PhoenixBleServerContext* g_active_context                      = NULL;
static bool                     g_softdevice_started                  = false;
static bool                     g_service_started                     = false;
static bool                     g_command_characteristic_started      = false;
static bool                     g_notification_characteristic_started = false;
static uint16_t                 g_configured_mtu                      = 0U;
static char                     g_service_uuid_string[37]             = "";
static char                     g_command_uuid_string[37]             = "";
static char                     g_notification_uuid_string[37]        = "";

static void bluefruit_connect_callback(uint16_t conn_handle) {
  (void) conn_handle;

  if (g_active_context == NULL) {
    return;
  }

  const PhoenixBleStatus return_code = phoenix_ble_server_handle_connection_event(g_active_context, 1U);
  (void) return_code;
}

static void bluefruit_disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;

  if (g_active_context == NULL) {
    return;
  }

  const PhoenixBleStatus return_code = phoenix_ble_server_handle_connection_event(g_active_context, 0U);
  (void) return_code;
}

static void bluefruit_command_write_callback(uint16_t conn_handle, BLECharacteristic* characteristic, uint8_t* data,
                                             uint16_t length) {
  (void) conn_handle;
  (void) characteristic;

  if ((g_active_context == NULL) || (g_active_context->command_handler == NULL)) {
    return;
  }

  const PhoenixBleStatus return_code = g_active_context->command_handler(data, length);
  (void) return_code;
}

static PhoenixBleStatus configure_advertising_payload(void) {
  // Step 1: Reset the advertising buffers so the Phoenix service metadata stays accurate.
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();

  // Step 2: Populate the advertising payload with the Phoenix service and device identity.
  if (!Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE)) {
    return PHX_ERR_COMMUNICATION;
  }

  if (!Bluefruit.Advertising.addTxPower()) {
    return PHX_ERR_COMMUNICATION;
  }

  if (!Bluefruit.Advertising.addService(g_primary_service)) {
    return PHX_ERR_COMMUNICATION;
  }

  if (!Bluefruit.ScanResponse.addName()) {
    return PHX_ERR_COMMUNICATION;
  }

  Bluefruit.Advertising.setIntervalMS(32U, 244U);
  Bluefruit.Advertising.setFastTimeout(30U);
  Bluefruit.Advertising.restartOnDisconnect(true);

  return PHX_OK;
}

static PhoenixBleStatus bluefruit_initialize(PhoenixBleServerContext* context, const PhoenixBleConfig* config) {
  GUARD_NONNULL(context);
  GUARD_NONNULL(config);

  g_active_context = context;

  // Step 1: Start the SoftDevice stack and enable maximum bandwidth when first invoked.
  if (!g_softdevice_started) {
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

    if (!Bluefruit.begin()) {
      return PHX_ERR_HARDWARE_FAILURE;
    }

    Bluefruit.autoConnLed(true);
    g_softdevice_started = true;
  }

  // Step 2: Apply GAP configuration for device identity and connection parameters.
  Bluefruit.setName(config->device_name);

  if (!Bluefruit.Periph.setConnIntervalMS(config->preferred_connection_interval_min_ms,
                                          config->preferred_connection_interval_max_ms)) {
    return PHX_ERR_COMMUNICATION;
  }

  if ((g_configured_mtu != 0U) && (g_configured_mtu != config->preferred_mtu)) {
    return PHX_ERR_INVALID_ARG;
  }

  // Step 3: Provision the Phoenix primary service on the SoftDevice.
  BLEUuid service_uuid(config->service_uuid);

  if (!g_service_started) {
    g_primary_service.setUuid(service_uuid);

    if (g_primary_service.begin() != ERROR_NONE) {
      return PHX_ERR_HARDWARE_FAILURE;
    }

    g_service_started = true;
    strncpy(g_service_uuid_string, config->service_uuid, sizeof(g_service_uuid_string) - 1U);
    g_service_uuid_string[sizeof(g_service_uuid_string) - 1U] = '\0';
  }
  else if (strncmp(g_service_uuid_string, config->service_uuid, sizeof(g_service_uuid_string)) != 0) {
    return PHX_ERR_INVALID_ARG;
  }

  // Step 4: Configure the command characteristic so the phone can submit requests.
  BLEUuid command_uuid(config->command_characteristic_uuid);

  if (!g_command_characteristic_started) {
    g_command_characteristic.setUuid(command_uuid);
    g_command_characteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    g_command_characteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    g_command_characteristic.setMaxLen(config->preferred_mtu);
    g_command_characteristic.setWriteCallback(bluefruit_command_write_callback);

    if (g_command_characteristic.begin() != ERROR_NONE) {
      return PHX_ERR_HARDWARE_FAILURE;
    }

    g_command_characteristic_started = true;
    strncpy(g_command_uuid_string, config->command_characteristic_uuid, sizeof(g_command_uuid_string) - 1U);
    g_command_uuid_string[sizeof(g_command_uuid_string) - 1U] = '\0';
  }
  else if (strncmp(g_command_uuid_string, config->command_characteristic_uuid, sizeof(g_command_uuid_string)) != 0) {
    return PHX_ERR_INVALID_ARG;
  }

  // Step 5: Configure the notification characteristic for outbound telemetry.
  BLEUuid notification_uuid(config->notification_characteristic_uuid);

  if (!g_notification_characteristic_started) {
    g_notification_characteristic.setUuid(notification_uuid);
    g_notification_characteristic.setProperties(CHR_PROPS_NOTIFY);
    g_notification_characteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    g_notification_characteristic.setMaxLen(config->preferred_mtu);

    if (g_notification_characteristic.begin() != ERROR_NONE) {
      return PHX_ERR_HARDWARE_FAILURE;
    }

    g_notification_characteristic_started = true;
    strncpy(g_notification_uuid_string, config->notification_characteristic_uuid,
            sizeof(g_notification_uuid_string) - 1U);
    g_notification_uuid_string[sizeof(g_notification_uuid_string) - 1U] = '\0';
  }
  else if (strncmp(g_notification_uuid_string, config->notification_characteristic_uuid,
                   sizeof(g_notification_uuid_string)) != 0) {
    return PHX_ERR_INVALID_ARG;
  }

  // Step 6: Record the SoftDevice handles so higher layers can reference them.
  const ble_gatts_char_handles_t command_handles             = g_command_characteristic.handles();
  const ble_gatts_char_handles_t notification_handles        = g_notification_characteristic.handles();
  context->characteristic_ids.command_characteristic_id      = command_handles.value_handle;
  context->characteristic_ids.notification_characteristic_id = notification_handles.value_handle;

  // Step 7: Keep the facade's connection state aligned with the Bluefruit callbacks.
  Bluefruit.Periph.setConnectCallback(bluefruit_connect_callback);
  Bluefruit.Periph.setDisconnectCallback(bluefruit_disconnect_callback);

  g_configured_mtu = config->preferred_mtu;

  return PHX_OK;
}

static PhoenixBleStatus bluefruit_start_advertising(PhoenixBleServerContext* context) {
  GUARD_NONNULL(context);

  // Step 1: Refresh the advertising payload so the phone sees the latest Phoenix metadata.
  PhoenixBleStatus return_code = configure_advertising_payload();

  if (return_code != PHX_OK) {
    return return_code;
  }

  // Step 2: Begin advertising indefinitely until a client connects.
  if (!Bluefruit.Advertising.start(0U)) {
    return PHX_ERR_COMMUNICATION;
  }

  return PHX_OK;
}

static PhoenixBleStatus bluefruit_stop_advertising(PhoenixBleServerContext* context) {
  GUARD_NONNULL(context);

  // Step 1: Halt the advertiser so the facade's state machine stays in sync.
  if (!Bluefruit.Advertising.stop()) {
    return PHX_ERR_COMMUNICATION;
  }

  return PHX_OK;
}

static PhoenixBleStatus bluefruit_send_notification(PhoenixBleServerContext* context, const uint8_t* payload,
                                                    uint16_t payload_length) {
  GUARD_NONNULL(context);
  GUARD_NONNULL(payload);

  if ((context->is_connected == 0U) || (Bluefruit.connected() == 0U)) {
    return PHX_ERR_COMMUNICATION;
  }

  // Step 1: Forward the payload to all subscribed centrals.
  if (!g_notification_characteristic.notify(payload, payload_length)) {
    return PHX_ERR_COMMUNICATION;
  }

  return PHX_OK;
}

static const PhoenixBleBackend k_bluefruit_backend = {
    bluefruit_initialize,
    bluefruit_start_advertising,
    bluefruit_stop_advertising,
    bluefruit_send_notification,
};
}  // namespace

PhoenixBleStatus phoenix_ble_bluefruit_install_backend(void) {
  return phoenix_ble_server_register_backend(&k_bluefruit_backend);
}
