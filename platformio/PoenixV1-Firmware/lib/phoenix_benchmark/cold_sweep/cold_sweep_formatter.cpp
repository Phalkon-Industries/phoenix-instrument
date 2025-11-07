#include "cold_sweep_formatter.hpp"

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

void format_integer(char* buffer, std::size_t length, int32_t value, bool has_metric) {
  if ((buffer == nullptr) || (length == 0u)) {
    return;
  }
  if (!has_metric) {
    std::snprintf(buffer, length, "%s", k_placeholder);
    return;
  }
  std::snprintf(buffer, length, "%ld", static_cast<long>(value));
}

void format_saturation_mask(uint8_t mask, char* buffer, std::size_t length) {
  if ((buffer == nullptr) || (length == 0u)) {
    return;
  }

  if (mask == 0u) {
    std::snprintf(buffer, length, "none");
    return;
  }

  int  offset       = 0;
  bool emitted      = false;
  auto append_token = [&](const char* token) {
    if (offset < 0) {
      return;
    }
    const int written =
        std::snprintf(buffer + offset, length - static_cast<std::size_t>(offset), (offset > 0) ? "|%s" : "%s", token);
    if (written > 0) {
      offset += written;
    }
    emitted = true;
  };

  if ((mask & k_phoenix_benchmark_cold_sweep_saturation_drain_blue) != 0u) {
    append_token("db");
  }
  if ((mask & k_phoenix_benchmark_cold_sweep_saturation_drain_green) != 0u) {
    append_token("dg");
  }
  if ((mask & k_phoenix_benchmark_cold_sweep_saturation_blue) != 0u) {
    append_token("blue");
  }
  if ((mask & k_phoenix_benchmark_cold_sweep_saturation_green) != 0u) {
    append_token("green");
  }

  if (!emitted) {
    std::snprintf(buffer, length, "none");
  }
}

}  // namespace

bool phoenix_benchmark_cold_sweep_format_summary_header(char* buffer, std::size_t length) {
  // Step 1: Guard against null buffers before rendering the header.
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  // Step 2: Emit the fixed-width column names aligned with other benchmark tables.
  const int written =
      std::snprintf(buffer, length, "Channel        Samples  Mean      StdDev     Min        Max        Saturated");
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}

bool phoenix_benchmark_cold_sweep_format_summary_row(const PhoenixBenchmarkColdSweepSummaryRowValues& values,
                                                     char* buffer, std::size_t length) {
  // Step 1: Ensure callers provide writable storage for the summary output.
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  // Step 2: Convert floating-point and integer metrics into printable strings.
  char       mean_buffer[16] = {};
  char       std_buffer[16]  = {};
  char       min_buffer[16]  = {};
  char       max_buffer[16]  = {};
  const bool has_samples     = values.has_samples;

  format_metric(mean_buffer, sizeof(mean_buffer), values.mean, has_samples);
  format_metric(std_buffer, sizeof(std_buffer), values.standard_deviation, has_samples);
  format_integer(min_buffer, sizeof(min_buffer), values.min_code, has_samples);
  format_integer(max_buffer, sizeof(max_buffer), values.max_code, has_samples);

  // Step 3: Render the row using fixed-width columns for consistent host parsing.
  const char* label          = (values.label != nullptr) ? values.label : "unknown";
  const char* saturated_text = values.saturated ? "yes" : "no";
  const int   written =
      std::snprintf(buffer, length, "%-12s  %7" PRIu32 "  %9s  %9s  %10s  %10s  %9s", label, values.sample_count,
                    mean_buffer, std_buffer, min_buffer, max_buffer, saturated_text);
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}

bool phoenix_benchmark_cold_sweep_format_sample_header(char* buffer, std::size_t length) {
  // Step 1: Reject calls without storage before emitting the header line.
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  // Step 2: Output the column labels expected by the host plotting scripts.
  const int written = std::snprintf(buffer, length, "Index  Drain_Blue  Drain_Green  Blue  Green  Saturation");
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}

bool phoenix_benchmark_cold_sweep_format_sample_row(const PhoenixBenchmarkColdSweepSampleRowValues& values,
                                                    char* buffer, std::size_t length) {
  // Step 1: Validate the output buffer before formatting the sample row.
  if ((buffer == nullptr) || (length == 0u)) {
    return false;
  }

  // Step 2: Encode the saturation mask into a printable token list.
  char saturation_buffer[24] = {};
  format_saturation_mask(values.saturation_mask, saturation_buffer, sizeof(saturation_buffer));

  // Step 3: Emit the codes with fixed-width alignment so spreadsheets remain readable.
  const int written =
      std::snprintf(buffer, length, "%5" PRIu32 "  %11ld  %12ld  %5ld  %6ld  %s", values.sweep_index,
                    static_cast<long>(values.drain_blue_code), static_cast<long>(values.drain_green_code),
                    static_cast<long>(values.blue_code), static_cast<long>(values.green_code), saturation_buffer);
  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= length) {
    return false;
  }
  return true;
}
