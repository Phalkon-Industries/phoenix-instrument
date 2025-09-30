#include "mcp356x.hpp"

#include <Arduino.h>
#include <SPI.h>

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
