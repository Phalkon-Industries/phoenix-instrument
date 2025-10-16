#include "adc_speed_command_parser.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

const char* skip_whitespace(const char* cursor) {
  if (cursor == nullptr) {
    return nullptr;
  }
  while ((*cursor != '\0') && std::isspace(static_cast<unsigned char>(*cursor))) {
    ++cursor;
  }
  return cursor;
}

bool parse_unsigned_value(const char* cursor, int base, uint32_t* value_out, const char** end_out) {
  if ((cursor == nullptr) || (value_out == nullptr)) {
    return false;
  }

  char*      parse_end = nullptr;
  const long parsed    = std::strtol(cursor, &parse_end, base);
  if ((parse_end == cursor) || (parsed < 0L)) {
    return false;
  }

  *value_out = static_cast<uint32_t>(parsed);
  if (end_out != nullptr) {
    *end_out = parse_end;
  }
  return true;
}

bool parse_boolean_value(const char* cursor, bool* value_out, const char** end_out) {
  if ((cursor == nullptr) || (value_out == nullptr)) {
    return false;
  }

  if (std::strncmp(cursor, "true", 4) == 0) {
    *value_out = true;
    if (end_out != nullptr) {
      *end_out = cursor + 4;
    }
    return true;
  }

  if (std::strncmp(cursor, "false", 5) == 0) {
    *value_out = false;
    if (end_out != nullptr) {
      *end_out = cursor + 5;
    }
    return true;
  }

  return false;
}

bool strings_equal(const char* text, std::size_t length, const char* expected) {
  if ((text == nullptr) || (expected == nullptr)) {
    return false;
  }
  return (std::strlen(expected) == length) && (std::strncmp(text, expected, length) == 0);
}

bool parse_parameters_object(const char** cursor, PhoenixBenchmarkAdcSpeedOptions* options, const char** error) {
  if ((cursor == nullptr) || (*cursor == nullptr) || (options == nullptr)) {
    if (error != nullptr) {
      *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
    }
    return false;
  }

  if (**cursor != '{') {
    if (error != nullptr) {
      *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
    }
    return false;
  }
  ++(*cursor);

  while (true) {
    *cursor = skip_whitespace(*cursor);
    if (*cursor == nullptr) {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
      }
      return false;
    }

    if (**cursor == '}') {
      ++(*cursor);
      break;
    }

    if (**cursor != '"') {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
      }
      return false;
    }
    ++(*cursor);

    const char* key_start = *cursor;
    while ((**cursor != '\0') && (**cursor != '"')) {
      ++(*cursor);
    }
    if (**cursor != '"') {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
      }
      return false;
    }

    const std::size_t key_length = static_cast<std::size_t>(*cursor - key_start);
    if (key_length == 0u) {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
      }
      return false;
    }

    char key_buffer[32] = {};
    if (key_length >= sizeof(key_buffer)) {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
      }
      return false;
    }
    std::memcpy(key_buffer, key_start, key_length);
    key_buffer[key_length] = '\0';

    ++(*cursor);
    *cursor = skip_whitespace(*cursor);
    if (**cursor != ':') {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
      }
      return false;
    }
    ++(*cursor);
    *cursor = skip_whitespace(*cursor);

    if (std::strcmp(key_buffer, "duration_ms") == 0) {
      uint32_t parsed_value = 0u;
      if (!parse_unsigned_value(*cursor, 10, &parsed_value, cursor) || (parsed_value == 0u)) {
        if (error != nullptr) {
          *error = k_phoenix_benchmark_adc_speed_error_invalid_value;
        }
        return false;
      }
      options->duration_ms = parsed_value;
    }
    else if ((std::strcmp(key_buffer, "enable_blocking") == 0) ||
             (std::strcmp(key_buffer, "enable_blocking_mode") == 0)) {
      bool flag_value = true;
      if (!parse_boolean_value(*cursor, &flag_value, cursor)) {
        if (error != nullptr) {
          *error = k_phoenix_benchmark_adc_speed_error_invalid_value;
        }
        return false;
      }
      options->enable_blocking = flag_value;
    }
    else if ((std::strcmp(key_buffer, "enable_irq") == 0) || (std::strcmp(key_buffer, "enable_irq_mode") == 0)) {
      bool flag_value = true;
      if (!parse_boolean_value(*cursor, &flag_value, cursor)) {
        if (error != nullptr) {
          *error = k_phoenix_benchmark_adc_speed_error_invalid_value;
        }
        return false;
      }
      options->enable_irq = flag_value;
    }
    else {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
      }
      return false;
    }

    *cursor = skip_whitespace(*cursor);
    if (**cursor == ',') {
      ++(*cursor);
      continue;
    }
    if (**cursor == '}') {
      ++(*cursor);
      break;
    }
    if (error != nullptr) {
      *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
    }
    return false;
  }

  return true;
}

