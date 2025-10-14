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

  return (uint8_t)(((MCP356X_DEVICE_ADDRESS & MCP356X_DEVICE_ADDRESS_MASK) << 6) |
                   ((register_or_command & k_command_register_mask) << 2) | (command_type & k_command_type_mask));
}

static const uint8_t k_config1_prescaler_mask = 0xC0u;  // PRE[1:0] reside in bits 7:6.
static const uint8_t k_config1_osr_mask       = 0x0Fu;  // OSR[3:0] occupies bits 5:2 before shifting.
static const uint8_t k_config1_prescaler_value_mask =
    0x03u;                                               // Raw prescaler enum fits inside two bits before shifting.
static const uint8_t k_config2_gain_mask       = 0x38u;  // CONFIG2.GAIN[2:0] lives in bits 5:3.
static const uint8_t k_config2_gain_field_mask = 0x07u;  // Mask for the 3-bit gain enum prior to shifting.
static const uint8_t k_config2_clear_gain_mask =
    (uint8_t)(~k_config2_gain_mask);                  // Preserves BOOST/AZ bits while zeroing GAIN.
static const uint8_t k_config2_reserved_lsb = 0x01u;  // CONFIG2 bit0 must remain set per datasheet Section 8.4.

static inline uint8_t mcp356x_config2_with_gain_bits(mcp356x_gain gain) {
  uint8_t config2 = (uint8_t)(MCP356X_CONFIG2_DEFAULT & k_config2_clear_gain_mask);
  config2 |= (uint8_t)((static_cast<uint8_t>(gain) & k_config2_gain_field_mask) << 3);
  config2 |= k_config2_reserved_lsb;  // Datasheet mandates CONFIG2 bit0 remains set.
  return config2;
}

static inline bool mcp356x_osr_is_valid(mcp356x_osr osr) {
  return (static_cast<uint8_t>(osr) & ~k_config1_osr_mask) == 0u;
}

static inline uint8_t mcp356x_config1_with_osr_bits(mcp356x_osr osr, uint8_t preserved_prescaler_bits) {
  uint8_t config1 = (uint8_t)(preserved_prescaler_bits & k_config1_prescaler_mask);
  config1 |= (uint8_t)((static_cast<uint8_t>(osr) & k_config1_osr_mask) << 2);
  return config1;
}

static inline bool mcp356x_prescaler_is_valid(mcp356x_prescaler prescaler) {
  return (static_cast<uint8_t>(prescaler) & ~k_config1_prescaler_value_mask) == 0u;
}

static inline uint8_t mcp356x_config1_with_prescaler_bits(mcp356x_prescaler prescaler, uint8_t preserved_osr_bits) {
  uint8_t config1 = (uint8_t)(preserved_osr_bits & (uint8_t)(~k_config1_prescaler_mask));
  config1 |= (uint8_t)((static_cast<uint8_t>(prescaler) & k_config1_prescaler_value_mask) << 6);
  return config1;
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
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }
  if (status_byte == NULL) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Build command byte: [7:6]=device address, [5:2]=command, [1:0]=type (00 for fast command)
  // Step 2: Assemble the on-wire command header according to Table 6-3.
  uint8_t command =
      (uint8_t)(((MCP356X_DEVICE_ADDRESS & MCP356X_DEVICE_ADDRESS_MASK) << 6) | ((command_code & 0x0Fu) << 2));

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
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }
  if (buffer == NULL || length == 0 || length > 4) {
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
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }
  if (buffer == NULL || length == 0 || length > 4) {
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
  uint8_t mux_value = (uint8_t)((channel_index << 4) | MCP356X_MUX_AGND);
  return mcp356x_write_register(MCP356X_REG_MUX, &mux_value, 1u, NULL);
}

int mcp356x_set_gain(mcp356x_gain gain) {
  // Step 1: Require initialisation before touching configuration registers.
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Read the existing CONFIG2 register so we can preserve unrelated bits.
  uint8_t config2_value = 0u;
  int     return_code   = mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  const uint8_t preserved_bits = (uint8_t)(config2_value & k_config2_clear_gain_mask);  // Clear GAIN[5:3].
  const uint8_t gain_bits      = (uint8_t)(static_cast<uint8_t>(gain) & k_config2_gain_field_mask);

  // Step 3: Merge the new gain setting and write it back to the device.
  config2_value = (uint8_t)(preserved_bits | (uint8_t)(gain_bits << 3));
  config2_value |= k_config2_reserved_lsb;  // Datasheet mandates CONFIG2 bit0 remains 1.

  return mcp356x_write_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL);
}

