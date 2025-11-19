#include "adc_hal.hpp"

#include "mcp356x.hpp"
#include <Arduino.h>

static void    adc_hal_clear_stale_drdy(void);
static bool    adc_hal_wait_for_irq_asserted(uint32_t timeout_us);
static bool    adc_hal_is_irq_asserted(void);
static uint8_t adc_hal_payload_length_from_format(mcp356x_data_format format);
static uint8_t adc_hal_get_cached_payload_length(void);
static int     adc_hal_decode_sample(const uint8_t* data_bytes, mcp356x_data_format format, int32_t* sample_out);
static int     adc_hal_read_sample_via_driver(int32_t* sample_out, uint8_t* status_out);

struct AdcHalState {
  bool initialized;
  bool defaults_programmed;
  int  irq_pin;  // Latched DRDY pin so attach/detach helpers avoid global constants.
};

static AdcHalState              g_state                     = {false, false, -1};
static uint32_t                 g_default_config_call_count = 0u;
static AdcHalChannel            g_last_channel_requested    = AdcHalChannel::ADC_HAL_CHANNEL_0;
static adc_hal_irq_wait_hook_t  g_irq_wait_hook             = NULL;
static adc_hal_irq_pin_reader_t g_irq_pin_reader            = NULL;
static int32_t                  g_staged_irq_sample         = 0;
static uint8_t                  g_staged_irq_status         = 0u;
static bool                     g_has_staged_irq_sample     = false;
static mcp356x_data_format      g_cached_data_format        = mcp356x_data_format::data24;
static uint8_t                  g_cached_payload_length     = 3u;
static bool                     g_payload_length_valid      = false;

int adc_hal_initialize(const AdcHalConfig* config) {
  // Step 1: Reject calls that forget required configuration (SPI pins and IRQ line).
  GUARD_NONNULL(config);

  if (config->irq_pin < 0) {
    return ADC_HAL_ERR_INVALID_ARG;
  }

  // Step 2: Bring up the underlying MCP356x driver using the caller-specified SPI parameters.
  const int return_code = mcp356x_initialize(config->chip_select_pin, config->spi_clock_hz);
  if (return_code != MCP356X_OK) {
    return ADC_HAL_ERR_BACKEND_FAILURE;
  }

  // Step 3: Cache the defaults so subsequent calls can reuse them without fetching from the caller.
  g_state.initialized         = true;
  g_state.defaults_programmed = false;
  g_state.irq_pin             = config->irq_pin;

  pinMode(g_state.irq_pin, INPUT);  // External pull-up on DRDY keeps the line idle between reads.

  return ADC_HAL_OK;
}

int adc_hal_apply_default_configuration(void) {
  // Step 1: Ensure the HAL is active before attempting to program registers.
  GUARD_INITIALIZED(g_state.initialized);

  // Step 2: Skip work when defaults already match the requested configuration.
  if (g_state.defaults_programmed) {
    return ADC_HAL_OK;
  }

  // Step 3: Ask the MCP356x driver to apply its canonical defaults so config sources stay unified.
  const int return_code = mcp356x_apply_default_config();
  if (return_code != MCP356X_OK) {
    return ADC_HAL_ERR_BACKEND_FAILURE;
  }

  g_state.defaults_programmed = true;

  // Step 4: Track state used by tests to confirm configuration sequencing.
  g_default_config_call_count += 1u;

  return ADC_HAL_OK;
}

int adc_hal_read_single_ended(AdcHalChannel channel, uint32_t timeout_us, int32_t* code_out) {
  // Step 1: Confirm the caller provided storage for the conversion code.
  GUARD_NONNULL(code_out);

  // Step 2: Require driver initialisation before attempting a conversion.
  GUARD_INITIALIZED(g_state.initialized);

  // Step 3: Validate the channel selection against the hardware's supported range.
  const uint8_t channel_index = static_cast<uint8_t>(channel);
  if (channel_index > static_cast<uint8_t>(AdcHalChannel::ADC_HAL_CHANNEL_7)) {
    return ADC_HAL_ERR_INVALID_ARG;
  }

  // Step 4: The MCP356x driver now expects a microsecond timeout budget; pass through directly.
  const uint32_t timeout_us_forward = timeout_us;
  g_last_channel_requested          = channel;

  // Step 5: Delegate the conversion to the MCP356x driver and surface its error codes.
  const int return_code = mcp356x_read_single_ended_channel(channel_index, timeout_us_forward, code_out);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  return ADC_HAL_OK;
}

