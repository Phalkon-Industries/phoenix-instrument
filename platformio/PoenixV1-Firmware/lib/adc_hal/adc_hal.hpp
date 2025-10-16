#ifndef ADC_HAL_HPP
#define ADC_HAL_HPP

#include <stdint.h>

/**
 * @brief Return codes shared by the generic ADC interface.
 */
#define ADC_HAL_OK 0
#define ADC_HAL_ERR_INVALID_ARG -1
#define ADC_HAL_ERR_NOT_INITIALIZED -2
#define ADC_HAL_ERR_BACKEND_FAILURE -3
#define ADC_HAL_ERR_NOT_IMPLEMENTED -4
#define ADC_HAL_ERR_TIMEOUT -5

/**
 * @brief Logical ADC channels supported by the abstraction.
 *
 * Additional channels can be mapped by the backend implementation as needed.
 */
enum class AdcHalChannel : uint8_t {
  ADC_HAL_CHANNEL_0 = 0u,
  ADC_HAL_CHANNEL_1,
  ADC_HAL_CHANNEL_2,
  ADC_HAL_CHANNEL_3,
  ADC_HAL_CHANNEL_4,
  ADC_HAL_CHANNEL_5,
  ADC_HAL_CHANNEL_6,
  ADC_HAL_CHANNEL_7,
};

/**
 * @brief Runtime configuration required to initialise the abstraction.
 */
struct AdcHalConfig {
  int      chip_select_pin; /**< Chip select GPIO for the active ADC backend. */
  uint32_t spi_clock_hz;    /**< Desired SPI clock in Hertz. */
  int      irq_pin;         /**< Interrupt pin wired to the ADC's DRDY output. */
};

/**
 * @brief Prepare the active ADC backend for use.
 *
 * @param config Pointer to configuration parameters describing the backend.
 * @return ADC_HAL_OK on success, or a negative error code on failure.
 */
int adc_hal_initialize(const AdcHalConfig* config);

/**
 * @brief Apply the backend's default configuration and cache it for future operations.
 *
 * @return ADC_HAL_OK when defaults are programmed, or a negative error code on failure.
 */
int adc_hal_apply_default_configuration(void);

/**
 * @brief Read a single-ended channel and return the raw conversion code.
 *
 * @param channel Logical channel identifier to sample.
 * @param timeout_us Maximum time to wait for conversion completion, expressed in microseconds.
 * @param code_out Destination pointer that receives the conversion result.
 * @return ADC_HAL_OK on success, ADC_HAL_ERR_INVALID_ARG for null pointers, or backend error codes.
 */
int adc_hal_read_single_ended(AdcHalChannel channel, uint32_t timeout_us, int32_t* code_out);

/**
 * @brief Read a single-ended channel using the interrupt-driven flow.
 */
int adc_hal_read_channel_irq(AdcHalChannel channel, uint32_t timeout_us, int32_t* code_out);

/**
 * @brief Place the backend into a low-power standby state.
 *
 * @return ADC_HAL_OK when the backend acknowledges the command, or a negative error code.
 */
int adc_hal_enter_standby(void);

/**
 * @brief Release resources and mark the abstraction uninitialised.
 *
 * @return ADC_HAL_OK on success, ADC_HAL_ERR_NOT_INITIALIZED when called before initialise.
 */
int adc_hal_shutdown(void);

/**
 * @brief Test hook used to reset internal state between Unity test cases.
 */
void adc_hal_reset_for_test(void);

/**
 * @brief Retrieve the number of times the backend default configuration helper has been invoked.
 */
uint32_t adc_hal_test_default_config_call_count(void);

/**
 * @brief Inspect the last channel passed through to the backend during a single-ended read.
 */
AdcHalChannel adc_hal_test_last_channel_requested(void);

typedef void (*adc_hal_irq_wait_hook_t)(void);

void     adc_hal_test_set_irq_wait_hook(adc_hal_irq_wait_hook_t hook);
void     adc_hal_test_stage_irq_sample(int32_t sample_code, uint8_t status_byte);
void     adc_hal_test_fire_staged_irq(void);
uint32_t adc_hal_test_attach_interrupt_call_count(void);
uint32_t adc_hal_test_detach_interrupt_call_count(void);
bool     adc_hal_test_interrupt_attached(void);
void     adc_hal_test_reset_irq_state(void);

#endif  // ADC_HAL_HPP
