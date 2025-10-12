#ifndef PHOENIX_SUMMARY_FORMATTER_HPP
#define PHOENIX_SUMMARY_FORMATTER_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace phoenix_benchmark_support {

/// @brief Defines the per-column widths used by the fixed-width summary table.
/// The values were sized so that common sensor magnitudes (up to six digits and
/// three decimals) render without wrapping while keeping the serial view narrow.
static constexpr std::size_t k_summary_label_width        = 8u;
static constexpr std::size_t k_summary_samples_width      = 9u;
static constexpr std::size_t k_summary_channel_width      = 12u;
static constexpr std::size_t k_summary_map_width          = 12u;
static constexpr std::size_t k_summary_column_count       = 11u;
static constexpr std::size_t k_summary_table_buffer_bytes = 256u;

/// @brief Captures the values required to populate one summary table row.
struct SummaryRowValues {
  const char* label;              ///< Human-readable identifier for the row, e.g. state name or "Cycle".
  uint32_t    sample_count;       ///< Number of captured samples represented by the row.
  double      mean_channel_a;     ///< Mean ADC code for channel A; ignored when has_channel_metrics is false.
  double      std_channel_a;      ///< Sample standard deviation for channel A.
  double      min_channel_a;      ///< Minimum observed ADC code for channel A.
  double      max_channel_a;      ///< Maximum observed ADC code for channel A.
  double      mean_channel_b;     ///< Mean ADC code for channel B.
  double      std_channel_b;      ///< Sample standard deviation for channel B.
  double      min_channel_b;      ///< Minimum observed ADC code for channel B.
  double      max_channel_b;      ///< Maximum observed ADC code for channel B.
  const char* channel_alignment;  ///< String describing channel dominance vs expectation (nullptr renders placeholder).
  bool        has_channel_metrics;  ///< When false, channel metrics print "--" placeholders for clarity.
};

/// @brief Format the summary table header into a caller-supplied buffer.
/// @param buffer Destination character buffer; must be at least
///        k_summary_table_buffer_bytes bytes.
/// @param buffer_length Capacity of the destination buffer in bytes.
/// @return True when formatting succeeded without truncation.
inline bool format_summary_header(char* buffer, std::size_t buffer_length);

/// @brief Format one row of the summary table.
/// @param values Populated metrics describing the row content.
/// @param buffer Destination character buffer; must be at least
///        k_summary_table_buffer_bytes bytes.
/// @param buffer_length Capacity of the destination buffer in bytes.
/// @return True when formatting succeeded without truncation.
inline bool format_summary_row(const SummaryRowValues& values, char* buffer, std::size_t buffer_length);

namespace detail {

inline bool append_column_left(char* buffer, std::size_t buffer_length, std::size_t* offset, const char* text,
                               std::size_t width) {
  if (buffer == nullptr || text == nullptr || offset == nullptr) {
    return false;
  }
  if (*offset >= buffer_length) {
    return false;
  }

  const int written = snprintf(buffer + *offset, buffer_length - *offset, "%-*s", static_cast<int>(width), text);
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= buffer_length - *offset) {
    return false;
  }
  *offset += static_cast<std::size_t>(written);
  return true;
}

inline bool append_column_right(char* buffer, std::size_t buffer_length, std::size_t* offset, const char* text,
                                std::size_t width) {
  if (buffer == nullptr || text == nullptr || offset == nullptr) {
    return false;
  }
  if (*offset >= buffer_length) {
    return false;
  }

  const int written = snprintf(buffer + *offset, buffer_length - *offset, "%*s", static_cast<int>(width), text);
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= buffer_length - *offset) {
    return false;
  }
  *offset += static_cast<std::size_t>(written);
  return true;
}

inline void trim_trailing_space(char* buffer, std::size_t* offset) {
  if (buffer == nullptr || offset == nullptr) {
    return;
  }
  while (*offset > 0u && buffer[*offset - 1u] == ' ') {
    --(*offset);
  }
  buffer[*offset] = '\0';
}

inline bool format_double(char* destination, std::size_t destination_length, double value, std::size_t width,
                          uint8_t precision) {
  if (destination == nullptr || destination_length == 0u) {
    return false;
  }
  const int written = snprintf(destination, destination_length, "%*.*f", static_cast<int>(width), precision, value);
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= destination_length) {
    return false;
  }
  return true;
}

inline bool format_uint32(char* destination, std::size_t destination_length, uint32_t value, std::size_t width) {
  if (destination == nullptr || destination_length == 0u) {
    return false;
  }
  const int written =
      snprintf(destination, destination_length, "%*lu", static_cast<int>(width), static_cast<unsigned long>(value));
  if (written < 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= destination_length) {
    return false;
  }
  return true;
}

inline bool append_placeholder(char* buffer, std::size_t buffer_length, std::size_t* offset, std::size_t width) {
  // "--" visually flags absent data while preserving column alignment.
  return append_column_left(buffer, buffer_length, offset, "--", width);
}

}  // namespace detail

