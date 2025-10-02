#include "mcp356x.hpp"

#include <Arduino.h>
#include <SPI.h>
#include <limits.h>

// Forward declare delay helper so we can abstract for tests if needed in future.
static inline void mcp356x_delay_ms(uint32_t milliseconds)
{
    delay(milliseconds);
}

// ------------------------------ Driver state ---------------------------------
// These globals track the runtime configuration selected via mcp356x_initialize.
static int g_chip_select_pin = -1;
static bool g_initialized = false;
static SPISettings g_spi_settings(1000000UL, MSBFIRST, SPI_MODE0);

// Datasheet helper: verify we stay inside the 0x0..0xF logical register window.
static inline bool mcp356x_is_valid_register(uint8_t reg)
{
    return reg <= MCP356X_REG_CRCREG;
}

// Assemble the 8-bit command header:
//   [7:6] = device address, [5:2] = register or fast command code, [1:0] = type
// where type corresponds to the "static" command encoding from Table 6-3.
static inline uint8_t mcp356x_command_byte(uint8_t register_or_command, uint8_t command_type)
{
    return (uint8_t)(((MCP356X_DEVICE_ADDRESS & MCP356X_DEVICE_ADDRESS_MASK) << 6) |
                     ((register_or_command & 0x0Fu) << 2) |
                     (command_type & 0x03u));
}

int mcp356x_initialize(int chip_select_pin, uint32_t spi_clock_hz)
{
    // Guardrail checks: CS must be valid, SPI clock must be non-zero.
    if (chip_select_pin < 0 || spi_clock_hz == 0) {
        return MCP356X_ERR_INVALID_ARG;
    }

    g_chip_select_pin = chip_select_pin;
    g_spi_settings = SPISettings(spi_clock_hz, MSBFIRST, SPI_MODE0);

    pinMode(g_chip_select_pin, OUTPUT);
    digitalWrite(g_chip_select_pin, HIGH);

    SPI.begin();
    g_initialized = true;
    return MCP356X_OK;
}

int mcp356x_send_fast_command(uint8_t command_code, uint8_t *status_byte)
{
    // Fast commands always require the driver to be initialised and STATUS storage.
    if (!g_initialized) {
        return MCP356X_ERR_NOT_INITIALIZED;
    }
    if (status_byte == NULL) {
        return MCP356X_ERR_INVALID_ARG;
    }

    // Build command byte: [7:6]=device address, [5:2]=command, [1:0]=type (00 for fast command)
    uint8_t command = (uint8_t)(((MCP356X_DEVICE_ADDRESS & MCP356X_DEVICE_ADDRESS_MASK) << 6) |
                                ((command_code & 0x0Fu) << 2));

    SPI.beginTransaction(g_spi_settings);
    digitalWrite(g_chip_select_pin, LOW);
    uint8_t status = SPI.transfer(command);
    digitalWrite(g_chip_select_pin, HIGH);
    SPI.endTransaction();

    *status_byte = status;
    return MCP356X_OK;
}

int mcp356x_read_register(uint8_t register_address, uint8_t *buffer, size_t length, uint8_t *status_byte)
{
    // Static read operation (single header followed by 1-4 data bytes clocked out).
    // Validate runtime state and caller parameters before touching the bus.
    if (!g_initialized) {
        return MCP356X_ERR_NOT_INITIALIZED;
    }
    if (buffer == NULL || length == 0 || length > 4) {
        return MCP356X_ERR_INVALID_ARG;
    }
    if (!mcp356x_is_valid_register(register_address)) {
        return MCP356X_ERR_INVALID_ARG;
    }

    uint8_t command = mcp356x_command_byte(register_address, 0x01u);  // Static read

    SPI.beginTransaction(g_spi_settings);
    digitalWrite(g_chip_select_pin, LOW);
    // The STATUS byte is returned alongside the first data byte; capture it.
    uint8_t status = SPI.transfer(command);
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = SPI.transfer(0x00);
    }
    digitalWrite(g_chip_select_pin, HIGH);
    SPI.endTransaction();

    if (status_byte != NULL) {
        *status_byte = status;
    }
    return MCP356X_OK;
}

