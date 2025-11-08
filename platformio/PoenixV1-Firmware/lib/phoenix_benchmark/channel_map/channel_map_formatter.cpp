#include "channel_map_formatter.hpp"

#include <cstdio>
#include <cstring>

namespace {

bool append_column_left(char* buffer, std::size_t buffer_length, std::size_t* offset, const char* text,
                        std::size_t width) {
  // Step 1: Validate pointers and ensure we have room before attempting to write.
  if ((buffer == nullptr) || (text == nullptr) || (offset == nullptr)) {
    return false;
  }
  if (*offset >= buffer_length) {
    return false;
  }

  // Step 2: Emit a left-aligned column into the buffer at the current offset.
  const int written = std::snprintf(buffer + *offset, buffer_length - *offset, "%-*s", static_cast<int>(width), text);
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= (buffer_length - *offset)) {
    return false;
  }
  // Step 3: Advance the offset so subsequent columns append after this field.
  *offset += static_cast<std::size_t>(written);
  return true;
}

bool append_column_right(char* buffer, std::size_t buffer_length, std::size_t* offset, const char* text,
                         std::size_t width) {
  // Step 1: Validate inputs and confirm the buffer still has writable capacity.
  if ((buffer == nullptr) || (text == nullptr) || (offset == nullptr)) {
    return false;
  }
  if (*offset >= buffer_length) {
    return false;
  }

  // Step 2: Emit a right-aligned column so numeric values line up visually.
  const int written = std::snprintf(buffer + *offset, buffer_length - *offset, "%*s", static_cast<int>(width), text);
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= (buffer_length - *offset)) {
    return false;
  }
  // Step 3: Update the offset for the next column.
  *offset += static_cast<std::size_t>(written);
  return true;
}

void trim_trailing_space(char* buffer, std::size_t* offset) {
  // Step 1: Ignore trimming when the caller supplied invalid pointers.
  if ((buffer == nullptr) || (offset == nullptr)) {
    return;
  }
  // Step 2: Walk backwards removing trailing spaces to clean up padding.
  while ((*offset > 0u) && (buffer[*offset - 1u] == ' ')) {
    --(*offset);
  }
  // Step 3: Null-terminate the buffer at the trimmed offset.
  buffer[*offset] = '\0';
}

bool format_double(char* destination, std::size_t destination_length, double value, std::size_t width,
                   uint8_t precision) {
  // Step 1: Ensure the destination buffer exists so snprintf writes safely.
  if ((destination == nullptr) || (destination_length == 0u)) {
    return false;
  }
  // Step 2: Format the floating point value with the requested width and precision.
  const int written =
      std::snprintf(destination, destination_length, "%*.*f", static_cast<int>(width), precision, value);
  if (written < 0) {
    return false;
  }
  // Step 3: Reject truncated results so callers only see complete strings.
  if (static_cast<std::size_t>(written) >= destination_length) {
    return false;
  }
  return true;
}

bool format_uint32(char* destination, std::size_t destination_length, uint32_t value, std::size_t width) {
  // Step 1: Validate the target buffer before formatting numeric output.
  if ((destination == nullptr) || (destination_length == 0u)) {
    return false;
  }
  // Step 2: Render the unsigned integer into the buffer with the specified width.
  const int written = std::snprintf(destination, destination_length, "%*lu", static_cast<int>(width),
                                    static_cast<unsigned long>(value));
  if (written < 0) {
    return false;
  }
  // Step 3: Fail when the formatted output did not fit into the destination buffer.
  if (static_cast<std::size_t>(written) >= destination_length) {
    return false;
  }
  return true;
}

bool append_placeholder(char* buffer, std::size_t buffer_length, std::size_t* offset, std::size_t width) {
  // Step 1: Forward to the left-aligned helper so placeholder columns stay aligned.
  // Step 2: Use "--" to visually flag absent data without disturbing layout.
  return append_column_left(buffer, buffer_length, offset, "--", width);
}

}  // namespace

bool phoenix_benchmark_channel_map_format_summary_header(char* buffer, std::size_t buffer_length) {
  // Step 1: Validate the caller supplied a mutable buffer.
  if (buffer == nullptr) {
    return false;
  }

  // Step 2: Append each header label with the matching column width for alignment.
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
  if (!append_column_right(buffer, buffer_length, &offset, "Slope_A",
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
  if (!append_column_right(buffer, buffer_length, &offset, "Slope_B",
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
  if (!append_column_right(buffer, buffer_length, &offset, "Warnings",
                           k_phoenix_benchmark_channel_map_summary_warning_width)) {
    return false;
  }

  // Step 3: Remove trailing padding so log output looks tidy.
  trim_trailing_space(buffer, &offset);
  return true;
}

bool phoenix_benchmark_channel_map_format_summary_row(const PhoenixBenchmarkChannelMapSummaryRowValues& values,
                                                      char* buffer, std::size_t buffer_length) {
  // Step 1: Reject calls lacking an output buffer or state label.
  if ((buffer == nullptr) || (values.label == nullptr)) {
    return false;
  }

  // Step 2: Emit the row label and sample count into their respective columns.
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

  // Step 3: Populate channel statistics when data is available, otherwise placeholders.
  if (values.has_channel_metrics) {
    // Step 3.1: Format channel A metrics in order: mean, standard deviation, slope, min, and max.
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
    if (!format_double(temp, sizeof(temp), values.slope_channel_a,
                       k_phoenix_benchmark_channel_map_summary_channel_width, 6u)) {
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
    // Step 3.2: Format channel B metrics using the same ordering.
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
    if (!format_double(temp, sizeof(temp), values.slope_channel_b,
                       k_phoenix_benchmark_channel_map_summary_channel_width, 6u)) {
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
    // Step 3.3: Fill all channel columns with placeholders when metrics are missing.
    const std::size_t channel_column_count = 10u;
    for (std::size_t column = 0u; column < channel_column_count; ++column) {
      if (!append_placeholder(buffer, buffer_length, &offset, k_phoenix_benchmark_channel_map_summary_channel_width)) {
        return false;
      }
    }
  }

  // Step 4: Append channel alignment and warning columns, defaulting to placeholders.
  const char* alignment_text = (values.channel_alignment != nullptr) ? values.channel_alignment : "--";
  if (!append_column_left(buffer, buffer_length, &offset, alignment_text,
                          k_phoenix_benchmark_channel_map_summary_map_width)) {
    return false;
  }

  const char* warning_text = (values.warning_label != nullptr) ? values.warning_label : "--";
  if (!append_column_left(buffer, buffer_length, &offset, warning_text,
                          k_phoenix_benchmark_channel_map_summary_warning_width)) {
    return false;
  }

  // Step 5: Trim trailing padding so the rendered row is compact.
  trim_trailing_space(buffer, &offset);
  return true;
}
