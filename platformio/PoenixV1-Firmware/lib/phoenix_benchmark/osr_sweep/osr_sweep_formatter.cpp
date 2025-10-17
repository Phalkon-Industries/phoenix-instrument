#include "osr_sweep_formatter.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>

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

void format_integer_metric(char* buffer, std::size_t length, uint32_t value, bool has_metric) {
  if ((buffer == nullptr) || (length == 0u)) {
    return;
  }
  if (!has_metric) {
    std::snprintf(buffer, length, "%s", k_placeholder);
    return;
  }
  std::snprintf(buffer, length, "%" PRIu32, value);
}

}  // namespace

bool phoenix_benchmark_osr_sweep_format_summary_header(char* buffer, std::size_t length) {
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  const int written = std::snprintf(
      buffer, length,
      "Value      Samples  Drain_Mean  Drain_Std  Drain_Min  Drain_Max  LED1_Mean  LED1_Std  LED1_Min  LED1_Max  LED2_Mean  LED2_Std  LED2_Min  LED2_Max  Sweep_us");
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}

bool phoenix_benchmark_osr_sweep_format_summary_row(const PhoenixBenchmarkOsrSweepSummaryRowValues& values,
                                                    char* buffer, std::size_t length) {
  if ((buffer == nullptr) || (length == 0u) || (values.label == nullptr)) {
    return false;
  }

  char drain_mean[16] = {};
  char drain_std[16]  = {};
  char drain_min[16]  = {};
  char drain_max[16]  = {};
  char led1_mean[16]  = {};
  char led1_std[16]   = {};
  char led1_min[16]   = {};
  char led1_max[16]   = {};
  char led2_mean[16]  = {};
  char led2_std[16]   = {};
  char led2_min[16]   = {};
  char led2_max[16]   = {};
  char sweep_us[16]   = {};

  const bool has_metric_values = values.has_metrics;

  format_metric(drain_mean, sizeof(drain_mean), values.drain_mean, has_metric_values);
  format_metric(drain_std, sizeof(drain_std), values.drain_std, has_metric_values);
  format_metric(drain_min, sizeof(drain_min), values.drain_min, has_metric_values);
  format_metric(drain_max, sizeof(drain_max), values.drain_max, has_metric_values);

  format_metric(led1_mean, sizeof(led1_mean), values.led1_mean, has_metric_values);
  format_metric(led1_std, sizeof(led1_std), values.led1_std, has_metric_values);
  format_metric(led1_min, sizeof(led1_min), values.led1_min, has_metric_values);
  format_metric(led1_max, sizeof(led1_max), values.led1_max, has_metric_values);

  format_metric(led2_mean, sizeof(led2_mean), values.led2_mean, has_metric_values);
  format_metric(led2_std, sizeof(led2_std), values.led2_std, has_metric_values);
  format_metric(led2_min, sizeof(led2_min), values.led2_min, has_metric_values);
  format_metric(led2_max, sizeof(led2_max), values.led2_max, has_metric_values);

  format_integer_metric(sweep_us, sizeof(sweep_us), values.sweep_duration_us, has_metric_values);

  const int written = std::snprintf(
      buffer, length, "%-10s%9" PRIu32 "  %10s  %10s  %10s  %10s  %10s  %10s  %10s  %10s  %10s  %10s  %10s  %10s  %10s",
      values.label, values.sample_count, drain_mean, drain_std, drain_min, drain_max, led1_mean, led1_std, led1_min,
      led1_max, led2_mean, led2_std, led2_min, led2_max, sweep_us);
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}
