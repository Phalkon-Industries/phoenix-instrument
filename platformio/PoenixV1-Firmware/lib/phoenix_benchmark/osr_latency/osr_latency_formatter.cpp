#include "osr_latency_formatter.hpp"

#include <cstdio>
#include <cstring>

namespace {

static constexpr const char k_osr_latency_summary_header[] =
    "OSR      Mode      Samples  Mean_us  StdDev_us  Min_us  Max_us";

bool write_text(char* buffer, std::size_t buffer_length, const char* text) {
  if ((buffer == nullptr) || (text == nullptr)) {
    return false;
  }
  const std::size_t length = std::strlen(text);
  if (length >= buffer_length) {
    return false;
  }
  std::memcpy(buffer, text, length + 1u);
  return true;
}

}  // namespace

bool phoenix_benchmark_osr_latency_format_summary_header(char* buffer, std::size_t buffer_length) {
  return write_text(buffer, buffer_length, k_osr_latency_summary_header);
}

bool phoenix_benchmark_osr_latency_format_summary_row(const PhoenixBenchmarkOsrLatencySummaryRowValues& values,
                                                      char* buffer, std::size_t buffer_length) {
  if ((buffer == nullptr) || (buffer_length == 0u) || (values.osr_label == nullptr) || (values.mode_label == nullptr)) {
    return false;
  }

  if (values.has_metrics) {
    const int written = std::snprintf(buffer, buffer_length, "%-8s %-8s %8lu %7.3f %9.3f %7lu %7lu", values.osr_label,
                                      values.mode_label, static_cast<unsigned long>(values.sample_count),
                                      values.mean_us, values.stddev_us, static_cast<unsigned long>(values.min_us),
                                      static_cast<unsigned long>(values.max_us));
    if (written < 0) {
      return false;
    }
    return static_cast<std::size_t>(written) < buffer_length;
  }

  const int written =
      std::snprintf(buffer, buffer_length, "%-8s %-8s %8lu %7s %9s %7s %7s", values.osr_label, values.mode_label,
                    static_cast<unsigned long>(values.sample_count), "--", "--", "--", "--");
  if (written < 0) {
    return false;
  }
  return static_cast<std::size_t>(written) < buffer_length;
}
