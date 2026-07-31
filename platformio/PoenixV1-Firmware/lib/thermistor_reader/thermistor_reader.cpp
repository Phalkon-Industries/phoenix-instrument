#include "thermistor_reader.hpp"

#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace {
constexpr size_t kThermistorCount = 5u;

void default_pin_mode(int pin, uint32_t mode) {
  ::pinMode(static_cast<uint32_t>(pin), mode);
}

void default_digital_write(int pin, uint32_t value) {
  ::digitalWrite(static_cast<uint32_t>(pin), value);
}

void default_delay_us(uint32_t delay_us) {
  ::delayMicroseconds(delay_us);
}

int default_adc_reader(AdcHalChannel channel, uint32_t timeout_us, int32_t* code_out) {
  return adc_hal_read_single_ended(channel, timeout_us, code_out);
}

struct ThermistorReaderState {
  bool                                 initialized;
  ThermistorReaderConfig               config;
  thermistor_reader_pin_mode_fn_t      pin_mode;
  thermistor_reader_digital_write_fn_t digital_write;
  thermistor_reader_delay_us_fn_t      delay_us;
  thermistor_reader_adc_reader_fn_t    adc_reader;
  float                                last_resistance_ohms[kThermistorCount];
  bool                                 last_resistance_valid[kThermistorCount];
};

ThermistorReaderState g_state = {};

void thermistor_reader_apply_default_hooks(void) {
  g_state.pin_mode      = default_pin_mode;
  g_state.digital_write = default_digital_write;
  g_state.delay_us      = default_delay_us;
  g_state.adc_reader    = default_adc_reader;
}

struct SteinhartHartSegment {
  float ratio_min_inclusive;
  float coefficient_a;
  float coefficient_b;
  float coefficient_c;
  float coefficient_d;
};

constexpr SteinhartHartSegment kWaterThermistorSegments[] = {
    {3.274f, 3.3538646e-03f, 2.5654090e-04f, 1.9243889e-06f, 1.0969244e-07f},
    {0.36036f, 3.3540154e-03f, 2.5627725e-04f, 2.0829210e-06f, 7.3003206e-08f},
    {0.06831f, 3.3539264e-03f, 2.5609446e-04f, 1.9621987e-06f, 4.6045930e-08f},
    {0.0f, 3.3368620e-03f, 2.4057263e-04f, -2.6687093e-06f, -4.0719355e-07f},
};

size_t thermistor_reader_index_for_id(ThermistorId id) {
  return static_cast<size_t>(id);
}

AdcHalChannel thermistor_reader_channel_for_id(ThermistorId id) {
  const size_t sensor_index = thermistor_reader_index_for_id(id);
  if (sensor_index >= kThermistorCount) {
    return g_state.config.reference_channel;
  }
  return g_state.config.sensors[sensor_index].channel;
}

// Step 1: Capture ADC samples so the library can convert dividers into resistances.
int thermistor_reader_sample_channel(AdcHalChannel channel, int32_t* code_out) {
  return g_state.adc_reader(channel, g_state.config.adc_timeout_us, code_out);
}

// Step 2: Convert measured codes to thermistor resistance using the reference divider as a voltage proxy.
int thermistor_reader_compute_resistance(int32_t reference_code, int32_t sensor_code, float* resistance_out) {
  if ((reference_code == 0) || (resistance_out == NULL)) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  const float reference_ratio = static_cast<float>(sensor_code) / static_cast<float>(reference_code);
  if (reference_ratio <= 0.0f) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }
  const float pullup_ohms    = static_cast<float>(g_state.config.pullup_resistance_ohms);
  const float reference_ohms = static_cast<float>(g_state.config.reference_resistance_ohms);
  const float divider_factor = (pullup_ohms + reference_ohms) / reference_ohms;
  const float denominator    = divider_factor - reference_ratio;

  if (denominator <= 0.0f) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  *resistance_out = (reference_ratio * pullup_ohms) / denominator;
  if (*resistance_out <= 0.0f) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  return THERMISTOR_READER_OK;
}

