#include "device_setup.hpp"
#include "thermistor_reader.hpp"
#include <Arduino.h>

namespace {
constexpr uint32_t k_measurement_interval_ms = 1000u;

const ThermistorReaderConfig k_thermistor_reader_config = {
    AdcHalChannel::ADC_HAL_CHANNEL_7,  // reference (10k/10k divider)
    {
        // THERMISTOR_ID_SAMPLE (ch6) - Steinhart-Hart
        {AdcHalChannel::ADC_HAL_CHANNEL_6, ThermistorModel::THERMISTOR_MODEL_STEINHART_HART, 10000.0f, 0.0f},
        // THERMISTOR_ID_BLUE_LED (ch4) - Steinhart-Hart
        {AdcHalChannel::ADC_HAL_CHANNEL_4, ThermistorModel::THERMISTOR_MODEL_STEINHART_HART, 10000.0f, 0.0f},
        // THERMISTOR_ID_GREEN_LED (ch5) - Steinhart-Hart
        {AdcHalChannel::ADC_HAL_CHANNEL_5, ThermistorModel::THERMISTOR_MODEL_STEINHART_HART, 10000.0f, 0.0f},
        // THERMISTOR_ID_GAIN_STAGE (ch2) - Beta
        {AdcHalChannel::ADC_HAL_CHANNEL_2, ThermistorModel::THERMISTOR_MODEL_BETA, 10000.0f, 0.0f},
        // THERMISTOR_ID_LED_DRIVE_STAGE (ch3) - Beta
        {AdcHalChannel::ADC_HAL_CHANNEL_3, ThermistorModel::THERMISTOR_MODEL_BETA, 10000.0f, 0.0f},
    },
    PIN_THERMISTOR_ON,
    10000u,   // pullup_resistance_ohms
    10000u,   // reference_resistance_ohms
    100000u,  // adc_timeout_us
    2000u,    // settle_time_us
    3380.0f,  // beta_constant (shared for all Beta-model sensors)
};

void halt_with_error(const __FlashStringHelper* label, int return_code) {
  Serial.print(label);
  Serial.print(F(": error="));
  Serial.println(return_code);
  while (true) {
    delay(1000);
  }
}

struct TemperatureSample {
  const __FlashStringHelper* label;
  float                      temperature_c;
  int                        return_code;
};

TemperatureSample capture_temperature(const __FlashStringHelper* label, ThermistorId sensor_id) {
  TemperatureSample sample = {label, 0.0f, THERMISTOR_READER_OK};
  sample.return_code       = thermistor_reader_measure_celsius(sensor_id, &sample.temperature_c);
  return sample;
}

void print_row(const TemperatureSample& board, const TemperatureSample& water) {
  Serial.print(board.label);
  Serial.print('\t');
  if (board.return_code == THERMISTOR_READER_OK) {
    Serial.print(board.temperature_c, 2);
    Serial.print(F(" C"));
  }
  else {
    Serial.print(F("error="));
    Serial.print(board.return_code);
  }

  Serial.print('\t');
  Serial.print(water.label);
  Serial.print('\t');
  if (water.return_code == THERMISTOR_READER_OK) {
    Serial.print(water.temperature_c, 2);
    Serial.println(F(" C"));
  }
  else {
    Serial.print(F("error="));
    Serial.println(water.return_code);
  }
}
}  // namespace

void setup() {
  // Step 1: Start the CDC serial interface so readings appear on the host.
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }
  Serial.println(F("Phoenix Thermistor Reader Example"));

  // Step 2: Prepare shared peripherals (power domains, ADC, etc.).
  int return_code = device_setup_initialize();
  if (return_code != PHX_OK) {
    halt_with_error(F("device_setup_initialize failed"), return_code);
  }

  // Step 3: Configure the thermistor reader so future calls pulse the rail and read temperatures.
  return_code = thermistor_reader_initialize(&k_thermistor_reader_config);
  if (return_code != THERMISTOR_READER_OK) {
    halt_with_error(F("thermistor_reader_initialize failed"), return_code);
  }
}

void loop() {
  // Step 1: Sample thermistors so the printout can display them side-by-side.
  const TemperatureSample gain_stage_sample =
      capture_temperature(F("gain_stage"), ThermistorId::THERMISTOR_ID_GAIN_STAGE);
  const TemperatureSample sample_thermistor = capture_temperature(F("sample"), ThermistorId::THERMISTOR_ID_SAMPLE);

  // Step 2: Emit a tab-delimited row for easier visual comparison in the serial console.
  print_row(gain_stage_sample, sample_thermistor);

  // Step 3: Wait before taking the next measurement to limit self-heating.
  delay(k_measurement_interval_ms);
}
