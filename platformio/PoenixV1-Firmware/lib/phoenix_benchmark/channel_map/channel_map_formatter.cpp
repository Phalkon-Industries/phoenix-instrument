#include "channel_map_formatter.hpp"

#include <cstdio>
#include <cstring>

namespace {

bool append_column_left(char* buffer, std::size_t buffer_length, std::size_t* offset, const char* text,
                        std::size_t width) {
  if ((buffer == nullptr) || (text == nullptr) || (offset == nullptr)) {
    return false;
  }
  if (*offset >= buffer_length) {
    return false;
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

void trim_trailing_space(char* buffer, std::size_t* offset) {
  if ((buffer == nullptr) || (offset == nullptr)) {
    return;
  }
  while ((*offset > 0u) && (buffer[*offset - 1u] == ' ')) {
    --(*offset);
  }
  buffer[*offset] = '\0';
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
  // "--" visually flags absent data while preserving column alignment.
  return append_column_left(buffer, buffer_length, offset, "--", width);
}

}  // namespace

bool phoenix_benchmark_channel_map_format_summary_header(char* buffer, std::size_t buffer_length) {
  if (buffer == nullptr) {
    return false;
  }

  std::size_t offset = 0u;
  if (!append_column_left(buffer, buffer_length, &offset, "State",
                          k_phoenix_benchmark_channel_map_summary_label_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Samples",
                           k_phoenix_benchmark_channel_map_summary_samples_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Mean_A",
                           k_phoenix_benchmark_channel_map_summary_channel_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Std_A",
                           k_phoenix_benchmark_channel_map_summary_channel_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Min_A",
                           k_phoenix_benchmark_channel_map_summary_channel_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Max_A",
                           k_phoenix_benchmark_channel_map_summary_channel_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Mean_B",
                           k_phoenix_benchmark_channel_map_summary_channel_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Std_B",
                           k_phoenix_benchmark_channel_map_summary_channel_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Min_B",
                           k_phoenix_benchmark_channel_map_summary_channel_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Max_B",
                           k_phoenix_benchmark_channel_map_summary_channel_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, "Channel_Map",
                           k_phoenix_benchmark_channel_map_summary_map_width)) {
    return false;
  }

  trim_trailing_space(buffer, &offset);
  return true;
}

bool phoenix_benchmark_channel_map_format_summary_row(const PhoenixBenchmarkChannelMapSummaryRowValues& values,
                                                      char* buffer, std::size_t buffer_length) {
  if ((buffer == nullptr) || (values.label == nullptr)) {
    return false;
  }

  std::size_t offset = 0u;
  if (!append_column_left(buffer, buffer_length, &offset, values.label,
                          k_phoenix_benchmark_channel_map_summary_label_width)) {
    return false;
  }

  char temp[32];
  if (!format_uint32(temp, sizeof(temp), values.sample_count, k_phoenix_benchmark_channel_map_summary_samples_width)) {
    return false;
  }
  if (!append_column_right(buffer, buffer_length, &offset, temp,
                           k_phoenix_benchmark_channel_map_summary_samples_width)) {
    return false;
  }

  if (values.has_channel_metrics) {
    if (!format_double(temp, sizeof(temp), values.mean_channel_a, k_phoenix_benchmark_channel_map_summary_channel_width,
                       3u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp,
                             k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!format_double(temp, sizeof(temp), values.std_channel_a, k_phoenix_benchmark_channel_map_summary_channel_width,
                       3u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp,
                             k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!format_double(temp, sizeof(temp), values.min_channel_a, k_phoenix_benchmark_channel_map_summary_channel_width,
                       3u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp,
                             k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!format_double(temp, sizeof(temp), values.max_channel_a, k_phoenix_benchmark_channel_map_summary_channel_width,
                       3u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp,
                             k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!format_double(temp, sizeof(temp), values.mean_channel_b, k_phoenix_benchmark_channel_map_summary_channel_width,
                       3u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp,
                             k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!format_double(temp, sizeof(temp), values.std_channel_b, k_phoenix_benchmark_channel_map_summary_channel_width,
                       3u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp,
                             k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!format_double(temp, sizeof(temp), values.min_channel_b, k_phoenix_benchmark_channel_map_summary_channel_width,
                       3u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp,
                             k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!format_double(temp, sizeof(temp), values.max_channel_b, k_phoenix_benchmark_channel_map_summary_channel_width,
                       3u)) {
      return false;
    }
    if (!append_column_right(buffer, buffer_length, &offset, temp,
                             k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
  }
  else {
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
    if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_channel_map_summary_channel_width)) {
      return false;
    }
  }

  const char* alignment_text = values.channel_alignment;
  if (alignment_text == nullptr) {
    alignment_text = "--";
  }
  if (!append_column_left(buffer, buffer_length, &offset, alignment_text,
                          k_phoenix_benchmark_channel_map_summary_map_width)) {
    return false;
  }

  trim_trailing_space(buffer, &offset);
  return true;
}
