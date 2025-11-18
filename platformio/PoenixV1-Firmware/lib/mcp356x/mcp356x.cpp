#include "mcp356x.hpp"

#include <Arduino.h>
#include <SPI.h>

// Forward declare delay helper so we can abstract for tests if needed in future.
static inline void mcp356x_delay_ms(uint32_t milliseconds) {
  delay(milliseconds);
}

// ------------------------------ Driver state ---------------------------------
// These globals track the runtime configuration selected via mcp356x_initialize.
static int         g_chip_select_pin = -1;
static bool        g_initialized     = false;
static SPISettings g_spi_settings(1000000UL, MSBFIRST, SPI_MODE0);
// Measurement-speed exception: cache the active data-format locally so we avoid
// re-reading CONFIG3 before every conversion. The style guide discourages
// shadowed register copies, but we document this trade-off to keep high-rate
// acquisition paths within budget.
static mcp356x_data_format g_cached_data_format = mcp356x_data_format::data24;
static uint8_t             g_last_data_length   = 0u;
static uint32_t            g_last_raw_word      = 0u;

// Datasheet helper: verify we stay inside the 0x0..0xF logical register window.
static inline bool mcp356x_is_valid_register(uint8_t reg) {
  return reg <= MCP356X_REG_CRCREG;
}

// Assemble the 8-bit command header:
//   [7:6] = device address, [5:2] = register or fast command code, [1:0] = type
// where type corresponds to the "static" command encoding from Table 6-3.
static inline uint8_t mcp356x_command_byte(uint8_t register_or_command, uint8_t command_type) {
  static const uint8_t k_command_register_mask = 0x0Fu;  // CMD[5:2] encode register or fast-command value.
  static const uint8_t k_command_type_mask     = 0x03u;  // CMD[1:0] select static/fast/incremental mode per Table 6-3.

  return (uint8_t) (((MCP356X_DEVICE_ADDRESS & MCP356X_DEVICE_ADDRESS_MASK) << 6) |
                    ((register_or_command & k_command_register_mask) << 2) | (command_type & k_command_type_mask));
}

static const uint8_t k_config1_prescaler_mask = 0xC0u;  // PRE[1:0] reside in bits 7:6.
static const uint8_t k_config1_osr_mask       = 0x0Fu;  // OSR[3:0] occupies bits 5:2 before shifting.
static const uint8_t k_config1_prescaler_value_mask =
    0x03u;                                               // Raw prescaler enum fits inside two bits before shifting.
static const uint8_t k_config2_gain_mask       = 0x38u;  // CONFIG2.GAIN[2:0] lives in bits 5:3.
static const uint8_t k_config2_gain_field_mask = 0x07u;  // Mask for the 3-bit gain enum prior to shifting.
static const uint8_t k_config2_clear_gain_mask =
    (uint8_t) (~k_config2_gain_mask);                  // Preserves BOOST/AZ bits while zeroing GAIN.
static const uint8_t k_config2_reserved_lsb  = 0x01u;  // CONFIG2 bit0 must remain set per datasheet Section 8.4.
static const uint8_t k_config2_az_mux_mask   = 0x04u;  // CONFIG2 bit2 enables input multiplexer auto-zero.
static const uint8_t k_config2_az_ref_mask   = 0x02u;  // CONFIG2 bit1 enables reference buffer auto-zero.
static const uint8_t k_config3_mode_mask     = 0xC0u;  // CONV_MODE[1:0] occupy bits 7:6.
static const uint8_t k_config3_format_mask   = 0x30u;  // DATA_FORMAT[1:0] occupy bits 5:4.
static const uint8_t k_config3_preserve_mask = 0x0Fu;  // Preserve CRC and reserved bits [3:0].

static inline uint8_t mcp356x_config2_with_gain_bits(mcp356x_gain gain) {
  uint8_t config2 = (uint8_t) (MCP356X_CONFIG2_DEFAULT & k_config2_clear_gain_mask);
  config2 |= (uint8_t) ((static_cast<uint8_t>(gain) & k_config2_gain_field_mask) << 3);
  config2 |= k_config2_reserved_lsb;  // Datasheet mandates CONFIG2 bit0 remains set.
  return config2;
}

