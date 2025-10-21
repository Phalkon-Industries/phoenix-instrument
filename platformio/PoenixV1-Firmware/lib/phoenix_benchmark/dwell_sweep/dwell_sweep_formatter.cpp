#include "dwell_sweep_formatter.hpp"

#include <cinttypes>
#include <cstdio>

namespace {

constexpr const char* k_placeholder = "--";

void format_metric(char* buffer, std::size_t length, double value, bool has_metric) {
  if ((buffer == nullptr) || (length == 0u)) {
    return;
  }
  if (!has_metric) {
    std::snprintf(buffer, length, "%s", k_placeholder);
    return;
  }
  std::snprintf(buffer, length, "%0.3f", value);
}

}  // namespace

bool phoenix_benchmark_dwell_sweep_format_summary_header(char* buffer, std::size_t length) {
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  const int written = std::snprintf(
      buffer, length,
      "Dwell_us  Sweeps  Drain_Mean  Drain_Std  LED1_Mean  LED1_Std  LED2_Mean  LED2_Std  Duration_us  Warning_Mask");
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}

bool phoenix_benchmark_dwell_sweep_format_summary_row(const PhoenixBenchmarkDwellSweepSummaryRowValues& values,
                                                      char* buffer, std::size_t length) {
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  char drain_mean[16] = {};
  char drain_std[16]  = {};
  char led1_mean[16]  = {};
  char led1_std[16]   = {};
  char led2_mean[16]  = {};
  char led2_std[16]   = {};

  format_metric(drain_mean, sizeof(drain_mean), values.drain_mean, values.has_metrics);
  format_metric(drain_std, sizeof(drain_std), values.drain_std, values.has_metrics);
  format_metric(led1_mean, sizeof(led1_mean), values.led1_mean, values.has_metrics);
  format_metric(led1_std, sizeof(led1_std), values.led1_std, values.has_metrics);
  format_metric(led2_mean, sizeof(led2_mean), values.led2_mean, values.has_metrics);
  format_metric(led2_std, sizeof(led2_std), values.led2_std, values.has_metrics);

  const int written = std::snprintf(
      buffer, length, "%8" PRIu32 "  %6" PRIu32 "  %10s  %9s  %10s  %8s  %10s  %8s  %12" PRIu32 "  0x%02X",
      values.dwell_us, values.sweeps_completed, drain_mean, drain_std, led1_mean, led1_std, led2_mean, led2_std,
      values.duration_us, static_cast<unsigned int>(values.warning_mask));
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}
