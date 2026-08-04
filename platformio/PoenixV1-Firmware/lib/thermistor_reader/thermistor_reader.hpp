#ifndef THERMISTOR_READER_HPP
#define THERMISTOR_READER_HPP

#include "adc_hal.hpp"
#include "phoenix_guard.hpp"
#include <stdint.h>

// Return codes shared across the thermistor reader module.
#define THERMISTOR_READER_OK PHX_OK
#define THERMISTOR_READER_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define THERMISTOR_READER_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define THERMISTOR_READER_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED
#define THERMISTOR_READER_ERR_COMPUTE_FAILURE PHX_ERR_HARDWARE_FAILURE

/**
 * @brief Identifies each thermistor on the Tornado board.
 *
 * Enum values map directly to indices in the sensor/config arrays.
 */
enum class ThermistorId : uint8_t {
  THERMISTOR_ID_SAMPLE = 0u,     /**< ch6 — sample thermistor (Steinhart-Hart model). */
  THERMISTOR_ID_BLUE_LED,        /**< ch4 — blue LED thermistor (Steinhart-Hart model). */
  THERMISTOR_ID_GREEN_LED,       /**< ch5 — green LED thermistor (Steinhart-Hart model). */
  THERMISTOR_ID_GAIN_STAGE,      /**< ch2 — gain-stage thermistor (Beta model). */
  THERMISTOR_ID_LED_DRIVE_STAGE, /**< ch3 — LED-drive-stage thermistor (Beta model). */
};

/**
 * @brief Temperature-conversion model assigned to a thermistor.
 */
enum class ThermistorModel : uint8_t {
  THERMISTOR_MODEL_BETA = 0u,      /**< Beta-parameter equation with a shared beta_constant. */
  THERMISTOR_MODEL_STEINHART_HART, /**< Segmented Steinhart-Hart equation using datasheet coefficients. */
};

/**
 * @brief Per-sensor configuration describing one thermistor channel.
 */
struct ThermistorSensorConfig {
  AdcHalChannel   channel;              /**< ADC channel wired to this thermistor's divider. */
  ThermistorModel model;                /**< Temperature-conversion model to apply. */
  float           r25_ohms;             /**< Nominal resistance at 25 °C, from the thermistor datasheet. */
  float           calibration_offset_c; /**< Optional offset added to the computed temperature in °C. */
};

/**
 * @brief Top-level configuration for the thermistor reader module.
 */
struct ThermistorReaderConfig {
  AdcHalChannel          reference_channel;         /**< ADC channel for the 10k/10k reference divider. */
  ThermistorSensorConfig sensors[5];                /**< One entry per ThermistorId. */
  int                    rail_enable_pin;           /**< GPIO that energises the thermistor rail; pass -1 to disable. */
  uint32_t               pullup_resistance_ohms;    /**< Common pull-up resistor shared by all dividers. */
  uint32_t               reference_resistance_ohms; /**< Lower-leg resistor in the reference divider. */
  uint32_t               adc_timeout_us;            /**< Maximum time to wait for each ADC conversion. */
  uint32_t               settle_time_us;            /**< Delay after enabling the rail before sampling begins. */
  float                  beta_constant;             /**< Shared Beta constant for all Beta-model sensors. */
};

/**
 * @brief Result of a full sweep of all five thermistors.
 */
struct ThermistorSweepResult {
  float   temperatures_c[5]; /**< Computed temperatures, one per ThermistorId. */
  int32_t reference_code;    /**< Raw ADC code from the reference divider for rail-drift tracking. */
  bool    valid[5];          /**< Per-sensor validity flag; false when sampling or conversion failed. */
};

/** @brief Hook signature for configuring a GPIO pin mode. */
typedef void (*thermistor_reader_pin_mode_fn_t)(int pin, uint32_t mode);

/** @brief Hook signature for driving a GPIO pin high or low. */
typedef void (*thermistor_reader_digital_write_fn_t)(int pin, uint32_t value);

/** @brief Hook signature for microsecond-precision busy-wait delays. */
typedef void (*thermistor_reader_delay_us_fn_t)(uint32_t delay_us);

/**
 * @brief Hook signature for reading an ADC channel.
 *
 * Must populate *code_out with the raw conversion code.
 *
 * @return ADC_HAL_OK on success, or a propagated error code.
 */
typedef int (*thermistor_reader_adc_reader_fn_t)(AdcHalChannel channel, uint32_t timeout_us, int32_t* code_out);

/**
 * @brief Initialise the thermistor reader with board-specific configuration.
 *
 * Copies the caller's config, installs production GPIO / ADC hooks, and
 * sets the rail-enable pin to a known-off state when configured.
 *
 * @param config Pointer to the board-level ThermistorReaderConfig.
 * @return THERMISTOR_READER_OK on success, THERMISTOR_READER_ERR_INVALID_ARG when config is NULL.
 */
int thermistor_reader_initialize(const ThermistorReaderConfig* config);

/**
 * @brief Measure a single thermistor and return the temperature in °C.
 *
 * Energises the thermistor rail (if configured), samples the reference
 * divider followed by the target sensor, computes resistance, converts
 * it to temperature using the sensor's configured model, and depowers
 * the rail before returning.
 *
 * @param id Which thermistor to measure.
 * @param temperature_c_out Destination for the computed temperature in °C.
 * @return THERMISTOR_READER_OK on success, or a negative error code on failure.
 */
int thermistor_reader_measure_celsius(ThermistorId id, float* temperature_c_out);

/**
 * @brief Sweep all five thermistors in a single rail pulse and return every result.
 *
 * The reference divider is sampled once; each sensor code is converted
 * against that shared reference.  Sensors that fail sampling or
 * conversion are marked invalid individually without aborting the sweep.
 *
 * @param result_out Destination ThermistorSweepResult to populate.
 * @return THERMISTOR_READER_OK when the sweep completes (individual validity
 *         is in result_out->valid[]), or a propagated error code when the
 *         reference sample itself fails.
 */
int thermistor_reader_measure_all(ThermistorSweepResult* result_out);

/** @brief Reset internal module state to defaults for unit testing. */
void thermistor_reader_reset_for_test(void);

/** @brief Retrieve the currently cached configuration for test inspection. */
void thermistor_reader_get_config_for_test(ThermistorReaderConfig* config_out);

/**
 * @brief Replace GPIO hooks with test-controlled stubs.
 *
 * Each non-NULL parameter replaces the corresponding production default.
 */
void thermistor_reader_set_gpio_hooks_for_test(thermistor_reader_pin_mode_fn_t      pin_mode,
                                               thermistor_reader_digital_write_fn_t digital_write,
                                               thermistor_reader_delay_us_fn_t      delay_us);

/** @brief Replace the ADC reader hook with a test-controlled stub. */
void thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_reader_fn_t reader);

/**
 * @brief Return the last computed resistance for a given thermistor.
 *
 * @param id Which thermistor to query.
 * @param resistance_out Destination for the resistance in ohms.
 * @return THERMISTOR_READER_OK on success, or a negative error code.
 */
int thermistor_reader_get_last_resistance_for_test(ThermistorId id, float* resistance_out);

#endif  // THERMISTOR_READER_HPP