int mcp356x_write_register(uint8_t register_address, const uint8_t *buffer, size_t length, uint8_t *status_byte)
{
    // Static write operation (single header followed by 1-4 data bytes clocked in).
    // Validate runtime state and caller parameters before touching the bus.
    if (!g_initialized) {
        return MCP356X_ERR_NOT_INITIALIZED;
    }
    if (buffer == NULL || length == 0 || length > 4) {
        return MCP356X_ERR_INVALID_ARG;
    }
    if (!mcp356x_is_valid_register(register_address)) {
        return MCP356X_ERR_INVALID_ARG;
    }

    uint8_t command = mcp356x_command_byte(register_address, 0x02u);  // Static write

    SPI.beginTransaction(g_spi_settings);
    digitalWrite(g_chip_select_pin, LOW);
    // STATUS is sampled during the header transfer and optionally returned.
    uint8_t status = SPI.transfer(command);
    for (size_t i = 0; i < length; ++i) {
        (void)SPI.transfer(buffer[i]);
    }
    digitalWrite(g_chip_select_pin, HIGH);
    SPI.endTransaction();

    if (status_byte != NULL) {
        *status_byte = status;
    }
    return MCP356X_OK;
}

int mcp356x_select_single_ended_channel(uint8_t channel_index)
{
    // Only channels 0-7 map to the single-ended inputs; reject anything outside that range.
    if (channel_index > MCP356X_MUX_CH7) {
        return MCP356X_ERR_INVALID_ARG;
    }

    uint8_t mux_value = (uint8_t)((channel_index << 4) | MCP356X_MUX_AGND);
    return mcp356x_write_register(MCP356X_REG_MUX, &mux_value, 1u, NULL);
}

int mcp356x_apply_default_config(void)
{
    if (!g_initialized) {
        return MCP356X_ERR_NOT_INITIALIZED;
    }

    const uint8_t config_defaults[] = {
        MCP356X_CONFIG0_DEFAULT,
        MCP356X_CONFIG1_DEFAULT,
        MCP356X_CONFIG2_DEFAULT,
        MCP356X_CONFIG3_DEFAULT,
    };

    int rc = mcp356x_write_register(MCP356X_REG_CONFIG0, &config_defaults[0], 1u, NULL);
    if (rc != MCP356X_OK) {
        return rc;
    }
    rc = mcp356x_write_register(MCP356X_REG_CONFIG1, &config_defaults[1], 1u, NULL);
    if (rc != MCP356X_OK) {
        return rc;
    }
    rc = mcp356x_write_register(MCP356X_REG_CONFIG2, &config_defaults[2], 1u, NULL);
    if (rc != MCP356X_OK) {
        return rc;
    }
    return mcp356x_write_register(MCP356X_REG_CONFIG3, &config_defaults[3], 1u, NULL);
}

int mcp356x_read_single_ended_channel(uint8_t channel_index, uint32_t timeout_ms, int32_t *result)
{
    if (result == NULL) {
        return MCP356X_ERR_INVALID_ARG;
    }
    if (!g_initialized) {
        return MCP356X_ERR_NOT_INITIALIZED;
    }

    int rc = mcp356x_select_single_ended_channel(channel_index);
    if (rc != MCP356X_OK) {
        return rc;
    }

    uint8_t status = 0xFFu;
    rc = mcp356x_send_fast_command(MCP356X_FASTCMD_START, &status);
    if (rc != MCP356X_OK) {
        return rc;
    }

    uint8_t adc_bytes[3] = {0};
    uint32_t elapsed_ms = 0u;
    bool data_ready = false;

    while (!data_ready) {
        uint8_t read_status = 0xFFu;
        rc = mcp356x_read_register(MCP356X_REG_ADCDATA, adc_bytes, sizeof adc_bytes, &read_status);
        if (rc != MCP356X_OK) {
            return rc;
        }

        data_ready = ((read_status & MCP356X_STATUS_DR_MASK) == 0u);
        if (data_ready) {
            break;
        }

        if (elapsed_ms >= timeout_ms) {
            return MCP356X_ERR_TIMEOUT;
        }

        mcp356x_delay_ms(1u);
        ++elapsed_ms;
    }

    int32_t raw_value = (int32_t)((adc_bytes[0] << 16) | (adc_bytes[1] << 8) | adc_bytes[2]);
    if (raw_value & 0x800000) {
        raw_value |= 0xFF000000;
    }

    *result = raw_value;
    return MCP356X_OK;
}