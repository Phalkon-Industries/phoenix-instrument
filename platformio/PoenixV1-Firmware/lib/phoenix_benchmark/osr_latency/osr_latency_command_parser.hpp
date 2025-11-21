#ifndef PHOENIX_BENCHMARK_OSR_LATENCY_COMMAND_PARSER_HPP
#define PHOENIX_BENCHMARK_OSR_LATENCY_COMMAND_PARSER_HPP

#include "osr_latency.hpp"
#include <cstddef>
#include <cstdint>

struct PhoenixBenchmarkOsrLatencyParseOutcome {
  bool                              success;
  PhoenixBenchmarkOsrLatencyOptions options;
  const char*                       error_message;
};

constexpr const char k_phoenix_benchmark_osr_latency_error_invalid_command[] = "osr_latency_invalid_command";
constexpr const char k_phoenix_benchmark_osr_latency_error_invalid_value[]   = "osr_latency_invalid_value";

PhoenixBenchmarkOsrLatencyParseOutcome phoenix_benchmark_osr_latency_parse_command_line(const char* line);

#endif  // PHOENIX_BENCHMARK_OSR_LATENCY_COMMAND_PARSER_HPP
