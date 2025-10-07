#include "adc_hal.hpp"

#include "mcp356x.hpp"

struct AdcHalState {
  bool         initialized;
  bool         defaults_programmed;
  mcp356x_gain cached_gain;
  AdcHalConfig cached_config;
};

static AdcHalState g_state = {
    false,
    false,
    mcp356x_gain::gain_x1,
    {0, 0u, AdcHalGain::ADC_HAL_GAIN_1},
};

static mcp356x_gain  adc_hal_convert_gain(AdcHalGain gain);
static uint32_t      adc_hal_timeout_us_to_ms(uint32_t timeout_us);
static uint32_t      g_default_config_call_count = 0u;
static AdcHalGain    g_last_gain_requested       = AdcHalGain::ADC_HAL_GAIN_1;
static AdcHalChannel g_last_channel_requested    = AdcHalChannel::ADC_HAL_CHANNEL_0;

int adc_hal_initialize(const AdcHalConfig* config) {
  if (config == NULL) {
    return ADC_HAL_ERR_INVALID_ARG;
  }

  const int return_code = mcp356x_initialize(config->chip_select_pin, config->spi_clock_hz);
  if (return_code != MCP356X_OK) {
    return ADC_HAL_ERR_BACKEND_FAILURE;
  }

  g_state.initialized         = true;
  g_state.defaults_programmed = false;
  g_state.cached_gain         = adc_hal_convert_gain(config->default_gain);
  g_state.cached_config       = *config;

  return ADC_HAL_OK;
}

int adc_hal_apply_default_configuration(void) {
  if (!g_state.initialized) {
    return ADC_HAL_ERR_NOT_INITIALIZED;
  }

  if (g_state.defaults_programmed) {
    return ADC_HAL_OK;
  }

  const int return_code = mcp356x_apply_default_config_with_gain(g_state.cached_gain);
  if (return_code != MCP356X_OK) {
    return ADC_HAL_ERR_BACKEND_FAILURE;
  }

  g_state.defaults_programmed = true;

  g_default_config_call_count += 1u;
  g_last_gain_requested = g_state.cached_config.default_gain;

  return ADC_HAL_OK;
}

int adc_hal_read_single_ended(AdcHalChannel channel, uint32_t timeout_us, int32_t* code_out) {
  if (code_out == NULL) {
    return ADC_HAL_ERR_INVALID_ARG;
  }

  if (!g_state.initialized) {
    return ADC_HAL_ERR_NOT_INITIALIZED;
  }

  const uint8_t channel_index = static_cast<uint8_t>(channel);
  if (channel_index > static_cast<uint8_t>(AdcHalChannel::ADC_HAL_CHANNEL_7)) {
    return ADC_HAL_ERR_INVALID_ARG;
  }

  const uint32_t timeout_ms = adc_hal_timeout_us_to_ms(timeout_us);
  g_last_channel_requested  = channel;

  const int return_code = mcp356x_read_single_ended_channel(channel_index, timeout_ms, code_out);
  if (return_code != MCP356X_OK) {
    return return_code;
  }

  return ADC_HAL_OK;
}

int adc_hal_enter_standby(void) {
  if (!g_state.initialized) {
    return ADC_HAL_ERR_NOT_INITIALIZED;
  }

  const int return_code = mcp356x_enter_standby(NULL);
  if (return_code != MCP356X_OK) {
    return ADC_HAL_ERR_BACKEND_FAILURE;
  }

  return ADC_HAL_OK;
}

int adc_hal_shutdown(void) {
  if (!g_state.initialized) {
    return ADC_HAL_ERR_NOT_INITIALIZED;
  }

  const int return_code = mcp356x_enter_full_shutdown(NULL);
  if (return_code != MCP356X_OK) {
    return ADC_HAL_ERR_BACKEND_FAILURE;
  }

  g_state.initialized         = false;
  g_state.defaults_programmed = false;
  g_state.cached_gain         = mcp356x_gain::gain_x1;

  return ADC_HAL_OK;
}

void adc_hal_reset_for_test(void) {
  g_state.initialized                   = false;
  g_state.defaults_programmed           = false;
  g_state.cached_gain                   = mcp356x_gain::gain_x1;
  g_state.cached_config.chip_select_pin = 0;
  g_state.cached_config.spi_clock_hz    = 0u;
  g_state.cached_config.default_gain    = AdcHalGain::ADC_HAL_GAIN_1;

  g_default_config_call_count = 0u;
  g_last_gain_requested       = AdcHalGain::ADC_HAL_GAIN_1;
  g_last_channel_requested    = AdcHalChannel::ADC_HAL_CHANNEL_0;

  mcp356x_force_uninitialized_for_test();
}

static mcp356x_gain adc_hal_convert_gain(AdcHalGain gain) {
  switch (gain) {
    case AdcHalGain::ADC_HAL_GAIN_1:
      return mcp356x_gain::gain_x1;
    case AdcHalGain::ADC_HAL_GAIN_2:
      return mcp356x_gain::gain_x2;
    case AdcHalGain::ADC_HAL_GAIN_4:
      return mcp356x_gain::gain_x4;
    case AdcHalGain::ADC_HAL_GAIN_8:
      return mcp356x_gain::gain_x8;
    case AdcHalGain::ADC_HAL_GAIN_16:
      return mcp356x_gain::gain_x16;
    case AdcHalGain::ADC_HAL_GAIN_32:
      return mcp356x_gain::gain_x32;
    case AdcHalGain::ADC_HAL_GAIN_64:
      return mcp356x_gain::gain_x64;
    default:
      return mcp356x_gain::gain_x1;
  }
}

static uint32_t adc_hal_timeout_us_to_ms(uint32_t timeout_us) {
  if (timeout_us == 0u) {
    return 0u;
  }

  const uint32_t remainder  = timeout_us % 1000u;
  uint32_t       timeout_ms = timeout_us / 1000u;
  if (remainder > 0u) {
    timeout_ms += 1u;
  }

  return timeout_ms;
}

uint32_t adc_hal_test_default_config_call_count(void) {
  return g_default_config_call_count;
}

AdcHalChannel adc_hal_test_last_channel_requested(void) {
  return g_last_channel_requested;
}

AdcHalGain adc_hal_test_last_gain_requested(void) {
  return g_last_gain_requested;
}