inline bool format_summary_header(char* buffer, std::size_t buffer_length) {
  if (buffer == nullptr) {
    return false;
  }
  std::size_t offset = 0u;
  if (!detail::append_column_left(buffer, buffer_length, &offset, "State", k_summary_label_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Samples", k_summary_samples_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Mean_A", k_summary_channel_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Std_A", k_summary_channel_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Min_A", k_summary_channel_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Max_A", k_summary_channel_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Mean_B", k_summary_channel_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Std_B", k_summary_channel_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Min_B", k_summary_channel_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Max_B", k_summary_channel_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, "Channel_Map", k_summary_map_width)) {
    return false;
  }
  detail::trim_trailing_space(buffer, &offset);
  return true;
}

inline bool format_summary_row(const SummaryRowValues& values, char* buffer, std::size_t buffer_length) {
  if (buffer == nullptr || values.label == nullptr) {
    return false;
  }

  std::size_t offset = 0u;
  if (!detail::append_column_left(buffer, buffer_length, &offset, values.label, k_summary_label_width)) {
    return false;
  }

  char temp[32];
  if (!detail::format_uint32(temp, sizeof(temp), values.sample_count, k_summary_samples_width)) {
    return false;
  }
  if (!detail::append_column_right(buffer, buffer_length, &offset, temp, k_summary_samples_width)) {
    return false;
  }

  const bool include_channel = values.has_channel_metrics;
  if (include_channel) {
    if (!detail::format_double(temp, sizeof(temp), values.mean_channel_a, k_summary_channel_width, 3u)) {
      return false;
    }
    if (!detail::append_column_right(buffer, buffer_length, &offset, temp, k_summary_channel_width)) {
      return false;
    }
    if (!detail::format_double(temp, sizeof(temp), values.std_channel_a, k_summary_channel_width, 3u)) {
      return false;
    }
    if (!detail::append_column_right(buffer, buffer_length, &offset, temp, k_summary_channel_width)) {
      return false;
    }
    if (!detail::format_double(temp, sizeof(temp), values.min_channel_a, k_summary_channel_width, 3u)) {
      return false;
    }
    if (!detail::append_column_right(buffer, buffer_length, &offset, temp, k_summary_channel_width)) {
      return false;
    }
    if (!detail::format_double(temp, sizeof(temp), values.max_channel_a, k_summary_channel_width, 3u)) {
      return false;
    }
    if (!detail::append_column_right(buffer, buffer_length, &offset, temp, k_summary_channel_width)) {
      return false;
    }
    if (!detail::format_double(temp, sizeof(temp), values.mean_channel_b, k_summary_channel_width, 3u)) {
      return false;
    }
    if (!detail::append_column_right(buffer, buffer_length, &offset, temp, k_summary_channel_width)) {
      return false;
    }
    if (!detail::format_double(temp, sizeof(temp), values.std_channel_b, k_summary_channel_width, 3u)) {
      return false;
    }
    if (!detail::append_column_right(buffer, buffer_length, &offset, temp, k_summary_channel_width)) {
      return false;
    }
    if (!detail::format_double(temp, sizeof(temp), values.min_channel_b, k_summary_channel_width, 3u)) {
      return false;
    }
    if (!detail::append_column_right(buffer, buffer_length, &offset, temp, k_summary_channel_width)) {
      return false;
    }
    if (!detail::format_double(temp, sizeof(temp), values.max_channel_b, k_summary_channel_width, 3u)) {
      return false;
    }
    if (!detail::append_column_right(buffer, buffer_length, &offset, temp, k_summary_channel_width)) {
      return false;
    }
  }
  else {
    if (!detail::append_placeholder(buffer, buffer_length, &offset, k_summary_channel_width)) {
      return false;
    }
    if (!detail::append_placeholder(buffer, buffer_length, &offset, k_summary_channel_width)) {
      return false;
    }
    if (!detail::append_placeholder(buffer, buffer_length, &offset, k_summary_channel_width)) {
      return false;
    }
    if (!detail::append_placeholder(buffer, buffer_length, &offset, k_summary_channel_width)) {
      return false;
    }
    if (!detail::append_placeholder(buffer, buffer_length, &offset, k_summary_channel_width)) {
      return false;
    }
    if (!detail::append_placeholder(buffer, buffer_length, &offset, k_summary_channel_width)) {
      return false;
    }
    if (!detail::append_placeholder(buffer, buffer_length, &offset, k_summary_channel_width)) {
      return false;
    }
    if (!detail::append_placeholder(buffer, buffer_length, &offset, k_summary_channel_width)) {
      return false;
    }
  }

  const char* alignment_text = values.channel_alignment;
  if (alignment_text == nullptr) {
    alignment_text = "--";
  }
  if (!detail::append_column_left(buffer, buffer_length, &offset, alignment_text, k_summary_map_width)) {
    return false;
  }

  detail::trim_trailing_space(buffer, &offset);
  return true;
}

}  // namespace phoenix_benchmark_support

#endif  // PHOENIX_SUMMARY_FORMATTER_HPP