static int mcp356x_update_config2_auto_zero(uint8_t bit_mask, bool enable) {
  GUARD_INITIALIZED(g_initialized);

  uint8_t config2_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL));

  if (enable) {
    config2_value |= bit_mask;
  }
  else {
    config2_value &= (uint8_t) (~bit_mask);
  }
  config2_value |= k_config2_reserved_lsb;

  return mcp356x_write_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL);
}

static inline bool mcp356x_osr_is_valid(mcp356x_osr osr) {
  return (static_cast<uint8_t>(osr) & ~k_config1_osr_mask) == 0u;
}

static inline uint8_t mcp356x_config1_with_osr_bits(mcp356x_osr osr, uint8_t preserved_prescaler_bits) {
  uint8_t config1 = (uint8_t) (preserved_prescaler_bits & k_config1_prescaler_mask);
  config1 |= (uint8_t) ((static_cast<uint8_t>(osr) & k_config1_osr_mask) << 2);
  return config1;
}

static inline bool mcp356x_prescaler_is_valid(mcp356x_prescaler prescaler) {
  return (static_cast<uint8_t>(prescaler) & ~k_config1_prescaler_value_mask) == 0u;
}

static inline uint8_t mcp356x_config1_with_prescaler_bits(mcp356x_prescaler prescaler, uint8_t preserved_osr_bits) {
  uint8_t config1 = (uint8_t) (preserved_osr_bits & (uint8_t) (~k_config1_prescaler_mask));
  config1 |= (uint8_t) ((static_cast<uint8_t>(prescaler) & k_config1_prescaler_value_mask) << 6);
  return config1;
}

static inline bool mcp356x_conversion_mode_is_valid(mcp356x_conversion_mode mode) {
  return (static_cast<uint8_t>(mode) & 0x03u) != 0x03u;
}

static inline uint8_t mcp356x_config3_with_mode_format(mcp356x_conversion_mode mode, mcp356x_data_format format,
                                                       uint8_t preserved_lower_bits) {
  uint8_t config3 = (uint8_t) (preserved_lower_bits & k_config3_preserve_mask);
  config3 |= (uint8_t) ((static_cast<uint8_t>(mode) & 0x03u) << 6);
  config3 |= (uint8_t) ((static_cast<uint8_t>(format) & 0x03u) << 4);
  return config3;
}

static inline uint8_t mcp356x_data_length_from_format(mcp356x_data_format format) {
  if (format == mcp356x_data_format::data24) {
    return 3u;
  }
  return 4u;
}

static int mcp356x_update_irq_register(uint8_t mask, uint8_t desired_bits) {
  GUARD_INITIALIZED(g_initialized);

  uint8_t irq_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_IRQ, &irq_value, 1u, NULL));

  desired_bits &= mask;
  const uint8_t preserved_bits = (uint8_t) (irq_value & (uint8_t) (~mask));
  const uint8_t updated_value  = (uint8_t) (preserved_bits | desired_bits);

  if (updated_value == irq_value) {
    return MCP356X_OK;
  }

  return mcp356x_write_register(MCP356X_REG_IRQ, &updated_value, 1u, NULL);
}
void mcp356x_force_uninitialized_for_test(void) {
  g_initialized = false;
}

int mcp356x_initialize(int chip_select_pin, uint32_t spi_clock_hz) {
  // Guardrail checks: CS must be valid, SPI clock must be non-zero.
  // Step 1: Reject invalid pin assignments or zero-frequency SPI configs.
  if (chip_select_pin < 0 || spi_clock_hz == 0) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 2: Cache the hardware configuration for downstream transactions.
  g_chip_select_pin = chip_select_pin;
  g_spi_settings    = SPISettings(spi_clock_hz, MSBFIRST, SPI_MODE0);

  // Step 3: Prepare the chip-select line and bring up the SPI peripheral.
  pinMode(g_chip_select_pin, OUTPUT);
  digitalWrite(g_chip_select_pin, HIGH);

  SPI.begin();
  g_initialized = true;
  return MCP356X_OK;
}

