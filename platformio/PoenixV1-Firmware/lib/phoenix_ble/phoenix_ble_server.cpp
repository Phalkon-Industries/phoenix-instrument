#include "phoenix_ble_server.hpp"

#include "phoenix_guard.hpp"

#include <string.h>

namespace {
static const uint16_t k_minimum_mtu_bytes = 23U;
static const uint16_t k_maximum_mtu_bytes = 247U;
static const PhoenixBleBackend* g_backend = NULL;
}

PhoenixBleStatus phoenix_ble_server_register_backend(const PhoenixBleBackend* backend)
{
    g_backend = backend;
    return PHX_OK;
}

PhoenixBleStatus phoenix_ble_server_initialize(PhoenixBleServerContext* context, const PhoenixBleConfig* config)
{
    GUARD_NONNULL(context);
    GUARD_NONNULL(config);
    GUARD_NONNULL(config->device_name);
    GUARD_NONNULL(config->service_uuid);
    GUARD_NONNULL(config->command_characteristic_uuid);
    GUARD_NONNULL(config->notification_characteristic_uuid);

    if ((config->preferred_mtu < k_minimum_mtu_bytes) || (config->preferred_mtu > k_maximum_mtu_bytes)) {
        return PHX_ERR_INVALID_ARG;
    }

    if ((config->preferred_connection_interval_min_ms == 0U) ||
        (config->preferred_connection_interval_max_ms == 0U) ||
        (config->preferred_connection_interval_min_ms > config->preferred_connection_interval_max_ms)) {
        return PHX_ERR_INVALID_ARG;
    }

    if (g_backend == NULL) {
        return PHX_ERR_NOT_INITIALIZED;
    }

    // Step 1: Reset runtime state so repeated initialisation attempts always start cleanly.
    memset(&context->characteristic_ids, 0, sizeof(context->characteristic_ids));
    context->command_handler = NULL;
    context->is_advertising = 0U;
    context->is_connected = 0U;

    // Step 2: Invoke the backend so hardware state mirrors the incoming configuration.
    GUARD(g_backend->initialize(context, config));

    // Step 3: Record the caller-supplied configuration for future operations.
    context->config = *config;

    // Step 4: Mark the facade as initialised so follow-up calls can proceed.
    context->is_initialized = 1U;

    return PHX_OK;
}

PhoenixBleStatus phoenix_ble_server_start_advertising(PhoenixBleServerContext* context)
{
    GUARD_NONNULL(context);
    GUARD_INITIALIZED(context->is_initialized);

    if ((g_backend == NULL) || (g_backend->start_advertising == NULL)) {
        return PHX_ERR_NOT_INITIALIZED;
    }

    GUARD(g_backend->start_advertising(context));

    context->is_advertising = 1U;

    return PHX_OK;
}

PhoenixBleStatus phoenix_ble_server_stop_advertising(PhoenixBleServerContext* context)
{
    GUARD_NONNULL(context);
    GUARD_INITIALIZED(context->is_initialized);

    if ((g_backend == NULL) || (g_backend->stop_advertising == NULL)) {
        return PHX_ERR_NOT_INITIALIZED;
    }

    GUARD(g_backend->stop_advertising(context));

    context->is_advertising = 0U;

    return PHX_OK;
}

PhoenixBleStatus phoenix_ble_server_is_connected(const PhoenixBleServerContext* context, uint8_t* is_connected)
{
    GUARD_NONNULL(context);
    GUARD_NONNULL(is_connected);
    GUARD_INITIALIZED(context->is_initialized);

    *is_connected = context->is_connected;

    return PHX_OK;
}

PhoenixBleStatus phoenix_ble_server_send_notification(PhoenixBleServerContext* context, const uint8_t* payload, uint16_t payload_length)
{
    GUARD_NONNULL(context);
    GUARD_NONNULL(payload);

    if (payload_length == 0U) {
        return PHX_ERR_INVALID_ARG;
    }

    GUARD_INITIALIZED(context->is_initialized);

    if (payload_length > context->config.preferred_mtu) {
        return PHX_ERR_INVALID_ARG;
    }

    if ((g_backend == NULL) || (g_backend->send_notification == NULL)) {
        return PHX_ERR_NOT_INITIALIZED;
    }

    GUARD(g_backend->send_notification(context, payload, payload_length));

    return PHX_OK;
}

PhoenixBleStatus phoenix_ble_server_register_command_handler(PhoenixBleServerContext* context, PhoenixBleCommandHandler handler)
{
    GUARD_NONNULL(context);
    GUARD_INITIALIZED(context->is_initialized);
    GUARD_NONNULL(handler);

    context->command_handler = handler;

    return PHX_OK;
}

PhoenixBleStatus phoenix_ble_server_handle_connection_event(PhoenixBleServerContext* context, uint8_t is_connected)
{
    GUARD_NONNULL(context);
    GUARD_INITIALIZED(context->is_initialized);

    context->is_connected = (is_connected != 0U) ? 1U : 0U;

    return PHX_OK;
}
