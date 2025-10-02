#ifndef MCP356X_HPP
#define MCP356X_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ===================== Device Register Addresses (logical) =====================
// Address space 0x0..0xF (see detailed spec notes). Incremental read wraps 0xF->0x0,
// incremental write wraps 0xD->0x1.
#define MCP356X_DEVICE_ADDRESS 0x01u  // Device address bits CMD[7:6]
#define MCP356X_DEVICE_ADDRESS_MASK 0x03u

#define MCP356X_REG_ADCDATA 0x00  // 24/32-bit (read only)
#define MCP356X_REG_CONFIG0 0x01
#define MCP356X_REG_CONFIG1 0x02
#define MCP356X_REG_CONFIG2 0x03
#define MCP356X_REG_CONFIG3 0x04
#define MCP356X_REG_IRQ 0x05
#define MCP356X_REG_MUX 0x06
#define MCP356X_REG_SCAN 0x07        // 24-bit
#define MCP356X_REG_TIMER 0x08       // 24-bit
#define MCP356X_REG_OFFSETCAL 0x09   // 24-bit
#define MCP356X_REG_GAINCAL 0x0A     // 24-bit
#define MCP356X_REG_RESERVED_B 0x0B  // 24-bit (write 0x900000)
#define MCP356X_REG_RESERVED_C 0x0C  // 8-bit  (write 0x30)
#define MCP356X_REG_LOCK 0x0D        // 8-bit  (0xA5 unlock, else locked)
#define MCP356X_REG_RESERVED_E 0x0E
#define MCP356X_REG_CRCREG 0x0F  // 16-bit (read only)

// ===================== Fast Command Encodings (CMD[5:2]) =======================
// When Type bits (CMD[1:0]) = 00 (Fast Command)
#define MCP356X_FASTCMD_START 0b1010
#define MCP356X_FASTCMD_STANDBY 0b1011
#define MCP356X_FASTCMD_ADCSHUTDN 0b1100
#define MCP356X_FASTCMD_FULLSHUTDN 0b1101  // Writes CONFIG0 = 0x00
#define MCP356X_FASTCMD_FULLRESET 0b1110   // Full register reset (POR state)

// ===================== STATUS Bit Masks (returned during COMMAND) ==============
#define MCP356X_STATUS_DR_MASK 0x04  // DR_STATUS (0=new data, 1=no data)
#define MCP356X_STATUS_CRCCFG_MASK 0x02
#define MCP356X_STATUS_POR_MASK 0x01

// ===================== MUX Channel Codes (nibbles) =============================
#define MCP356X_MUX_CH0 0x0
#define MCP356X_MUX_CH1 0x1
#define MCP356X_MUX_CH2 0x2
#define MCP356X_MUX_CH3 0x3
#define MCP356X_MUX_CH4 0x4
#define MCP356X_MUX_CH5 0x5
#define MCP356X_MUX_CH6 0x6
#define MCP356X_MUX_CH7 0x7
#define MCP356X_MUX_AGND 0x8
#define MCP356X_MUX_AVDD 0x9
#define MCP356X_MUX_REFPLUS 0xB
#define MCP356X_MUX_REFMINUS 0xC
#define MCP356X_MUX_TEMP_P 0xD
#define MCP356X_MUX_TEMP_M 0xE
#define MCP356X_MUX_VCM 0xF

// ===================== Return Codes ============================================
#define MCP356X_OK 0
#define MCP356X_ERR_INVALID_ARG -1
#define MCP356X_ERR_SPI -2
#define MCP356X_ERR_TIMEOUT -3
#define MCP356X_ERR_UNSUPPORTED -4
#define MCP356X_ERR_NOT_INITIALIZED -5

// ===================== Minimal Public API =====================================
// These helpers intentionally mirror the datasheet command encodings and expose
// only the functionality we actively exercise in tests. Each call assumes the
// device address bits are 0b01 (matching the Phoenix hardware configuration).

/**
 * @brief Initialise driver state and configure the SPI bus pins.
 *
 * Sets the chip-select pin to OUTPUT/high and caches the SPISettings instance
 * used for all subsequent transactions. The function is idempotent and may be
 * called multiple times.
 *
 * @param chip_select_pin GPIO used for MCP356x CS/SS (active low).
 * @param spi_clock_hz    SPI clock frequency to request via SPISettings.
 * @return MCP356X_OK on success or a negative error code on invalid arguments.
 */
int mcp356x_initialize(int chip_select_pin, uint32_t spi_clock_hz);