int mcp356x_send_fast_command(uint8_t command_code, uint8_t* status_byte) {
  // Fast commands always require the driver to be initialised and STATUS storage.
  // Step 1: Reject calls without initialisation or status storage.
  GUARD_INITIALIZED(g_initialized);
  GUARD_NONNULL(status_byte);

  // Build command byte: [7:6]=device address, [5:2]=command, [1:0]=type (00 for fast command)
  // Step 2: Assemble the on-wire command header according to Table 6-3.
  uint8_t command =
      (uint8_t) (((MCP356X_DEVICE_ADDRESS & MCP356X_DEVICE_ADDRESS_MASK) << 6) | ((command_code & 0x0Fu) << 2));

  // Step 3: Issue the SPI transaction and capture the returned STATUS byte.
  SPI.beginTransaction(g_spi_settings);
  digitalWrite(g_chip_select_pin, LOW);
  uint8_t status = SPI.transfer(command);
  digitalWrite(g_chip_select_pin, HIGH);
  SPI.endTransaction();

  *status_byte = status;
  return MCP356X_OK;
}

static int mcp356x_issue_fast_command(uint8_t command_code, uint8_t* status_byte) {
  uint8_t scratch_status = 0xFFu;
  if (status_byte == NULL) {
    status_byte = &scratch_status;
  }

  return mcp356x_send_fast_command(command_code, status_byte);
}

int mcp356x_read_register(uint8_t register_address, uint8_t* buffer, size_t length, uint8_t* status_byte) {
  // Static read operation (single header followed by 1-4 data bytes clocked out).
  // Validate runtime state and caller parameters before touching the bus.
  // Step 1: Enforce initialisation and parameter bounds before the SPI transfer.
  GUARD_INITIALIZED(g_initialized);
  GUARD_NONNULL(buffer);
  if (length == 0 || length > 4) {
    return MCP356X_ERR_INVALID_ARG;
  }
  if (!mcp356x_is_valid_register(register_address)) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 2: Compose the static-read command header.
  uint8_t command = mcp356x_command_byte(register_address, 0x01u);  // Static read

  // Step 3: Clock out the STATUS byte and requested register payload.
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

int mcp356x_write_register(uint8_t register_address, const uint8_t* buffer, size_t length, uint8_t* status_byte) {
  // Static write operation (single header followed by 1-4 data bytes clocked in).
  // Validate runtime state and caller parameters before touching the bus.
  // Step 1: Confirm initialisation and validate parameters before writing.
  GUARD_INITIALIZED(g_initialized);
  GUARD_NONNULL(buffer);
  if (length == 0 || length > 4) {
    return MCP356X_ERR_INVALID_ARG;
  }
  if (!mcp356x_is_valid_register(register_address)) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 2: Build the static-write command header.
  uint8_t command = mcp356x_command_byte(register_address, 0x02u);  // Static write

  // Step 3: Stream the header and data bytes while capturing STATUS.
  SPI.beginTransaction(g_spi_settings);
  digitalWrite(g_chip_select_pin, LOW);
  // STATUS is sampled during the header transfer and optionally returned.
  uint8_t status = SPI.transfer(command);
  for (size_t i = 0; i < length; ++i) {
    (void) SPI.transfer(buffer[i]);
  }
  digitalWrite(g_chip_select_pin, HIGH);
  SPI.endTransaction();

  if (status_byte != NULL) {
    *status_byte = status;
  }
  return MCP356X_OK;
}

int mcp356x_select_single_ended_channel(uint8_t channel_index) {
  // Only channels 0-7 map to the single-ended inputs; reject anything outside that range.
  // Step 1: Validate the mux index before altering hardware state.
  if (channel_index > MCP356X_MUX_CH7) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 2: Encode the single-ended selection with AGND on the negative mux.
  uint8_t mux_value = (uint8_t) ((channel_index << 4) | MCP356X_MUX_AGND);
  return mcp356x_write_register(MCP356X_REG_MUX, &mux_value, 1u, NULL);
}

int mcp356x_set_gain(mcp356x_gain gain) {
  // Step 1: Require initialisation before touching configuration registers.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read the existing CONFIG2 register so we can preserve unrelated bits.
  uint8_t config2_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL));

  const uint8_t preserved_bits = (uint8_t) (config2_value & k_config2_clear_gain_mask);  // Clear GAIN[5:3].
  const uint8_t gain_bits      = (uint8_t) (static_cast<uint8_t>(gain) & k_config2_gain_field_mask);

  // Step 3: Merge the new gain setting and write it back to the device.
  config2_value = (uint8_t) (preserved_bits | (uint8_t) (gain_bits << 3));
  config2_value |= k_config2_reserved_lsb;  // Datasheet mandates CONFIG2 bit0 remains 1.

  return mcp356x_write_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL);
}