int mcp356x_get_gain(mcp356x_gain* out_gain) {
  // Step 1: Validate output storage and ensure the driver is initialised.
  if (out_gain == NULL) {
    return MCP356X_ERR_INVALID_ARG;
  }
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Read CONFIG2 and extract the GAIN bits as a driver enum.
  uint8_t config2_value = 0u;
  int     return_code   = mcp356x_read_register(MCP356X_REG_CONFIG2, &config2_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  uint8_t gain_bits = (uint8_t)((config2_value >> 3) & 0x07u);
  *out_gain         = static_cast<mcp356x_gain>(gain_bits);

  return MCP356X_OK;
}

int mcp356x_set_osr(mcp356x_osr osr) {
  // Step 1: Guard against use before driver initialisation.
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Reject bit patterns outside the CONFIG1.OSR[3:0] encoding range.
  if (!mcp356x_osr_is_valid(osr)) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 3: Read CONFIG1 so we can preserve the prescaler bits on update.
  uint8_t config1_value = 0u;
  int     return_code   = mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  const uint8_t preserved_prescaler = (uint8_t)(config1_value & k_config1_prescaler_mask);
  uint8_t       updated_config1     = mcp356x_config1_with_osr_bits(osr, preserved_prescaler);

  // Step 4: Write the updated value back to CONFIG1.
  return mcp356x_write_register(MCP356X_REG_CONFIG1, &updated_config1, 1u, NULL);
}

int mcp356x_get_osr(mcp356x_osr* out_osr) {
  // Step 1: Validate output storage and runtime initialisation.
  if (out_osr == NULL) {
    return MCP356X_ERR_INVALID_ARG;
  }
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Read CONFIG1 and decode the OSR field.
  uint8_t config1_value = 0u;
  int     return_code   = mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  const uint8_t osr_bits = (uint8_t)((config1_value >> 2) & k_config1_osr_mask);
  *out_osr               = static_cast<mcp356x_osr>(osr_bits);
  return MCP356X_OK;
}

int mcp356x_set_prescaler(mcp356x_prescaler prescaler) {
  // Step 1: Prevent configuration updates before driver initialisation.
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Reject undefined prescaler encodings so CONFIG1.PRE stays in-range.
  if (!mcp356x_prescaler_is_valid(prescaler)) {
    return MCP356X_ERR_INVALID_ARG;
  }

  // Step 3: Read CONFIG1 so OSR bits and reserved LSBs can be preserved.
  uint8_t config1_value = 0u;
  int     return_code   = mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  const uint8_t preserved_osr_bits = (uint8_t)(config1_value & (uint8_t)(~k_config1_prescaler_mask));
  uint8_t       updated_config1    = mcp356x_config1_with_prescaler_bits(prescaler, preserved_osr_bits);

  // Step 4: Program the updated CONFIG1 image back into the device.
  return mcp356x_write_register(MCP356X_REG_CONFIG1, &updated_config1, 1u, NULL);
}

int mcp356x_get_prescaler(mcp356x_prescaler* out_prescaler) {
  // Step 1: Validate pointer arguments and initialisation state.
  if (out_prescaler == NULL) {
    return MCP356X_ERR_INVALID_ARG;
  }
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Read CONFIG1 and extract the prescaler field.
  uint8_t config1_value = 0u;
  int     return_code   = mcp356x_read_register(MCP356X_REG_CONFIG1, &config1_value, 1u, NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  const uint8_t prescaler_bits = (uint8_t)((config1_value >> 6) & k_config1_prescaler_value_mask);
  *out_prescaler               = static_cast<mcp356x_prescaler>(prescaler_bits);
  return MCP356X_OK;
}

int mcp356x_apply_default_config(void) {
  return mcp356x_apply_default_config_with_gain_and_osr(mcp356x_gain::gain_x1, mcp356x_osr::osr_4096);
}

int mcp356x_apply_default_config_with_gain(mcp356x_gain gain) {
  return mcp356x_apply_default_config_with_gain_and_osr(gain, mcp356x_osr::osr_4096);
}

int mcp356x_apply_default_config_with_gain_and_osr(mcp356x_gain gain, mcp356x_osr osr) {
  // Step 1: Require an initialised driver before touching configuration registers.
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }

  // Step 2: Validate the OSR encoding before computing register images.
  if (!mcp356x_osr_is_valid(osr)) {
    return MCP356X_ERR_INVALID_ARG;
  }

  const uint8_t config0_value       = MCP356X_CONFIG0_DEFAULT;
  const uint8_t preserved_prescaler = (uint8_t)(MCP356X_CONFIG1_DEFAULT & k_config1_prescaler_mask);
  const uint8_t config1_value       = mcp356x_config1_with_osr_bits(osr, preserved_prescaler);
  const uint8_t config2_value       = mcp356x_config2_with_gain_bits(gain);
  const uint8_t config3_value       = MCP356X_CONFIG3_DEFAULT;

  // Step 3: Sequentially program CONFIG0-3, aborting on the first failure.
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

  return mcp356x_write_register(MCP356X_REG_CONFIG3, &config3_value, 1u, NULL);
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
  return mcp356x_issue_fast_command(MCP356X_FASTCMD_FULLRESET, status_byte);
}

int mcp356x_read_single_ended_channel(uint8_t channel_index, uint32_t timeout_ms, int32_t* result) {
  // Step 1: Ensure we have a destination for the conversion result.
  if (result == NULL) {
    return MCP356X_ERR_INVALID_ARG;
  }
  // Step 2: Require driver initialisation.
  if (!g_initialized) {
    return MCP356X_ERR_NOT_INITIALIZED;
  }

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

  uint8_t  adc_bytes[3] = {0};
  uint32_t elapsed_ms   = 0u;
  bool     data_ready   = false;

  // Step 5: Poll the ADC result register until data is ready or the timeout expires.
  while (!data_ready) {
    uint8_t read_status = 0xFFu;
    return_code         = mcp356x_read_register(MCP356X_REG_ADCDATA, adc_bytes, sizeof adc_bytes, &read_status);
    if (return_code != MCP356X_OK) {
      return return_code;
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

  // Step 6: Combine the 24-bit two's complement result and sign-extend to 32 bits.
  int32_t raw_value = (int32_t)((adc_bytes[0] << 16) | (adc_bytes[1] << 8) | adc_bytes[2]);
  if (raw_value & 0x800000) {
    raw_value |= 0xFF000000;
  }

  // Step 7: Publish the conversion outcome to the caller.
  *result = raw_value;
  return MCP356X_OK;
}
