#include "phoenix_ble_data_packing.hpp"

#include <stdio.h>

PhoenixBleStatus phoenix_ble_pack_notification_message(const char* command,
                                                       const char* parameters_json,
                                                       char* buffer,
                                                       size_t buffer_capacity,
                                                       size_t* bytes_written)
{
    GUARD_NONNULL(command);
    GUARD_NONNULL(parameters_json);
    GUARD_NONNULL(buffer);
    GUARD_NONNULL(bytes_written);

    if (buffer_capacity == 0U) {
        return PHX_ERR_INVALID_ARG;
    }

    if (command[0] == '\0') {
        return PHX_ERR_INVALID_ARG;
    }

    const int formatted_length = snprintf(buffer, buffer_capacity,
                                          "{\"command\":\"%s\",\"parameters\":%s}\n",
                                          command, parameters_json);

    if (formatted_length < 0) {
        return PHX_ERR_COMMUNICATION;
    }

    if (static_cast<size_t>(formatted_length) >= buffer_capacity) {
        buffer[0] = '\0';
        return PHX_ERR_INVALID_ARG;
    }

    *bytes_written = static_cast<size_t>(formatted_length);

    return PHX_OK;
}