int mcp356x_get_gain(mcp356x_gain* out_gain) {
  // Step 1: Validate output storage and ensure the driver is initialised.
  GUARD_NONNULL(out_gain);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read CONFIG2 and extract the GAIN bits as a driver enum.
  uint8_t config2_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL));

  uint8_t gain_bits = (uint8_t) ((config2_value >> 3) & 0x07u);
  *out_gain         = static_cast<mcp356x_gain>(gain_bits);

  return MCP356X_OK;
}

int mcp356x_set_auto_zero_mux(bool enable) {
  return mcp356x_update_config2_auto_zero(k_config2_az_mux_mask, enable);
}

int mcp356x_get_auto_zero_mux(bool* out_enable) {
  GUARD_NONNULL(out_enable);
  GUARD_INITIALIZED(g_initialized);

  uint8_t config2_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL));

  *out_enable = (config2_value & k_config2_az_mux_mask) != 0u;
  return MCP356X_OK;
}

int mcp356x_set_auto_zero_reference(bool enable) {
  return mcp356x_update_config2_auto_zero(k_config2_az_ref_mask, enable);
}

int mcp356x_get_auto_zero_reference(bool* out_enable) {
  GUARD_NONNULL(out_enable);
  GUARD_INITIALIZED(g_initialized);

  uint8_t config2_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL));

  *out_enable = (config2_value & k_config2_az_ref_mask) != 0u;
  return MCP356X_OK;
}

int mcp356x_set_irq_mode(mcp356x_irq_mode mode) {
  // Step 1: Require driver initialisation before touching the IRQ register.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Reject bit patterns outside the two-bit IRQ_MODE field.
  const uint8_t mode_bits = static_cast<uint8_t>(mode);
  if ((mode_bits & ~0x03u) != 0u) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 3: Update only the IRQ_MODE bits while preserving the status nibble.
  const uint8_t desired_bits = (uint8_t) (mode_bits << 2);
  return mcp356x_update_irq_register(MCP356X_IRQ_MODE_MASK, desired_bits);
}

int mcp356x_get_irq_mode(mcp356x_irq_mode* out_mode) {
  // Step 1: Validate caller storage and initialisation state.
  GUARD_NONNULL(out_mode);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read the IRQ register and decode the mode bits.
  uint8_t irq_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_IRQ, &irq_value, 1u, NULL));

  const uint8_t mode_bits = (uint8_t) ((irq_value & MCP356X_IRQ_MODE_MASK) >> 2);
  *out_mode               = static_cast<mcp356x_irq_mode>(mode_bits & 0x03u);
  return MCP356X_OK;
}

int mcp356x_set_irq_fastcmd_enabled(bool enable) {
  // Step 1: Ensure the driver is initialised before updating the IRQ register.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Update the EN_FASTCMD bit while preserving other fields.
  const uint8_t desired_bits = enable ? MCP356X_IRQ_EN_FASTCMD_MASK : 0u;
  return mcp356x_update_irq_register(MCP356X_IRQ_EN_FASTCMD_MASK, desired_bits);
}

int mcp356x_get_irq_fastcmd_enabled(bool* out_enable) {
  // Step 1: Validate output storage and initialisation state.
  GUARD_NONNULL(out_enable);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read and decode the EN_FASTCMD bit.
  uint8_t irq_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_IRQ, &irq_value, 1u, NULL));

  *out_enable = (irq_value & MCP356X_IRQ_EN_FASTCMD_MASK) != 0u;
  return MCP356X_OK;
}

int mcp356x_set_irq_conversion_start_interrupt_enabled(bool enable) {
  // Step 1: Ensure the driver is initialised before updating the IRQ register.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Update the EN_STP bit while preserving other fields.
  const uint8_t desired_bits = enable ? MCP356X_IRQ_EN_CONV_START_MASK : 0u;
  return mcp356x_update_irq_register(MCP356X_IRQ_EN_CONV_START_MASK, desired_bits);
}

int mcp356x_get_irq_conversion_start_interrupt_enabled(bool* out_enable) {
  // Step 1: Validate output storage and initialisation state.
  GUARD_NONNULL(out_enable);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read and decode the EN_STP bit.
  uint8_t irq_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_IRQ, &irq_value, 1u, NULL));

  *out_enable = (irq_value & MCP356X_IRQ_EN_CONV_START_MASK) != 0u;
  return MCP356X_OK;
}