// Step 3: Apply the Beta equation to sensors using the Beta model so callers receive calibrated °C values.
int thermistor_reader_convert_beta_temperature(float resistance_ohms, float r25_ohms, float calibration_offset_c,
                                               float* temperature_c_out) {
  if ((temperature_c_out == NULL) || (resistance_ohms <= 0.0f)) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  const float beta_constant = g_state.config.beta_constant;
  if ((beta_constant <= 0.0f) || (r25_ohms <= 0.0f)) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  const float nominal_temp_kelvin = 298.15f;  // 25 °C reference point specified in the datasheet.
  const float ratio               = resistance_ohms / r25_ohms;
  if (ratio <= 0.0f) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  const float inverse_temperature = (1.0f / nominal_temp_kelvin) + (1.0f / beta_constant) * logf(ratio);
  if (inverse_temperature <= 0.0f) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  const float temperature_kelvin = 1.0f / inverse_temperature;
  const float temperature_c      = temperature_kelvin - 273.15f + calibration_offset_c;
  *temperature_c_out             = temperature_c;
  return THERMISTOR_READER_OK;
}

// Step 4: Convert sensors using the Steinhart-Hart model with datasheet coefficients for each ratio segment.
int thermistor_reader_convert_steinhart_hart_temperature(float resistance_ohms, float r25_ohms,
                                                         float calibration_offset_c, float* temperature_c_out) {
  if ((temperature_c_out == NULL) || (resistance_ohms <= 0.0f)) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  if (r25_ohms <= 0.0f) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  const float ratio = resistance_ohms / r25_ohms;
  if (ratio <= 0.0f) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  const SteinhartHartSegment* matched_segment = NULL;
  for (size_t i = 0u; i < (sizeof(kWaterThermistorSegments) / sizeof(kWaterThermistorSegments[0])); ++i) {
    if (ratio >= kWaterThermistorSegments[i].ratio_min_inclusive) {
      matched_segment = &kWaterThermistorSegments[i];
      break;
    }
  }

  if (matched_segment == NULL) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  const float ln_ratio    = logf(ratio);
  const float ln_ratio_sq = ln_ratio * ln_ratio;
  const float inverse_t   = matched_segment->coefficient_a + matched_segment->coefficient_b * ln_ratio +
                            matched_segment->coefficient_c * ln_ratio_sq +
                            matched_segment->coefficient_d * ln_ratio_sq * ln_ratio;
  if (inverse_t <= 0.0f) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  const float temperature_kelvin = 1.0f / inverse_t;
  const float temperature_c      = temperature_kelvin - 273.15f + calibration_offset_c;
  *temperature_c_out             = temperature_c;
  return THERMISTOR_READER_OK;
}

// Step 5: Pulse the thermistor rail so dividers only energize during measurement windows.
void thermistor_reader_set_rail_state(bool enable) {
  if (g_state.config.rail_enable_pin < 0) {
    return;
  }

  const uint32_t level = enable ? HIGH : LOW;
  g_state.digital_write(g_state.config.rail_enable_pin, level);
  if (enable && (g_state.config.settle_time_us > 0u)) {
    g_state.delay_us(g_state.config.settle_time_us);
  }
}
}  // namespace

int thermistor_reader_initialize(const ThermistorReaderConfig* config) {
  if (config == NULL) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }

  g_state.config = *config;
  thermistor_reader_apply_default_hooks();
  g_state.initialized = true;
  memset(g_state.last_resistance_ohms, 0, sizeof(g_state.last_resistance_ohms));
  memset(g_state.last_resistance_valid, 0, sizeof(g_state.last_resistance_valid));

  if (g_state.config.rail_enable_pin >= 0) {
    g_state.pin_mode(g_state.config.rail_enable_pin, OUTPUT);
    g_state.digital_write(g_state.config.rail_enable_pin, LOW);
  }

  return THERMISTOR_READER_OK;
}

