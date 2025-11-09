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
  // Step 1: Reject calls without writable storage for the header.
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  // Step 2: Emit the fixed-width column labels mirroring other benchmark tables.
  const int written = std::snprintf(buffer, length,
                                    "Dwell_us  Sweeps  Drain_Blue_Mean  Drain_Blue_Std  Drain_Blue_Slope  "
                                    "Drain_Green_Mean  Drain_Green_Std  Drain_Green_Slope  Blue_Mean  Blue_Std  "
                                    "Blue_Slope  Green_Mean  Green_Std  Green_Slope  Duration_us  Warning_Mask");
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
  // Step 1: Guard against null pointers before computing row text.
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  // Step 2: Prepare printable versions of each floating-point metric, preserving placeholders when data is missing.
  char drain_blue_mean[16]   = {};
  char drain_blue_std[16]    = {};
  char drain_blue_slope[16]  = {};
  char drain_green_mean[16]  = {};
  char drain_green_std[16]   = {};
  char drain_green_slope[16] = {};
  char blue_mean[16]         = {};
  char blue_std[16]          = {};
  char blue_slope[16]        = {};
  char green_mean[16]        = {};
  char green_std[16]         = {};
  char green_slope[16]       = {};

  format_metric(drain_blue_mean, sizeof(drain_blue_mean), values.drain_blue_mean, values.has_metrics);
  format_metric(drain_blue_std, sizeof(drain_blue_std), values.drain_blue_std, values.has_metrics);
  format_metric(drain_blue_slope, sizeof(drain_blue_slope), values.drain_blue_slope, values.has_metrics);
  format_metric(drain_green_mean, sizeof(drain_green_mean), values.drain_green_mean, values.has_metrics);
  format_metric(drain_green_std, sizeof(drain_green_std), values.drain_green_std, values.has_metrics);
  format_metric(drain_green_slope, sizeof(drain_green_slope), values.drain_green_slope, values.has_metrics);
  format_metric(blue_mean, sizeof(blue_mean), values.blue_mean, values.has_metrics);
  format_metric(blue_std, sizeof(blue_std), values.blue_std, values.has_metrics);
  format_metric(blue_slope, sizeof(blue_slope), values.blue_slope, values.has_metrics);
  format_metric(green_mean, sizeof(green_mean), values.green_mean, values.has_metrics);
  format_metric(green_std, sizeof(green_std), values.green_std, values.has_metrics);
  format_metric(green_slope, sizeof(green_slope), values.green_slope, values.has_metrics);

  // Step 3: Render the row using fixed-width columns for host-side alignment parity with other scenarios.
  const int written =
      std::snprintf(buffer, length,
                    "%8" PRIu32 "  %6" PRIu32
                    "  %15s  %15s  %17s  %16s  %15s  %17s  %10s  %8s  %10s  %10s  %8s  %11s  %12" PRIu32 "  0x%02X",
                    values.dwell_us, values.sweeps_completed, drain_blue_mean, drain_blue_std, drain_blue_slope,
                    drain_green_mean, drain_green_std, drain_green_slope, blue_mean, blue_std, blue_slope, green_mean,
                    green_std, green_slope, values.duration_us, static_cast<unsigned int>(values.warning_mask));
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}
