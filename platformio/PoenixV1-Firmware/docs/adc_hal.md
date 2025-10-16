# ADC HAL Usage Cheat Sheet

The ADC hardware abstraction layer (`adc_hal`) wraps the MCP356x-series driver so
application code stays free of device-specific helpers. The key workflow is:

1. Configure the adapter using `AdcHalConfig`.
2. Call `adc_hal_initialize()` with that configuration.
3. Program the default register image through `adc_hal_apply_default_configuration()`.
4. Sample channels or enter standby via the high-level helpers.
5. Shut the adapter down during teardown so subsequent initialisation starts cleanly.

## Quickstart

```cpp
#include "adc_hal.hpp"

static const AdcHalConfig k_adc_config = {
    .chip_select_pin = PIN_ADC_CS,
  .spi_clock_hz    = 500000UL,
  .irq_pin         = PIN_ADC_IRQ,
};

void example(void) {
  if (adc_hal_initialize(&k_adc_config) != ADC_HAL_OK) {
    // Handle error
    return;
  }

  if (adc_hal_apply_default_configuration() != ADC_HAL_OK) {
    // Handle error
    return;
  }

  int32_t code = 0;
  if (adc_hal_read_single_ended(AdcHalChannel::ADC_HAL_CHANNEL_4, 1000000u, &code) == ADC_HAL_OK) {
    // Use conversion result
  }

  if (adc_hal_read_channel_irq(AdcHalChannel::ADC_HAL_CHANNEL_4, 250000u, &code) == ADC_HAL_OK) {
    // Use interrupt-synchronised result
  }

  (void) adc_hal_enter_standby();
  (void) adc_hal_shutdown();
}
```

## Test Instrumentation

The adapter exposes lightweight instrumentation so Unity tests can make strong
assertions without stubbing the MCP356x layer:

- `adc_hal_test_default_config_call_count()` returns how many times the backend
  default configuration helper ran.
- `adc_hal_test_last_channel_requested()` records the most recent single-ended
  channel passed to the backend.

These functions are safe to call in production builds; they simply provide
observability for tests.

## Shutdown Discipline

Call `adc_hal_shutdown()` whenever the ADC should park in a low-power state. The
function forwards the request to the MCP356x driver, clears the cached
initialisation flag, and resets internal instrumentation so tests begin with a
clean slate.