/**
 * @brief Issue one of the MCP356x "Fast Command" opcodes.
 *
 * Fast commands share a single byte where CMD[5:2] identifies the action and
 * CMD[1:0] = 0b00 selects the fast-command mode. The STATUS byte returned by
 * the device during the transfer is copied into @p status_byte.
 *
 * @param command_code 4-bit fast-command code (see MCP356X_FASTCMD_* constants).
 * @param status_byte  Pointer that receives the STATUS response (must not be NULL).
 * @return MCP356X_OK when the transfer succeeded, else a negative error code.
 */
int mcp356x_send_fast_command(uint8_t command_code, uint8_t* status_byte);

/**
 * @brief Read one or more bytes from a static register.
 *
 * Issues a static read command (CMD[1:0] = 0b01) followed by @p length bytes of
 * dummy writes to clock data out of the device. Useful for 8/24/32-bit register
 * reads; passes back the STATUS byte if @p status_byte is non-null.
 *
 * @param register_address Logical register index (0x0..0xF only).
 * @param buffer           Caller-provided output buffer.
 * @param length           Number of bytes to read (1..4).
 * @param status_byte      Optional pointer to receive STATUS (may be NULL).
 * @return MCP356X_OK on success or a negative error code.
 */
int mcp356x_read_register(uint8_t register_address, uint8_t* buffer, size_t length, uint8_t* status_byte);

/**
 * @brief Write one or more bytes to a static register.
 *
 * Issues a static write command (CMD[1:0] = 0b10) and clocks @p length bytes
 * into the part. STATUS is optionally returned via @p status_byte so higher
 * layers can inspect DR/POR flags after register updates.
 *
 * @param register_address Logical register index (0x0..0xF only).
 * @param buffer           Pointer to the data to write.
 * @param length           Number of bytes to write (1..4).
 * @param status_byte      Optional pointer to receive STATUS (may be NULL).
 * @return MCP356X_OK on success or a negative error code.
 */
int mcp356x_write_register(uint8_t register_address, const uint8_t* buffer, size_t length, uint8_t* status_byte);

// ===================== Default Config Preset ==================================
#define MCP356X_CONFIG0_DEFAULT 0b10110011  // Internal REF, continuous conversion, standby disabled
#define MCP356X_CONFIG1_DEFAULT 0b00001100  // OSR=4096, boost off (high-resolution mode)
#define MCP356X_CONFIG2_DEFAULT 0b10001011  // BOOST=10b, gain=1 (001b), AZ_MUX/AZ_REF enabled, LSB must stay 1
#define MCP356X_CONFIG3_DEFAULT 0b00000000  // No auto-sequence, default conversion delay

// Gain control resides in CONFIG2.GAIN[2:0]. Provide a typed enum so callers cannot
// pass arbitrary bit patterns. The hardware exposes a 1/3x mode alongside the usual
// 1x..64x ladder.
enum class mcp356x_gain : uint8_t {
  gain_div3 = 0b000,  // 1/3× (digital attenuation)
  gain_x1   = 0b001,  // 1× (POR default)
  gain_x2   = 0b010,
  gain_x4   = 0b011,
  gain_x8   = 0b100,
  gain_x16  = 0b101,
  gain_x32  = 0b110,
  gain_x64  = 0b111,
};

/**
 * @brief Update CONFIG2.GAIN[2:0] while keeping other CONFIG2 fields intact.
 *
 * The helper performs a read-modify-write on CONFIG2, ensuring BOOST[1:0],
 * AZ_MUX, AZ_REF, and the mandatory bit0=1 remain untouched. The device must be
 * initialised before use.
 *
 * @param gain           Desired hardware gain (1/3× through 64×).
 * @return MCP356X_OK on success, or a negative error propagated from register
 *         read/write helpers.
 */
int mcp356x_set_gain(mcp356x_gain gain);

/**
 * @brief Read CONFIG2.GAIN[2:0] and convert it to the strongly typed enum.
 *
 * @param out_gain Pointer receiving the decoded gain enum.
 * @return MCP356X_OK on success, MCP356X_ERR_INVALID_ARG when @p out_gain is
 *         NULL, or a propagated error from the register read helper.
 */
int mcp356x_get_gain(mcp356x_gain* out_gain);

/**
 * @brief Configure the ADC MUX for a single-ended channel relative to AGND.
 *
 * Helps application code quickly select a channel without exposing raw register
 * encoding details. Only channels 0-7 are valid; the helper leaves hardware
 * untouched if an invalid channel index is provided.
 *
 * @param channel_index Logical single-ended channel (0-7 inclusive).
 * @return MCP356X_OK if the register write succeeded, otherwise a negative error code.
 */