int mcp356x_set_osr(mcp356x_osr osr) {
  // Step 1: Guard against use before driver initialisation.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Reject bit patterns outside the CONFIG1.OSR[3:0] encoding range.
  if (!mcp356x_osr_is_valid(osr)) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 3: Read CONFIG1 so we can preserve the prescaler bits on update.
  uint8_t config1_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL));

  const uint8_t preserved_prescaler = (uint8_t) (config1_value & k_config1_prescaler_mask);
  uint8_t       updated_config1     = mcp356x_config1_with_osr_bits(osr, preserved_prescaler);

  // Step 4: Write the updated value back to CONFIG1.
  return mcp356x_write_register(MCP356X_REG_CONFIG1, &updated_config1, 1u, NULL);
}

int mcp356x_get_osr(mcp356x_osr* out_osr) {
  // Step 1: Validate output storage and runtime initialisation.
  GUARD_NONNULL(out_osr);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read CONFIG1 and decode the OSR field.
  uint8_t config1_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL));

  const uint8_t osr_bits = (uint8_t) ((config1_value >> 2) & k_config1_osr_mask);
  *out_osr               = static_cast<mcp356x_osr>(osr_bits);
  return MCP356X_OK;
}

int mcp356x_set_prescaler(mcp356x_prescaler prescaler) {
  // Step 1: Prevent configuration updates before driver initialisation.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Reject undefined prescaler encodings so CONFIG1.PRE stays in-range.
  if (!mcp356x_prescaler_is_valid(prescaler)) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 3: Read CONFIG1 so OSR bits and reserved LSBs can be preserved.
  uint8_t config1_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL));

  const uint8_t preserved_osr_bits = (uint8_t) (config1_value & (uint8_t) (~k_config1_prescaler_mask));
  uint8_t       updated_config1    = mcp356x_config1_with_prescaler_bits(prescaler, preserved_osr_bits);

  // Step 4: Program the updated CONFIG1 image back into the device.
  return mcp356x_write_register(MCP356X_REG_CONFIG1, &updated_config1, 1u, NULL);
}

int mcp356x_get_prescaler(mcp356x_prescaler* out_prescaler) {
  // Step 1: Validate pointer arguments and initialisation state.
  GUARD_NONNULL(out_prescaler);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read CONFIG1 and extract the prescaler field.
  uint8_t config1_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL));

  const uint8_t prescaler_bits = (uint8_t) ((config1_value >> 6) & k_config1_prescaler_value_mask);
  *out_prescaler               = static_cast<mcp356x_prescaler>(prescaler_bits);
  return MCP356X_OK;
}

int mcp356x_set_conversion_config(mcp356x_conversion_mode mode, mcp356x_data_format format) {
  // Step 1: Require initialisation before touching CONFIG3.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Reject reserved conversion-mode encodings so we never clock invalid bits into CONFIG3.
  if (!mcp356x_conversion_mode_is_valid(mode)) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 3: Read CONFIG3 to preserve CRC configuration and reserved bits in the lower nibble.
  uint8_t config3_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG3, &config3_value, 1u, NULL));

  const uint8_t updated_config3 = mcp356x_config3_with_mode_format(mode, format, config3_value);

  // Step 4: Write the updated CONFIG3 image back to the device.
  const int return_code = mcp356x_write_register(MCP356X_REG_CONFIG3, &updated_config3, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  g_cached_data_format = format;
  return MCP356X_OK;
}

int mcp356x_get_conversion_config(mcp356x_conversion_mode* out_mode, mcp356x_data_format* out_format) {
  // Step 1: Validate output storage and ensure the driver has been initialised.
  GUARD_NONNULL(out_mode);
  GUARD_NONNULL(out_format);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read CONFIG3 and decode the conversion mode/data format fields.
  uint8_t config3_value = 0u;
  GUARD(mcp356x_read_register(MCP356X_REG_CONFIG3, &config3_value, 1u, NULL));

  const uint8_t mode_bits = (uint8_t) ((config3_value & k_config3_mode_mask) >> 6);
  if (mode_bits == 0x03u) {
    return MCP356X_ERR_UNSUPPORTED;
  }
  const uint8_t format_bits = (uint8_t) ((config3_value & k_config3_format_mask) >> 4);

  *out_mode   = static_cast<mcp356x_conversion_mode>(mode_bits);
  *out_format = static_cast<mcp356x_data_format>(format_bits);
  return MCP356X_OK;
}

