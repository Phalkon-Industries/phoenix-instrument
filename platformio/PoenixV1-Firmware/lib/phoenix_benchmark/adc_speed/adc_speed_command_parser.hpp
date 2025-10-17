#ifndef PHOENIX_BENCHMARK_ADC_SPEED_COMMAND_PARSER_HPP
#define PHOENIX_BENCHMARK_ADC_SPEED_COMMAND_PARSER_HPP

#include "adc_speed.hpp"
#include <cstddef>
#include <cstdint>

struct PhoenixBenchmarkAdcSpeedParseOutcome {
  bool                            success;
  PhoenixBenchmarkAdcSpeedOptions options;
  const char*                     error_message;
};

constexpr const char k_phoenix_benchmark_adc_speed_error_invalid_command[] = "adc_speed_invalid_command";
constexpr const char k_phoenix_benchmark_adc_speed_error_invalid_value[]   = "adc_speed_invalid_value";

PhoenixBenchmarkAdcSpeedParseOutcome phoenix_benchmark_adc_speed_parse_command_line(const char* line);

#endif  // PHOENIX_BENCHMARK_ADC_SPEED_COMMAND_PARSER_HPP
