#include "mcp356x.hpp"

#include <Arduino.h>
#include <SPI.h>

static int g_chip_select_pin = -1;
static int g_data_ready_pin = -1;
static bool g_initialized = false;
static SPISettings g_spi_settings(1000000UL, MSBFIRST, SPI_MODE0);

int mcp356x_initialize(int chip_select_pin, int data_ready_pin, uint32_t spi_clock_hz)
{
    if (chip_select_pin < 0 || spi_clock_hz == 0) {
        return MCP356X_ERR_INVALID_ARG;
    }

    g_chip_select_pin = chip_select_pin;
    g_data_ready_pin = data_ready_pin;
    g_spi_settings = SPISettings(spi_clock_hz, MSBFIRST, SPI_MODE0);

    pinMode(g_chip_select_pin, OUTPUT);
    digitalWrite(g_chip_select_pin, HIGH);

    if (g_data_ready_pin >= 0) {
        pinMode(g_data_ready_pin, INPUT);
    }

    SPI.begin();
    g_initialized = true;
    return MCP356X_OK;
}

int mcp356x_send_fast_command(uint8_t command_code, uint8_t *status_byte)
{
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