int mcp356x_select_single_ended_channel(uint8_t channel_index);

/**
 * @brief Write a curated default configuration into CONFIG0-3 registers.
 *
 * Centralises the "golden" register image used during application bring-up so
 * firmware and tests share a consistent baseline. This helper preserves the
 * legacy behaviour of programming CONFIG2 with gain=1×.
 *
 * @return MCP356X_OK if all four writes succeed, otherwise propagates the first error encountered.
 */
int mcp356x_apply_default_config(void);

/**
 * @brief Apply the default configuration while overriding CONFIG2.GAIN[2:0].
 *
 * Allows firmware to bootstrap the ADC into the standard Phoenix baseline but
 * with a caller-specified PGA gain. BOOST, AZ_MUX, AZ_REF, and the reserved LSB
 * remain fixed to their datasheet defaults.
 *
 * @param gain Desired hardware gain (1/3× through 64×).
 * @return MCP356X_OK if all four writes succeed, otherwise propagates the first error encountered.
 */
int mcp356x_apply_default_config_with_gain(mcp356x_gain gain);

/**
 * @brief Test-only hook to clear the driver's initialisation flag.
 *
 * Exposed under UNIT_TEST so negative-path Unity tests can verify public APIs
 * reject calls issued before `mcp356x_initialize()`.
 */
void mcp356x_force_uninitialized_for_test(void);

/**
 * @brief Issue the FASTCMD_START opcode to begin continuous conversions.
 *
 * @param status_byte Optional pointer that receives the STATUS response. May
 *                    be NULL when callers are uninterested in the value.
 * @return MCP356X_OK on success or a propagated error from
 *         mcp356x_send_fast_command.
 */
int mcp356x_start_conversion(uint8_t* status_byte);

/**
 * @brief Place the ADC into standby mode using the FASTCMD_STANDBY opcode.
 *
 * Convenience wrapper that keeps application code away from raw fast-command
 * constants while still exposing the returned STATUS byte when desired. The
 * helper accepts a null pointer, in which case the STATUS value is discarded.
 *
 * @param status_byte Optional pointer that receives the STATUS response. May
 *                    be NULL when callers are uninterested in the value.
 * @return MCP356X_OK on success or a negative error propagated from
 *         mcp356x_send_fast_command.
 */
int mcp356x_enter_standby(uint8_t* status_byte);

/**
 * @brief Assert the FASTCMD_ADCSHUTDN opcode to stop conversions and power down the ADC core.
 *
 * @param status_byte Optional pointer that receives the STATUS response. May
 *                    be NULL when callers are uninterested in the value.
 * @return MCP356X_OK on success or a propagated error from the fast-command helper.
 */
int mcp356x_enter_adc_shutdown(uint8_t* status_byte);

/**
 * @brief Assert the FASTCMD_FULLSHUTDN opcode to place the device into full shutdown.
 *
 * @param status_byte Optional pointer that receives the STATUS response. May
 *                    be NULL when callers are uninterested in the value.
 * @return MCP356X_OK on success or a propagated error from the fast-command helper.
 */
int mcp356x_enter_full_shutdown(uint8_t* status_byte);

/**
 * @brief Issue FASTCMD_FULLRESET to restore power-on default register values.
 *
 * @param status_byte Optional pointer that receives the STATUS response. May
 *                    be NULL when callers are uninterested in the value.
 * @return MCP356X_OK on success or a propagated error from the fast-command helper.
 */
int mcp356x_full_reset(uint8_t* status_byte);

/**
 * @brief Select a single-ended channel and read a conversion with timeout protection.
 *
 * Wrapper performing: reset DRDY state, select the MUX, trigger a conversion,
 * poll ADCDATA until DR_STATUS indicates fresh data, sign-extend the 24-bit
 * result, and optionally return MCP356X_ERR_TIMEOUT if @p timeout_ms elapses.
 *
 * @param channel_index Logical channel (0-7) to sample single-ended.
 * @param timeout_ms    Maximum time in milliseconds to wait for DRDY (0 => immediate timeout).
 * @param result        Pointer receiving the signed 24-bit conversion result.
 * @return MCP356X_OK on success, MCP356X_ERR_TIMEOUT on timeout, or a negative error from underlying calls.
 */
int mcp356x_read_single_ended_channel(uint8_t channel_index, uint32_t timeout_ms, int32_t* result);
#endif  // MCP356X_HPP
