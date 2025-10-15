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

// ===================== Power-On Reset Register Images ==================================
#define MCP356X_CONFIG0_POR 0b11000000  // Datasheet POR CONFIG0 value
#define MCP356X_CONFIG1_POR 0b00001100  // Datasheet POR CONFIG1 value
#define MCP356X_CONFIG2_POR 0b10001011  // Datasheet POR CONFIG2 value
#define MCP356X_CONFIG3_POR 0b00000000  // Datasheet POR CONFIG3 value

// ===================== Library Default Config Preset ==================================
#define MCP356X_CONFIG0_DEFAULT 0b10110011  // Phoenix baseline CONFIG0 (internal reference, standby disabled)
#define MCP356X_CONFIG1_DEFAULT 0b00011100  // Phoenix baseline CONFIG1 (OSR 4096, AMCLK=MCLK)
#define MCP356X_CONFIG2_DEFAULT 0b10001011  // Phoenix baseline CONFIG2 (BOOST=10b, gain=1, auto-zero enabled)
#define MCP356X_CONFIG3_DEFAULT 0b00000000  // Phoenix baseline CONFIG3 (no auto-sequence, default delay)

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

// Oversampling ratio encodings map directly to CONFIG1.OSR[3:0]. Values follow
// datasheet Table 8-3 ordering so callers cannot accidentally write reserved
// bit patterns into CONFIG1.
enum class mcp356x_osr : uint8_t {
  osr_32    = 0b0000,
  osr_64    = 0b0001,
  osr_128   = 0b0010,
  osr_256   = 0b0011,  // (POR default))
  osr_512   = 0b0100,
  osr_1024  = 0b0101,
  osr_2048  = 0b0110,
  osr_4096  = 0b0111,
  osr_8192  = 0b1000,
  osr_16384 = 0b1001,
  osr_20480 = 0b1010,
  osr_24576 = 0b1011,
  osr_40960 = 0b1100,
  osr_49152 = 0b1101,
  osr_81920 = 0b1110,
  osr_98304 = 0b1111,
};

// Prescaler encodings map to CONFIG1.PRE[1:0] and control the analog master clock.
enum class mcp356x_prescaler : uint8_t {
  mclk_div1 = 0b00,
  mclk_div2 = 0b01,
  mclk_div4 = 0b10,
  mclk_div8 = 0b11,
};

// Conversion mode encodings map to CONFIG3.CONV_MODE[1:0]. 0b11 is reserved per
// the datasheet, so callers must never request it.
enum class mcp356x_conversion_mode : uint8_t {
  continuous       = 0b00,
  oneshot_standby  = 0b01,
  oneshot_shutdown = 0b10,
};

// Data format encodings map to CONFIG3.DATA_FORMAT[1:0] and control how the ADC
// presents conversion results on the SPI bus.
enum class mcp356x_data_format : uint8_t {
  data24             = 0b00,
  data32_left        = 0b01,
  data32_signed      = 0b10,
  data32_signed_chid = 0b11,
};

// Aggregated configuration payload used by the unified initialisation helper.
struct mcp356x_settings {
  mcp356x_gain            gain;
  mcp356x_osr             osr;
  mcp356x_prescaler       prescaler;
  mcp356x_conversion_mode conversion_mode;
  mcp356x_data_format     data_format;
};

/**
 * @brief Update CONFIG2.GAIN[2:0] while keeping other CONFIG2 fields intact.
 *
 * The helper performs a read-modify-write on CONFIG2, ensuring BOOST[1:0],
 * AZ_MUX, AZ_REF, and the mandatory bit0=1 remain untouched. The device must be
 * initialised before use.
 *
 * @param gain Desired hardware gain (1/3× through 64×).
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
 * @brief Toggle CONFIG2.AZ_MUX while preserving BOOST/GAIN fields.
 */
int mcp356x_set_auto_zero_mux(bool enable);

/**
 * @brief Read CONFIG2.AZ_MUX and report whether input auto-zero is enabled.
 */
int mcp356x_get_auto_zero_mux(bool* out_enable);

/**
 * @brief Toggle CONFIG2.AZ_REF while preserving BOOST/GAIN fields.
 */
int mcp356x_set_auto_zero_reference(bool enable);

