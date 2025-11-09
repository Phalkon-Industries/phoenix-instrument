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
      "Value      Samples  Drain_Blue_Mean  Drain_Blue_Std  Drain_Blue_Min  Drain_Blue_Max  Drain_Green_Mean  Drain_Green_Std  Drain_Green_Min  Drain_Green_Max  Blue_Mean  Blue_Std  Blue_Min  Blue_Max  Green_Mean  Green_Std  Green_Min  Green_Max  Sweep_us");
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

  char drain_blue_mean[16]  = {};
  char drain_blue_std[16]   = {};
  char drain_blue_min[16]   = {};
  char drain_blue_max[16]   = {};
  char drain_green_mean[16] = {};
  char drain_green_std[16]  = {};
  char drain_green_min[16]  = {};
  char drain_green_max[16]  = {};
  char blue_mean[16]        = {};
  char blue_std[16]         = {};
  char blue_min[16]         = {};
  char blue_max[16]         = {};
  char green_mean[16]       = {};
  char green_std[16]        = {};
  char green_min[16]        = {};
  char green_max[16]        = {};
  char sweep_us[16]         = {};

  const bool has_metric_values = values.has_metrics;

  format_metric(drain_blue_mean, sizeof(drain_blue_mean), values.drain_blue_mean, has_metric_values);
  format_metric(drain_blue_std, sizeof(drain_blue_std), values.drain_blue_std, has_metric_values);
  format_metric(drain_blue_min, sizeof(drain_blue_min), values.drain_blue_min, has_metric_values);
  format_metric(drain_blue_max, sizeof(drain_blue_max), values.drain_blue_max, has_metric_values);

  format_metric(drain_green_mean, sizeof(drain_green_mean), values.drain_green_mean, has_metric_values);
  format_metric(drain_green_std, sizeof(drain_green_std), values.drain_green_std, has_metric_values);
  format_metric(drain_green_min, sizeof(drain_green_min), values.drain_green_min, has_metric_values);
  format_metric(drain_green_max, sizeof(drain_green_max), values.drain_green_max, has_metric_values);

  format_metric(blue_mean, sizeof(blue_mean), values.blue_mean, has_metric_values);
  format_metric(blue_std, sizeof(blue_std), values.blue_std, has_metric_values);
  format_metric(blue_min, sizeof(blue_min), values.blue_min, has_metric_values);
  format_metric(blue_max, sizeof(blue_max), values.blue_max, has_metric_values);

  format_metric(green_mean, sizeof(green_mean), values.green_mean, has_metric_values);
  format_metric(green_std, sizeof(green_std), values.green_std, has_metric_values);
  format_metric(green_min, sizeof(green_min), values.green_min, has_metric_values);
  format_metric(green_max, sizeof(green_max), values.green_max, has_metric_values);

  format_integer_metric(sweep_us, sizeof(sweep_us), values.sweep_duration_us, has_metric_values);

  const int written = std::snprintf(
      buffer, length,
      "%-10s%9" PRIu32
      "  %15s  %15s  %15s  %15s  %15s  %15s  %15s  %15s  %10s  %10s  %10s  %10s  %10s  %10s  %10s  %10s  %10s",
      values.label, values.sample_count, drain_blue_mean, drain_blue_std, drain_blue_min, drain_blue_max,
      drain_green_mean, drain_green_std, drain_green_min, drain_green_max, blue_mean, blue_std, blue_min, blue_max,
      green_mean, green_std, green_min, green_max, sweep_us);
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}