int adc_hal_read_channel_irq(AdcHalChannel channel, uint32_t timeout_us, int32_t* code_out) {
  // Step 1: Guard against null result storage.
  GUARD_NONNULL(code_out);

  // Step 2: Require driver initialisation before arming hardware.
  GUARD_INITIALIZED(g_state.initialized);

  // Step 3: Validate the channel selection against the hardware's supported range.
  const uint8_t channel_index = static_cast<uint8_t>(channel);
  if (channel_index > static_cast<uint8_t>(AdcHalChannel::ADC_HAL_CHANNEL_7)) {
    return ADC_HAL_ERR_INVALID_ARG;
  }

  g_last_channel_requested = channel;

  // Step 4: Point the channel mux toward the requested single-ended input.
  int return_code = mcp356x_select_single_ended_channel(channel_index);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  // Step 5: Clear any stale DRDY condition so the next edge corresponds to this conversion.
  // Might not be strictly necessary since starting new conversion should reset the IRQ state.
  if (adc_hal_is_irq_asserted()) {
    adc_hal_clear_stale_drdy();
  }

  // Step 6: Launch the conversion so DRDY asserts once fresh data is ready.
  return_code = mcp356x_start_conversion(NULL);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  // Step 7: Busy-wait on the DRDY pin until it asserts or the timeout expires.
  if (!adc_hal_wait_for_irq_asserted(timeout_us)) {
    return ADC_HAL_ERR_TIMEOUT;
  }

  // Step 8: Pull the conversion result either from a staged test sample or from the ADC.
  int32_t sample_code = 0;
  uint8_t status_byte = 0u;
  if (g_has_staged_irq_sample) {
    sample_code             = g_staged_irq_sample;
    status_byte             = g_staged_irq_status;
    g_has_staged_irq_sample = false;
  }
  else {
    GUARD(adc_hal_read_sample_via_driver(&sample_code, &status_byte));
  }

  *code_out = sample_code;
  (void) status_byte;  // Keep compiler quiet
  return ADC_HAL_OK;
}

int adc_hal_enter_standby(void) {
  // Step 1: Ensure the HAL is initialised before forwarding power commands.
  GUARD_INITIALIZED(g_state.initialized);

  // Step 2: Ask the MCP356x driver to enter standby and translate failures.
  const int return_code = mcp356x_enter_standby(NULL);
  if (return_code != MCP356X_OK) {
    return ADC_HAL_ERR_BACKEND_FAILURE;
  }

  return ADC_HAL_OK;
}

int adc_hal_shutdown(void) {
  // Step 1: Require an active driver before issuing shutdown.
  GUARD_INITIALIZED(g_state.initialized);

  // Step 2: Forward the shutdown to the MCP356x backend and propagate failures.
  const int return_code = mcp356x_enter_full_shutdown(NULL);
  if (return_code != MCP356X_OK) {
    return ADC_HAL_ERR_BACKEND_FAILURE;
  }

  // Step 3: Reset cached state so future initialisations start from a blank slate.
  g_state.initialized         = false;
  g_state.defaults_programmed = false;

  return ADC_HAL_OK;
}

void adc_hal_reset_for_test(void) {
  // Step 1: Reset runtime state so tests can simulate fresh initialisation.
  g_state.initialized         = false;
  g_state.defaults_programmed = false;
  g_state.irq_pin             = -1;

  // Step 2: Clear test-observable counters to ensure deterministic assertions.
  g_default_config_call_count = 0u;
  g_last_channel_requested    = AdcHalChannel::ADC_HAL_CHANNEL_0;

  // Step 3: Force the MCP356x backend into an uninitialised state for the next test case.
  mcp356x_force_uninitialized_for_test();

  // Step 4: Reset IRQ scaffolding used by the interrupt-focused tests.
  adc_hal_test_reset_irq_state();
}

uint32_t adc_hal_test_default_config_call_count(void) {
  return g_default_config_call_count;
}

AdcHalChannel adc_hal_test_last_channel_requested(void) {
  return g_last_channel_requested;
}

void adc_hal_test_set_irq_wait_hook(adc_hal_irq_wait_hook_t hook) {
  g_irq_wait_hook = hook;
}

void adc_hal_test_stage_irq_sample(int32_t sample_code, uint8_t status_byte) {
  g_staged_irq_sample     = sample_code;
  g_staged_irq_status     = status_byte;
  g_has_staged_irq_sample = true;
}

void adc_hal_test_set_irq_pin_reader(adc_hal_irq_pin_reader_t reader) {
  g_irq_pin_reader = reader;
}

void adc_hal_test_reset_irq_state(void) {
  g_irq_wait_hook         = NULL;
  g_irq_pin_reader        = NULL;
  g_staged_irq_sample     = 0;
  g_staged_irq_status     = 0u;
  g_has_staged_irq_sample = false;
}

// Helper: read and discard any pending payload so the next DRDY edge reflects a fresh conversion.
static void adc_hal_clear_stale_drdy(void) {
  // Step 1: Determine how many bytes to read based on the cached data format.
  const uint8_t payload_size = adc_hal_get_cached_payload_length();
  if (payload_size == 0u) {
    return;
  }

  uint8_t discard_bytes[4] = {0u};
  uint8_t status_byte      = 0u;
  // Step 2: Ignore the result; the read solely clears DRDY so the next edge denotes fresh data.
  (void) mcp356x_read_register(MCP356X_REG_ADCDATA, discard_bytes, payload_size, &status_byte);
}