/**
 * @brief Read CONFIG2.AZ_REF and report the reference auto-zero state.
 */
int mcp356x_get_auto_zero_reference(bool* out_enable);

/**
 * @brief Update CONFIG1.OSR[3:0] while keeping prescaler and reserved bits intact.
 *
 * Performs a read-modify-write of CONFIG1 so the prescaler (PRE[1:0]) remains
 * untouched and the reserved bits [1:0] stay cleared. The driver must be
 * initialised before use.
 *
 * @param osr Requested oversampling ratio.
 * @return MCP356X_OK on success or a negative error propagated from register
 *         helpers.
 */
int mcp356x_set_osr(mcp356x_osr osr);

/**
 * @brief Read CONFIG1.OSR[3:0] and decode it into the OSR enum.
 *
 * @param out_osr Pointer receiving the decoded oversampling ratio.
 * @return MCP356X_OK on success, MCP356X_ERR_INVALID_ARG when @p out_osr is
 *         NULL, or a propagated error from mcp356x_read_register.
 */
int mcp356x_get_osr(mcp356x_osr* out_osr);

/**
 * @brief Update CONFIG1.PRE[1:0] while preserving OSR and reserved bits.
 *
 * Executes a read-modify-write of CONFIG1 so OSR[3:0] and the reserved LSBs
 * remain untouched. Rejects invalid enum values before touching hardware.
 *
 * @param prescaler Requested prescaler selection (MCLK/1 through MCLK/8).
 * @return MCP356X_OK on success or a negative driver error code.
 */
int mcp356x_set_prescaler(mcp356x_prescaler prescaler);

/**
 * @brief Read CONFIG1.PRE[1:0] and decode it into the prescaler enum.
 *
 * @param out_prescaler Pointer receiving the decoded prescaler value.
 * @return MCP356X_OK on success, MCP356X_ERR_INVALID_ARG when @p out_prescaler is
 *         NULL, or a propagated register access error.
 */
int mcp356x_get_prescaler(mcp356x_prescaler* out_prescaler);

/**
 * @brief Update CONFIG3 conversion mode and data format selections.
 *
 * Applies the requested conversion sequencing mode and SPI output width while
 * preserving CRC-related bits. The helper rejects reserved conversion-mode
 * encodings before touching hardware and requires prior initialisation.
 */
int mcp356x_set_conversion_config(mcp356x_conversion_mode mode, mcp356x_data_format format);

/**
 * @brief Read CONFIG3 and decode the current conversion mode/data format pair.
 *
 * Populates the caller-provided storage with the decoded enums. Requires
 * driver initialisation and validates the output pointers before use.
 */
int mcp356x_get_conversion_config(mcp356x_conversion_mode* out_mode, mcp356x_data_format* out_format);

mcp356x_data_format mcp356x_test_cached_data_format(void);
uint8_t             mcp356x_test_last_data_length(void);
void                mcp356x_test_reset_diagnostics(void);
uint32_t            mcp356x_test_last_raw_word(void);

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
 * @brief Apply the curated default configuration to CONFIG0-3 registers.
 *
 * Convenience wrapper that programmes the Phoenix baseline register image:
 * gain = 1×, OSR = 4096, prescaler = MCLK/1.
 *
 * @return MCP356X_OK if all writes succeed, otherwise propagates the first error encountered.
 */
int mcp356x_apply_default_config(void);

/**
 * @brief Configure CONFIG0-3 using a caller-specified settings struct.
 *
 * Applies the standard Phoenix defaults for CONFIG0/CONFIG3 while populating
 * CONFIG1/CONFIG2 from the provided gain, OSR, and prescaler selections.
 * The driver must be initialised beforehand.
 *
 * @param settings Pointer to the desired configuration payload.
 * @return MCP356X_OK on success, a negative driver error on failure, or
 *         MCP356X_ERR_INVALID_ARG when @p settings is NULL or contains invalid values.
 */
int mcp356x_apply_settings(const mcp356x_settings* settings);

int mcp356x_set_offset_calibration(int32_t code);

int mcp356x_get_offset_calibration(int32_t* out_code);

int mcp356x_set_gain_calibration(uint32_t code);

int mcp356x_get_gain_calibration(uint32_t* out_code);

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