int mcp356x_apply_default_config(void) {
  // Step 1: Compose the datasheet baseline so downstream helpers see a full register image.
  const mcp356x_settings defaults = {
      mcp356x_gain::gain_x1,
      mcp356x_osr::osr_4096,
      mcp356x_prescaler::mclk_div1,
      mcp356x_conversion_mode::oneshot_shutdown,
      mcp356x_data_format::data24,
      mcp356x_irq_mode::irq_push_pull,
      true,
      false,
  };
  // Step 2: Delegate to the unified helper so CONFIG0-3 are programmed consistently.
  return mcp356x_apply_settings(&defaults);
}

int mcp356x_apply_settings(const mcp356x_settings* settings) {
  // Step 1: Reject NULL inputs and ensure the driver has been initialised.
  GUARD_NONNULL(settings);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Validate enum encodings before altering CONFIG registers.
  if (!mcp356x_osr_is_valid(settings->osr)) {
    return MCP356X_ERR_INVALID_ARG;
  }
  if (!mcp356x_prescaler_is_valid(settings->prescaler)) {
    return MCP356X_ERR_INVALID_ARG;
  }
  if (!mcp356x_conversion_mode_is_valid(settings->conversion_mode)) {
    return MCP356X_ERR_INVALID_ARG;
  }
  if ((static_cast<uint8_t>(settings->irq_mode) & 0xFCu) != 0u) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 3: Compose CONFIG0-3 images to reflect the requested gain/OSR/prescaler trio.
  const uint8_t config0_value = MCP356X_CONFIG0_DEFAULT;
  const uint8_t prescaler_bits =
      (uint8_t) ((static_cast<uint8_t>(settings->prescaler) & k_config1_prescaler_value_mask) << 6);
  const uint8_t config1_value = mcp356x_config1_with_osr_bits(settings->osr, prescaler_bits);
  const uint8_t config2_value = mcp356x_config2_with_gain_bits(settings->gain);
  const uint8_t config3_value =
      mcp356x_config3_with_mode_format(settings->conversion_mode, settings->data_format, MCP356X_CONFIG3_DEFAULT);

  // Step 4: Program each CONFIG register sequentially so the device sees a coherent update.
  int return_code = mcp356x_write_register(MCP356X_REG_CONFIG0, &config0_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  return_code = mcp356x_write_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  return_code = mcp356x_write_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  return_code = mcp356x_write_register(MCP356X_REG_CONFIG3, &config3_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  return_code = mcp356x_set_irq_mode(settings->irq_mode);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  return_code = mcp356x_set_irq_fastcmd_enabled(settings->irq_fastcmd_enabled);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  return_code = mcp356x_set_irq_conversion_start_interrupt_enabled(settings->irq_conversion_start_interrupt_enabled);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  g_cached_data_format = settings->data_format;
  return MCP356X_OK;
}

int mcp356x_set_offset_calibration(int32_t code) {
  // Step 1: Guard against use before driver initialisation.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Enforce the signed 24-bit range documented for OFFSETCAL.
  if (code < -0x00800000 || code > 0x007FFFFF) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 3: Encode the two's-complement payload and commit it to the register.
  const uint32_t encoded   = (uint32_t) code & 0x00FFFFFFu;
  const uint8_t  buffer[3] = {
      (uint8_t) ((encoded >> 16) & 0xFFu),
      (uint8_t) ((encoded >> 8) & 0xFFu),
      (uint8_t) (encoded & 0xFFu),
  };
  return mcp356x_write_register(MCP356X_REG_OFFSETCAL, buffer, sizeof(buffer), NULL);
}

int mcp356x_get_offset_calibration(int32_t* out_code) {
  // Step 1: Validate pointer arguments and initialisation state.
  GUARD_NONNULL(out_code);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read the 24-bit OFFSETCAL register image.
  uint8_t buffer[3] = {0u};
  GUARD(mcp356x_read_register(MCP356X_REG_OFFSETCAL, buffer, sizeof(buffer), NULL));

  // Step 3: Sign-extend the two's-complement value to 32 bits for the caller.
  int32_t value = (int32_t) ((buffer[0] << 16) | (buffer[1] << 8) | buffer[2]);
  if ((value & 0x00800000) != 0) {
    value |= (int32_t) 0xFF000000;
  }

  *out_code = value;
  return MCP356X_OK;
}

int mcp356x_set_gain_calibration(uint32_t code) {
  // Step 1: Guard against use before driver initialisation.
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Ensure the unsigned 24-bit field does not overflow.
  if ((code & 0xFF000000u) != 0u) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 3: Encode the calibration value and update GAINCAL.
  const uint8_t buffer[3] = {
      (uint8_t) ((code >> 16) & 0xFFu),
      (uint8_t) ((code >> 8) & 0xFFu),
      (uint8_t) (code & 0xFFu),
  };
  return mcp356x_write_register(MCP356X_REG_GAINCAL, buffer, sizeof(buffer), NULL);
}

int mcp356x_get_gain_calibration(uint32_t* out_code) {
  // Step 1: Validate pointer arguments and initialisation state.
  GUARD_NONNULL(out_code);
  GUARD_INITIALIZED(g_initialized);

  // Step 2: Read the 24-bit GAINCAL register and decode it.
  uint8_t buffer[3] = {0u};
  GUARD(mcp356x_read_register(MCP356X_REG_GAINCAL, buffer, sizeof(buffer), NULL));

  *out_code = ((uint32_t) buffer[0] << 16) | ((uint32_t) buffer[1] << 8) | buffer[2];
  return MCP356X_OK;
}

int mcp356x_start_conversion(uint8_t* status_byte) {
  return mcp356x_issue_fast_command(MCP356X_FASTCMD_START, status_byte);
}

int mcp356x_enter_standby(uint8_t* status_byte) {
  return mcp356x_issue_fast_command(MCP356X_FASTCMD_STANDBY, status_byte);
}

int mcp356x_enter_adc_shutdown(uint8_t* status_byte) {
  return mcp356x_issue_fast_command(MCP356X_FASTCMD_ADCSHUTDN, status_byte);
}

int mcp356x_enter_full_shutdown(uint8_t* status_byte) {
  return mcp356x_issue_fast_command(MCP356X_FASTCMD_FULLSHUTDN, status_byte);
}

int mcp356x_full_reset(uint8_t* status_byte) {
  int return_code = mcp356x_issue_fast_command(MCP356X_FASTCMD_FULLRESET, status_byte);
  if (return_code == MCP356X_OK) {
    g_cached_data_format = mcp356x_data_format::data24;
  }
  return return_code;
}

int mcp356x_read_single_ended_channel(uint8_t channel_index, uint32_t timeout_us, int32_t* result) {
  // Step 1: Ensure we have a destination for the conversion result.
  GUARD_NONNULL(result);
  // Step 2: Require driver initialisation.
  GUARD_INITIALIZED(g_initialized);

  // Step 3: Program the mux to the requested single-ended channel.
  int return_code = mcp356x_select_single_ended_channel(channel_index);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  // Step 4: Kick off a conversion.
  return_code = mcp356x_start_conversion(NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  const uint8_t payload_length = mcp356x_data_length_from_format(g_cached_data_format);
  uint8_t       adc_bytes[4]   = {0};
  uint32_t      elapsed_us     = 0u;  // use microsecond resolution to reduce blocking floor
  bool          data_ready     = false;
  g_last_data_length           = 0u;
  g_last_raw_word              = 0u;

  // Step 5: Poll the ADC result register until data is ready or the timeout expires.
  while (!data_ready) {
    uint8_t read_status = 0xFFu;
    return_code         = mcp356x_read_register(MCP356X_REG_ADCDATA, adc_bytes, payload_length, &read_status);
    if (return_code != MCP356X_OK) {
      return return_code;
    }

    data_ready = ((read_status & MCP356X_STATUS_DR_MASK) == 0u);
    if (data_ready) {
      g_last_data_length = payload_length;
      break;
    }

    // Enforce microsecond-timeout budget and use short sleeps to lower blocking floor.
    if ((timeout_us > 0u) && (elapsed_us >= timeout_us)) {
      return MCP356X_ERR_TIMEOUT;
    }

    // Short sleep to lower the blocking-path floor while still yielding CPU.
    delayMicroseconds(100);
    elapsed_us += 100u;
  }

  uint32_t raw_word  = 0u;
  int32_t  raw_value = 0;

  switch (g_cached_data_format) {
    case mcp356x_data_format::data24: {
      raw_word  = (uint32_t) ((adc_bytes[0] << 16) | (adc_bytes[1] << 8) | adc_bytes[2]);
      raw_value = (int32_t) raw_word;
      if (raw_value & 0x800000) {
        raw_value |= 0xFF000000;
      }
      break;
    }
    case mcp356x_data_format::data32_left: {
      raw_word  = (uint32_t) (((uint32_t) adc_bytes[0] << 24) | ((uint32_t) adc_bytes[1] << 16) |
                             ((uint32_t) adc_bytes[2] << 8) | adc_bytes[3]);
      raw_value = (int32_t) (static_cast<int32_t>(raw_word) >> 8);
      break;
    }
    case mcp356x_data_format::data32_signed: {
      raw_word  = (uint32_t) (((uint32_t) adc_bytes[0] << 24) | ((uint32_t) adc_bytes[1] << 16) |
                             ((uint32_t) adc_bytes[2] << 8) | adc_bytes[3]);
      raw_value = (int32_t) raw_word;
      break;
    }
    case mcp356x_data_format::data32_signed_chid: {
      raw_word  = (uint32_t) (((uint32_t) adc_bytes[0] << 24) | ((uint32_t) adc_bytes[1] << 16) |
                             ((uint32_t) adc_bytes[2] << 8) | adc_bytes[3]);
      raw_value = (int32_t) (static_cast<int32_t>(raw_word) >> 8);
      break;
    }
    default: {
      return MCP356X_ERR_UNSUPPORTED;
    }
  }

  g_last_raw_word = raw_word;

  // Step 7: Publish the conversion outcome to the caller.
  *result = raw_value;
  return MCP356X_OK;
}

mcp356x_data_format mcp356x_test_cached_data_format(void) {
  return g_cached_data_format;
}

uint8_t mcp356x_test_last_data_length(void) {
  return g_last_data_length;
}

void mcp356x_test_reset_diagnostics(void) {
  g_last_data_length = 0u;
  g_last_raw_word    = 0u;
}

uint32_t mcp356x_test_last_raw_word(void) {
  return g_last_raw_word;
}

uint32_t mcp356x_estimate_conversion_delay(mcp356x_osr osr, mcp356x_sampling_mode mode) {
  struct ConversionLatencyEntry {
    mcp356x_osr osr;
    uint32_t    blocking_latency_us;
    uint32_t    irq_latency_us;
  };

  // Step 1: Bound conversion delays using worst-case latencies captured on Stormcloud v1.0.0
  // (AMCLK 4.9152 MHz) via docs/phoenix-benchmark/sample_plans/osr_latency_multiple_runs.json;
  // see python/benchmark_runs/report.md for the full measurement tables.
  static constexpr ConversionLatencyEntry k_conversion_latency_table[] = {
      {mcp356x_osr::osr_32, 977u, 977u},        {mcp356x_osr::osr_64, 977u, 977u},
      {mcp356x_osr::osr_128, 977u, 977u},       {mcp356x_osr::osr_256, 977u, 977u},
      {mcp356x_osr::osr_512, 1954u, 1954u},     {mcp356x_osr::osr_1024, 2930u, 2930u},
      {mcp356x_osr::osr_2048, 2930u, 2930u},    {mcp356x_osr::osr_4096, 4883u, 4883u},
      {mcp356x_osr::osr_8192, 8790u, 8790u},    {mcp356x_osr::osr_16384, 15625u, 15625u},
      {mcp356x_osr::osr_20480, 19532u, 19532u}, {mcp356x_osr::osr_24576, 23438u, 23438u},
      {mcp356x_osr::osr_40960, 37110u, 37110u}, {mcp356x_osr::osr_49152, 44922u, 44922u},
      {mcp356x_osr::osr_81920, 73243u, 73243u}, {mcp356x_osr::osr_98304, 87891u, 87891u},
  };

  for (size_t index = 0u; index < (sizeof(k_conversion_latency_table) / sizeof(k_conversion_latency_table[0]));
       ++index) {
    if (k_conversion_latency_table[index].osr == osr) {
      // Step 2: Return the measured latency matching the requested sampling path.
      return (mode == mcp356x_sampling_mode::blocking) ? k_conversion_latency_table[index].blocking_latency_us :
                                                         k_conversion_latency_table[index].irq_latency_us;
    }
  }

  // Step 3: Guardrail for unexpected OSR enums; zero signals a missing table entry.
  return 0u;
}
