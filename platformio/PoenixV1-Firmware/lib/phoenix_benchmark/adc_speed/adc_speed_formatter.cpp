#include "adc_speed_formatter.hpp"

#include <cstdio>

namespace {

bool append_column_left(char* buffer, std::size_t buffer_length, std::size_t* offset, const char* text,
                        std::size_t width) {
  if ((buffer == nullptr) || (text == nullptr) || (offset == nullptr)) {
    return false;
  }
  if (*offset >= buffer_length) {
    return false;
  }

  if (*offset > 0u) {
    if (*offset >= (buffer_length - 1u)) {
      return false;
    }
    buffer[*offset] = ' ';
    ++(*offset);
  }

  const int written = std::snprintf(buffer + *offset, buffer_length - *offset, "%-*s", static_cast<int>(width), text);
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= (buffer_length - *offset)) {
    return false;
  }

  *offset += static_cast<std::size_t>(written);
  return true;
}

bool append_column_right(char* buffer, std::size_t buffer_length, std::size_t* offset, const char* text,
                         std::size_t width) {
  if ((buffer == nullptr) || (text == nullptr) || (offset == nullptr)) {
    return false;
  }
  if (*offset >= buffer_length) {
    return false;
  }

  if (*offset > 0u) {
    if (*offset >= (buffer_length - 1u)) {
      return false;
    }
    buffer[*offset] = ' ';
    ++(*offset);
  }

  const int written = std::snprintf(buffer + *offset, buffer_length - *offset, "%*s", static_cast<int>(width), text);
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= (buffer_length - *offset)) {
    return false;
  }

  *offset += static_cast<std::size_t>(written);
  return true;
}

bool format_double(char* destination, std::size_t destination_length, double value, std::size_t width,
                   uint8_t precision) {
  if ((destination == nullptr) || (destination_length == 0u)) {
    return false;
  }

  const int written =
      std::snprintf(destination, destination_length, "%*.*f", static_cast<int>(width), precision, value);
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= destination_length) {
    return false;
  }

  return true;
}

bool format_uint32(char* destination, std::size_t destination_length, uint32_t value, std::size_t width) {
  if ((destination == nullptr) || (destination_length == 0u)) {
    return false;
  }

  const int written = std::snprintf(destination, destination_length, "%*lu", static_cast<int>(width),
                                    static_cast<unsigned long>(value));
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= destination_length) {
    return false;
  }

  return true;
}

bool append_placeholder(char* buffer, std::size_t buffer_length, std::size_t* offset, std::size_t width) {
  return append_column_left(buffer, buffer_length, offset, "--", width);
}

void trim_trailing_space(char* buffer, std::size_t* offset) {
  if ((buffer == nullptr) || (offset == nullptr)) {
    return;
  }

  while ((*offset > 0u) && (buffer[*offset - 1u] == ' ')) {
    --(*offset);
  }
  buffer[*offset] = '\0';
}

}  // namespace

bool phoenix_benchmark_adc_speed_format_summary_header(char* buffer, std::size_t buffer_length) {
  if (buffer == nullptr) {
    return false;
  }

  std::size_t offset = 0u;
  if (!append_column_left(buffer, buffer_length, &offset, "Mode", k_phoenix_benchmark_adc_speed_summary_mode_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Samples_per_s",
                           k_phoenix_benchmark_adc_speed_summary_rate_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Loop_us",
                           k_phoenix_benchmark_adc_speed_summary_loop_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Errors",
                           k_phoenix_benchmark_adc_speed_summary_error_width)) {
    return false;
  }
  if (!append_column_left(buffer, buffer_length, &offset, "Notes", k_phoenix_benchmark_adc_speed_summary_notes_width)) {
    return false;
  }

  trim_trailing_space(buffer, &offset);
  return true;
}

bool phoenix_benchmark_adc_speed_format_summary_row(const PhoenixBenchmarkAdcSpeedSummaryRowValues& values,
                                                    char* buffer, std::size_t buffer_length) {
  if ((buffer == nullptr) || (values.mode_label == nullptr)) {
    return false;
  }

  std::size_t offset = 0u;
  if (!append_column_left(buffer, buffer_length, &offset, values.mode_label,
                          k_phoenix_benchmark_adc_speed_summary_mode_width)) {
    return false;
  }

  char temp[48] = {};
  if (values.has_metrics) {
    if (!format_double(temp, sizeof(temp), values.samples_per_second, k_phoenix_benchmark_adc_speed_summary_rate_width,
                       2u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp, k_phoenix_benchmark_adc_speed_summary_rate_width)) {
      return false;
    }

    if (!format_double(temp, sizeof(temp), values.loop_microseconds, k_phoenix_benchmark_adc_speed_summary_loop_width,
                       3u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp, k_phoenix_benchmark_adc_speed_summary_loop_width)) {
      return false;
    }

    if (!format_uint32(temp, sizeof(temp), values.error_count, k_phoenix_benchmark_adc_speed_summary_error_width)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp, k_phoenix_benchmark_adc_speed_summary_error_width)) {
      return false;
    }
  }
  else {
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_adc_speed_summary_rate_width)) {
      return false;
    }
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_adc_speed_summary_loop_width)) {
      return false;
    }
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_adc_speed_summary_error_width)) {
      return false;
    }
  }

  const char* notes = (values.notes != nullptr) ? values.notes : "--";
  if (!append_column_left(buffer, buffer_length, &offset, notes, k_phoenix_benchmark_adc_speed_summary_notes_width)) {
    return false;
  }

  trim_trailing_space(buffer, &offset);
  return true;
}
