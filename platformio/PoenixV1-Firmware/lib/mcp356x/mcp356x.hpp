#ifndef MCP356X_H
#define MCP356X_H



#include <stdint.h>
#include <stdbool.h>

// ===================== Device Register Addresses (logical) =====================
// Address space 0x0..0xF (see detailed spec notes). Incremental read wraps 0xF->0x0,
// incremental write wraps 0xD->0x1.
#define MCP356X_DEVICE_ADDRESS 0x01u   // Device address bits CMD[7:6]
#define MCP356X_DEVICE_ADDRESS_MASK 0x03u

#define MCP356X_REG_ADCDATA     0x00  // 24/32-bit (read only)
#define MCP356X_REG_CONFIG0     0x01
#define MCP356X_REG_CONFIG1     0x02
#define MCP356X_REG_CONFIG2     0x03
#define MCP356X_REG_CONFIG3     0x04
#define MCP356X_REG_IRQ         0x05
#define MCP356X_REG_MUX         0x06
#define MCP356X_REG_SCAN        0x07  // 24-bit
#define MCP356X_REG_TIMER       0x08  // 24-bit
#define MCP356X_REG_OFFSETCAL   0x09  // 24-bit
#define MCP356X_REG_GAINCAL     0x0A  // 24-bit
#define MCP356X_REG_RESERVED_B  0x0B  // 24-bit (write 0x900000)
#define MCP356X_REG_RESERVED_C  0x0C  // 8-bit  (write 0x30)
#define MCP356X_REG_LOCK        0x0D  // 8-bit  (0xA5 unlock, else locked)
#define MCP356X_REG_RESERVED_E  0x0E
#define MCP356X_REG_CRCREG      0x0F  // 16-bit (read only)

// ===================== Fast Command Encodings (CMD[5:2]) =======================
// When Type bits (CMD[1:0]) = 00 (Fast Command)
#define MCP356X_FASTCMD_START      0b1010
#define MCP356X_FASTCMD_STANDBY    0b1011
#define MCP356X_FASTCMD_ADCSHUTDN  0b1100
#define MCP356X_FASTCMD_FULLSHUTDN 0b1101  // Writes CONFIG0 = 0x00
#define MCP356X_FASTCMD_FULLRESET  0b1110  // Full register reset (POR state)

// ===================== STATUS Bit Masks (returned during COMMAND) ==============
#define MCP356X_STATUS_DR_MASK     0x04  // DR_STATUS (0=new data, 1=no data)
#define MCP356X_STATUS_CRCCFG_MASK 0x02
#define MCP356X_STATUS_POR_MASK    0x01

// ===================== MUX Channel Codes (nibbles) =============================
#define MCP356X_MUX_CH0   0x0
#define MCP356X_MUX_CH1   0x1
#define MCP356X_MUX_CH2   0x2
#define MCP356X_MUX_CH3   0x3
#define MCP356X_MUX_CH4   0x4
#define MCP356X_MUX_CH5   0x5
#define MCP356X_MUX_CH6   0x6
#define MCP356X_MUX_CH7   0x7
#define MCP356X_MUX_AGND  0x8
#define MCP356X_MUX_AVDD  0x9
#define MCP356X_MUX_REFPLUS 0xB
#define MCP356X_MUX_REFMINUS 0xC
#define MCP356X_MUX_TEMP_P 0xD
#define MCP356X_MUX_TEMP_M 0xE
#define MCP356X_MUX_VCM    0xF

// ===================== Return Codes ============================================
#define MCP356X_OK                      0
#define MCP356X_ERR_INVALID_ARG        -1
#define MCP356X_ERR_SPI                -2
#define MCP356X_ERR_TIMEOUT            -3
#define MCP356X_ERR_UNSUPPORTED        -4
#define MCP356X_ERR_NOT_INITIALIZED    -5

// ===================== Minimal Public API =====================================
int mcp356x_initialize(int chip_select_pin, int data_ready_pin, uint32_t spi_clock_hz);
int mcp356x_send_fast_command(uint8_t command_code, uint8_t *status_byte);



#endif // MCP356X_H