int thermistor_reader_measure_celsius(ThermistorId id, float* temperature_c_out) {
  if (temperature_c_out == NULL) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }

  *temperature_c_out = 0.0f;

  if (!g_state.initialized) {
    return THERMISTOR_READER_ERR_NOT_INITIALIZED;
  }

  int                 result               = THERMISTOR_READER_OK;
  const bool          rail_control_enabled = (g_state.config.rail_enable_pin >= 0);
  int32_t             reference_code       = 0;
  int32_t             sensor_code          = 0;
  float               resistance_ohms      = 0.0f;
  const size_t        resistance_index     = thermistor_reader_index_for_id(id);
  const AdcHalChannel target_channel       = thermistor_reader_channel_for_id(id);

  if (rail_control_enabled) {
    thermistor_reader_set_rail_state(true);
  }

  result = thermistor_reader_sample_channel(g_state.config.reference_channel, &reference_code);
  if (result == THERMISTOR_READER_OK) {
    result = thermistor_reader_sample_channel(target_channel, &sensor_code);
  }

  if (result == THERMISTOR_READER_OK) {
    result = thermistor_reader_compute_resistance(reference_code, sensor_code, &resistance_ohms);
  }

  if (result == THERMISTOR_READER_OK) {
    if (resistance_index < kThermistorCount) {
      g_state.last_resistance_ohms[resistance_index]  = resistance_ohms;
      g_state.last_resistance_valid[resistance_index] = true;
    }

    if (resistance_index >= kThermistorCount) {
      result = THERMISTOR_READER_ERR_INVALID_ARG;
    }
    else {
      const ThermistorSensorConfig& sensor_config = g_state.config.sensors[resistance_index];
      switch (sensor_config.model) {
        case ThermistorModel::THERMISTOR_MODEL_BETA:
          result = thermistor_reader_convert_beta_temperature(resistance_ohms, sensor_config.r25_ohms,
                                                              sensor_config.calibration_offset_c, temperature_c_out);
          break;
        case ThermistorModel::THERMISTOR_MODEL_STEINHART_HART:
          result = thermistor_reader_convert_steinhart_hart_temperature(
              resistance_ohms, sensor_config.r25_ohms, sensor_config.calibration_offset_c, temperature_c_out);
          break;
        default:
          result = THERMISTOR_READER_ERR_INVALID_ARG;
          break;
      }
    }
  }

  if (rail_control_enabled) {
    thermistor_reader_set_rail_state(false);
  }

  return result;
}

void thermistor_reader_reset_for_test(void) {
  memset(&g_state, 0, sizeof(g_state));
  thermistor_reader_apply_default_hooks();
}

void thermistor_reader_get_config_for_test(ThermistorReaderConfig* config_out) {
  if (config_out == NULL) {
    return;
  }

  *config_out = g_state.config;
}

void thermistor_reader_set_gpio_hooks_for_test(thermistor_reader_pin_mode_fn_t      pin_mode,
                                               thermistor_reader_digital_write_fn_t digital_write,
                                               thermistor_reader_delay_us_fn_t      delay_us) {
  if (pin_mode != NULL) {
    g_state.pin_mode = pin_mode;
  }

  if (digital_write != NULL) {
    g_state.digital_write = digital_write;
  }

  if (delay_us != NULL) {
    g_state.delay_us = delay_us;
  }
}

void thermistor_reader_set_adc_reader_for_test(thermistor_reader_adc_reader_fn_t reader) {
  if (reader != NULL) {
    g_state.adc_reader = reader;
  }
}

int thermistor_reader_get_last_resistance_for_test(ThermistorId id, float* resistance_out) {
  if (resistance_out == NULL) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }

  const size_t resistance_index = thermistor_reader_index_for_id(id);
  if (resistance_index >= kThermistorCount) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }

  if (!g_state.last_resistance_valid[resistance_index]) {
    *resistance_out = 0.0f;
    return THERMISTOR_READER_ERR_NOT_INITIALIZED;
  }

  *resistance_out = g_state.last_resistance_ohms[resistance_index];
  return THERMISTOR_READER_OK;
}