bool handle_key_value(const char* key_buffer, const char** cursor, PhoenixBenchmarkAdcSpeedOptions* options,
                      const char** error) {
  if ((key_buffer == nullptr) || (cursor == nullptr) || (*cursor == nullptr) || (options == nullptr)) {
    if (error != nullptr) {
      *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
    }
    return false;
  }

  if ((std::strcmp(key_buffer, "parameters") == 0) || (std::strcmp(key_buffer, "arguments") == 0)) {
    return parse_parameters_object(cursor, options, error);
  }

  if (std::strcmp(key_buffer, "duration_ms") == 0) {
    uint32_t parsed_value = 0u;
    if (!parse_unsigned_value(*cursor, 10, &parsed_value, cursor) || (parsed_value == 0u)) {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_value;
      }
      return false;
    }
    options->duration_ms = parsed_value;
    return true;
  }

  if ((std::strcmp(key_buffer, "enable_blocking") == 0) || (std::strcmp(key_buffer, "enable_blocking_mode") == 0)) {
    bool flag_value = true;
    if (!parse_boolean_value(*cursor, &flag_value, cursor)) {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_value;
      }
      return false;
    }
    options->enable_blocking = flag_value;
    return true;
  }

  if ((std::strcmp(key_buffer, "enable_irq") == 0) || (std::strcmp(key_buffer, "enable_irq_mode") == 0)) {
    bool flag_value = true;
    if (!parse_boolean_value(*cursor, &flag_value, cursor)) {
      if (error != nullptr) {
        *error = k_phoenix_benchmark_adc_speed_error_invalid_value;
      }
      return false;
    }
    options->enable_irq = flag_value;
    return true;
  }

  if (error != nullptr) {
    *error = k_phoenix_benchmark_adc_speed_error_invalid_command;
  }
  return false;
}

}  // namespace

PhoenixBenchmarkAdcSpeedParseOutcome phoenix_benchmark_adc_speed_parse_command_line(const char* line) {
  PhoenixBenchmarkAdcSpeedOptions options = {1000u, true, true};
  const char*                     error   = nullptr;

  if (line == nullptr) {
    return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
  }

  const char* cursor = skip_whitespace(line);
  if ((cursor == nullptr) || (*cursor != '{')) {
    return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
  }
  ++cursor;

  bool saw_command = false;

  while (true) {
    cursor = skip_whitespace(cursor);
    if (cursor == nullptr) {
      return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
    }

    if (*cursor == '}') {
      ++cursor;
      break;
    }

    if (*cursor != '"') {
      return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
    }
    ++cursor;

    const char* key_start = cursor;
    while ((*cursor != '\0') && (*cursor != '"')) {
      ++cursor;
    }
    if (*cursor != '"') {
      return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
    }

    const std::size_t key_length = static_cast<std::size_t>(cursor - key_start);
    if (key_length == 0u) {
      return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
    }

    char key_buffer[32] = {};
    if (key_length >= sizeof(key_buffer)) {
      return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
    }
    std::memcpy(key_buffer, key_start, key_length);
    key_buffer[key_length] = '\0';

    ++cursor;
    cursor = skip_whitespace(cursor);
    if (*cursor != ':') {
      return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
    }
    ++cursor;
    cursor = skip_whitespace(cursor);

    if (std::strcmp(key_buffer, "command") == 0) {
      if (*cursor != '"') {
        return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
      }
      ++cursor;
      const char* value_start = cursor;
      while ((*cursor != '\0') && (*cursor != '"')) {
        ++cursor;
      }
      if (*cursor != '"') {
        return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
      }

      const std::size_t value_length = static_cast<std::size_t>(cursor - value_start);
      if (!strings_equal(value_start, value_length, "adc_speed")) {
        return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
      }

      saw_command = true;
      ++cursor;
    }
    else {
      if (!handle_key_value(key_buffer, &cursor, &options, &error)) {
        if (error == nullptr) {
          error = k_phoenix_benchmark_adc_speed_error_invalid_command;
        }
        return {false, options, error};
      }
    }

    cursor = skip_whitespace(cursor);
    if (*cursor == ',') {
      ++cursor;
      continue;
    }
    if (*cursor == '}') {
      ++cursor;
      break;
    }
    if (*cursor == '\0') {
      break;
    }
  }

  if (!saw_command) {
    return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_command};
  }

  if (options.duration_ms == 0u) {
    return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_value};
  }

  if (!options.enable_blocking && !options.enable_irq) {
    return {false, options, k_phoenix_benchmark_adc_speed_error_invalid_value};
  }

  return {true, options, nullptr};
}