// Helper: busy-wait on the DRDY pin (or its test double) until it asserts or the timeout expires.
static bool adc_hal_wait_for_irq_asserted(uint32_t timeout_us) {
  // Step 1: Enforce the historical behaviour where a zero timeout budgets results in an immediate timeout.
  if (timeout_us == 0u) {
    return false;
  }

  const uint32_t start_us = micros();

  while (true) {
    if (adc_hal_is_irq_asserted()) {
      return true;
    }

    if (g_irq_wait_hook != NULL) {
      g_irq_wait_hook();
    }

    const uint32_t elapsed_us = micros() - start_us;
    if (elapsed_us >= timeout_us) {
      return false;
    }
  }
}

// Helper: normalize DRDY sensing across hardware GPIO and the injected test pin reader.
static bool adc_hal_is_irq_asserted(void) {
  // Step 1: Bail out when initialise never latched a valid DRDY pin (common in tests).
  if (g_state.irq_pin < 0) {
    return false;
  }

  if (g_irq_pin_reader != NULL) {
    return g_irq_pin_reader();
  }

  return digitalRead(g_state.irq_pin) == LOW;
}

// Helper: convert the cached MCP356x data format into the number of raw payload bytes to read.
static uint8_t adc_hal_payload_length_from_format(mcp356x_data_format format) {
  switch (format) {
    case mcp356x_data_format::data24:
      return 3u;
    case mcp356x_data_format::data32_left:
    case mcp356x_data_format::data32_signed:
    case mcp356x_data_format::data32_signed_chid:
      return 4u;
    default:
      return 0u;
  }
}

// Helper: cache the payload length so repeated calls avoid re-running the format map.
static uint8_t adc_hal_get_cached_payload_length(void) {
  const mcp356x_data_format format = mcp356x_get_cached_data_format();
  if (!g_payload_length_valid || (format != g_cached_data_format)) {
    g_cached_data_format    = format;
    g_cached_payload_length = adc_hal_payload_length_from_format(format);
    g_payload_length_valid  = true;
  }

  return g_cached_payload_length;
}

// Helper: translate the MCP356x wire-format payload into a signed 32-bit value.
static int adc_hal_decode_sample(const uint8_t* data_bytes, mcp356x_data_format format, int32_t* sample_out) {
  GUARD_NONNULL(data_bytes);
  GUARD_NONNULL(sample_out);

  // Step 1: Decode the raw ADC payload into a signed 32-bit value that matches the requested format.
  int32_t decoded_value = 0;

  switch (format) {
    case mcp356x_data_format::data24: {
      decoded_value = (int32_t)((data_bytes[0] << 16) | (data_bytes[1] << 8) | data_bytes[2]);
      if ((decoded_value & 0x00800000L) != 0) {
        decoded_value |= 0xFF000000L;
      }
      break;
    }
    case mcp356x_data_format::data32_left: {
      const int32_t raw_word = (int32_t)(((uint32_t) data_bytes[0] << 24) | ((uint32_t) data_bytes[1] << 16) |
                                         ((uint32_t) data_bytes[2] << 8) | data_bytes[3]);
      decoded_value          = raw_word >> 8;
      break;
    }
    case mcp356x_data_format::data32_signed:
    case mcp356x_data_format::data32_signed_chid: {
      decoded_value = (int32_t)(((uint32_t) data_bytes[0] << 24) | ((uint32_t) data_bytes[1] << 16) |
                                ((uint32_t) data_bytes[2] << 8) | data_bytes[3]);
      break;
    }
    default:
      return MCP356X_ERR_UNSUPPORTED;
  }

  *sample_out = decoded_value;
  return MCP356X_OK;
}

// Helper: request the ADC data register through the driver and convert it into a signed sample.
static int adc_hal_read_sample_via_driver(int32_t* sample_out, uint8_t* status_out) {
  GUARD_NONNULL(sample_out);

  // Step 1: Size the read operation using the cached data format.
  const mcp356x_data_format format       = mcp356x_get_cached_data_format();
  const uint8_t             payload_size = adc_hal_get_cached_payload_length();
  if (payload_size == 0u) {
    return MCP356X_ERR_UNSUPPORTED;
  }

  // Step 2: Pull the raw data from the ADC and decode it locally.
  uint8_t data_bytes[4] = {0u};
  uint8_t status_byte   = 0u;
  int     return_code   = mcp356x_read_register(MCP356X_REG_ADCDATA, data_bytes, payload_size, &status_byte);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  return_code = adc_hal_decode_sample(data_bytes, format, sample_out);
  if ((return_code == MCP356X_OK) && (status_out != NULL)) {
    *status_out = status_byte;
  }

  return return_code;
}