int thermistor_reader_measure_all(ThermistorSweepResult* result_out) {
  if (result_out == NULL) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }

  memset(result_out, 0, sizeof(ThermistorSweepResult));

  if (!g_state.initialized) {
    return THERMISTOR_READER_ERR_NOT_INITIALIZED;
  }

  int        result               = THERMISTOR_READER_OK;
  const bool rail_control_enabled = (g_state.config.rail_enable_pin >= 0);

  // Step 1: Enable the thermistor rail once for the entire sweep
  if (rail_control_enabled) {
    thermistor_reader_set_rail_state(true);
  }

  // Step 2: Sample the reference divider once for all conversions
  int32_t reference_code = 0;
  result                 = thermistor_reader_sample_channel(g_state.config.reference_channel, &reference_code);
  if (result != THERMISTOR_READER_OK) {
    if (rail_control_enabled) {
      thermistor_reader_set_rail_state(false);
    }
    return result;
  }
  result_out->reference_code = reference_code;

  // Step 3: Sample sensors in order - sample thermistor first (ch6), then others
  // This ordering minimizes self-heating effects on the sample thermistor
  const ThermistorId sensor_order[kThermistorCount] = {
      ThermistorId::THERMISTOR_ID_SAMPLE, ThermistorId::THERMISTOR_ID_BLUE_LED, ThermistorId::THERMISTOR_ID_GREEN_LED,
      ThermistorId::THERMISTOR_ID_GAIN_STAGE, ThermistorId::THERMISTOR_ID_LED_DRIVE_STAGE};

  for (size_t i = 0u; i < kThermistorCount; ++i) {
    const ThermistorId  id             = sensor_order[i];
    const size_t        sensor_index   = thermistor_reader_index_for_id(id);
    const AdcHalChannel target_channel = thermistor_reader_channel_for_id(id);

    int32_t sensor_code = 0;
    result              = thermistor_reader_sample_channel(target_channel, &sensor_code);
    if (result != THERMISTOR_READER_OK) {
      result_out->valid[sensor_index] = false;
      continue;
    }

    float resistance_ohms = 0.0f;
    result                = thermistor_reader_compute_resistance(reference_code, sensor_code, &resistance_ohms);
    if (result != THERMISTOR_READER_OK) {
      result_out->valid[sensor_index] = false;
      continue;
    }

    g_state.last_resistance_ohms[sensor_index]  = resistance_ohms;
    g_state.last_resistance_valid[sensor_index] = true;

    const ThermistorSensorConfig& sensor_config = g_state.config.sensors[sensor_index];
    float                         temperature_c = 0.0f;

    switch (sensor_config.model) {
      case ThermistorModel::THERMISTOR_MODEL_BETA:
        result = thermistor_reader_convert_beta_temperature(resistance_ohms, sensor_config.r25_ohms,
                                                            sensor_config.calibration_offset_c, &temperature_c);
        break;
      case ThermistorModel::THERMISTOR_MODEL_STEINHART_HART:
        result = thermistor_reader_convert_steinhart_hart_temperature(
            resistance_ohms, sensor_config.r25_ohms, sensor_config.calibration_offset_c, &temperature_c);
        break;
      default:
        result = THERMISTOR_READER_ERR_INVALID_ARG;
        break;
    }

    if (result == THERMISTOR_READER_OK) {
      result_out->temperatures_c[sensor_index] = temperature_c;
      result_out->valid[sensor_index]          = true;
    }
    else {
      result_out->valid[sensor_index] = false;
    }
  }

  // Step 4: Disable the thermistor rail after the sweep
  if (rail_control_enabled) {
    thermistor_reader_set_rail_state(false);
  }

  return THERMISTOR_READER_OK;
}
