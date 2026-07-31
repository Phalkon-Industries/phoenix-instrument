#ifndef THERMISTOR_READER_HPP
#define THERMISTOR_READER_HPP

#include "adc_hal.hpp"
#include "phoenix_guard.hpp"
#include <stdint.h>

#define THERMISTOR_READER_OK PHX_OK
#define THERMISTOR_READER_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define THERMISTOR_READER_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define THERMISTOR_READER_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED
#define THERMISTOR_READER_ERR_COMPUTE_FAILURE PHX_ERR_HARDWARE_FAILURE

enum class ThermistorId : uint8_t {
  THERMISTOR_ID_SAMPLE = 0u,      // ch6 - experimental sample thermistor (Steinhart-Hart)
  THERMISTOR_ID_BLUE_LED,         // ch4 - blue LED thermistor (Steinhart-Hart)
  THERMISTOR_ID_GREEN_LED,        // ch5 - green LED thermistor (Steinhart-Hart)
  THERMISTOR_ID_GAIN_STAGE,       // ch2 - gain-stage thermistor (Beta)
  THERMISTOR_ID_LED_DRIVE_STAGE,  // ch3 - LED-drive-stage thermistor (Beta)
};

enum class ThermistorModel : uint8_t {
  THERMISTOR_MODEL_BETA = 0u,
  THERMISTOR_MODEL_STEINHART_HART,
};

struct ThermistorSensorConfig {
  AdcHalChannel   channel;
  ThermistorModel model;
  float           r25_ohms;
  float           calibration_offset_c;
};

struct ThermistorReaderConfig {
  AdcHalChannel          reference_channel;
  ThermistorSensorConfig sensors[5];  // One entry per ThermistorId
  int                    rail_enable_pin;
  uint32_t               pullup_resistance_ohms;
  uint32_t               reference_resistance_ohms;
  uint32_t               adc_timeout_us;
  uint32_t               settle_time_us;
  float                  beta_constant;  // Shared beta constant for all Beta-model sensors
};

struct ThermistorSweepResult {
  float   temperatures_c[5];  // One per ThermistorId
  int32_t reference_code;     // Raw reference divider code for drift tracking
  bool    valid[5];           // Per-sensor validity flags
};

typedef void (*thermistor_reader_pin_mode_fn_t)(int pin, uint32_t mode);
typedef void (*thermistor_reader_digital_write_fn_t)(int pin, uint32_t value);
typedef void (*thermistor_reader_delay_us_fn_t)(uint32_t delay_us);
typedef int (*thermistor_reader_adc_reader_fn_t)(AdcHalChannel channel, uint32_t timeout_us, int32_t* code_out);

int thermistor_reader_initialize(const ThermistorReaderConfig* config);

int thermistor_reader_measure_celsius(ThermistorId id, float* temperature_c_out);

int thermistor_reader_measure_all(ThermistorSweepResult* result_out);

void thermistor_reader_reset_for_test(void);

void thermistor_reader_get_config_for_test(ThermistorReaderConfig* config_out);

void thermistor_reader_set_gpio_hooks_for_test(thermistor_reader_pin_mode_fn_t      pin_mode,
                                               thermistor_reader_digital_write_fn_t digital_write,
                                               thermistor_reader_delay_us_fn_t      delay_us);

void thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_reader_fn_t reader);

int thermistor_reader_get_last_resistance_for_test(ThermistorId id, float* resistance_out);

#endif  // THERMISTOR_READER_HPP
