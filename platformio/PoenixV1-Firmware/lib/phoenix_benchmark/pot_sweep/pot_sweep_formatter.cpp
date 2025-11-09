#include "pot_sweep_formatter.hpp"

#include <cstdio>

bool phoenix_benchmark_pot_sweep_format_summary_header(char* buffer, std::size_t length) {
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  const int written = std::snprintf(buffer, length, "Wiper Blue_Max Green_Max Blue_Sat Green_Sat");
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}

bool phoenix_benchmark_pot_sweep_format_summary_row(const PhoenixBenchmarkPotSweepSummaryRowValues& values,
                                                    char* buffer, std::size_t length) {
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  const int written = std::snprintf(buffer, length, "0x%02X %9ld %9ld %5s %5s", values.wiper_code,
                                    static_cast<long>(values.blue_max_code), static_cast<long>(values.green_max_code),
                                    values.blue_saturated ? "yes" : "no", values.green_saturated ? "yes" : "no");
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}
