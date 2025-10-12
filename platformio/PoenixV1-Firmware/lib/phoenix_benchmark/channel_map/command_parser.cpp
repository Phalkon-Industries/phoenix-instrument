#include "command_parser.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr std::size_t k_command_buffer_length = 192u;
constexpr const char  k_whitespace_tokens[]   = " \t\r\n";

const char* skip_whitespace(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }
  while ((*text != '\0') && std::isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  return text;
}

bool parse_unsigned_value(const char* cursor, int base, uint32_t* value_out, const char** end_out) {
  if ((cursor == nullptr) || (value_out == nullptr)) {
    return false;
  }

  char*      end_ptr = nullptr;
  const long parsed  = std::strtol(cursor, &end_ptr, base);
  if ((end_ptr == cursor) || (parsed < 0L)) {
    return false;
  }

  *value_out = static_cast<uint32_t>(parsed);
  if (end_out != nullptr) {
    *end_out = end_ptr;
  }
  return true;
}

bool parse_key_value_arguments(const char* line, const char* expected_command,
                               PhoenixBenchmarkChannelMapCommandArguments* arguments, const char** error_message) {
  if ((line == nullptr) || (expected_command == nullptr) || (arguments == nullptr)) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
    }
    return false;
  }

  char        buffer[k_command_buffer_length] = {};
  std::size_t length                          = 0u;
  while ((length < (sizeof(buffer) - 1u)) && (line[length] != '\0')) {
    ++length;
  }
  const bool overflow = (line[length] != '\0');
  if ((length == 0u) || overflow) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
    }
    return false;
  }

  std::memcpy(buffer, line, length);
  buffer[length] = '\0';

  char* token = std::strtok(buffer, k_whitespace_tokens);
  if ((token == nullptr) || (std::strcmp(token, expected_command) != 0)) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
    }
    return false;
  }

  bool saw_token = false;
  while (true) {
    token = std::strtok(nullptr, k_whitespace_tokens);
    if (token == nullptr) {
      break;
    }

    saw_token    = true;
    char* equals = std::strchr(token, '=');
    if (equals == nullptr) {
      if (error_message != nullptr) {
        *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
      }
      return false;
    }

    *equals           = '\0';
    const char* key   = token;
    const char* value = equals + 1;
    if ((key == nullptr) || (value == nullptr) || (key[0] == '\0') || (value[0] == '\0')) {
      if (error_message != nullptr) {
        *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
      }
      return false;
    }

    if (std::strcmp(key, "sweeps") == 0) {
      uint32_t sweeps = 0u;
      if (!parse_unsigned_value(value, 10, &sweeps, nullptr) || (sweeps == 0u)) {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
        }
        return false;
      }
      arguments->sweep_count        = sweeps;
      arguments->has_sweep_override = true;
      continue;
    }

    if (std::strcmp(key, "dwell_us") == 0) {
      uint32_t dwell = 0u;
      if (!parse_unsigned_value(value, 10, &dwell, nullptr)) {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
        }
        return false;
      }
      arguments->dwell_us           = dwell;
      arguments->has_dwell_override = true;
      continue;
    }

    if ((std::strcmp(key, "wiper") == 0) || (std::strcmp(key, "wiper_code") == 0)) {
      uint32_t wiper_value = 0u;
      if (!parse_unsigned_value(value, 0, &wiper_value, nullptr) || (wiper_value > 0xFFu)) {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
        }
        return false;
      }
      arguments->wiper_code         = static_cast<uint8_t>(wiper_value & 0xFFu);
      arguments->has_wiper_override = true;
      continue;
    }

    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_unknown_argument;
    }
    return false;
  }

  if (!saw_token) {
    // Bare command is valid; defaults remain in the caller-provided output structure.
    return true;
  }

  return true;
}

PhoenixBenchmarkChannelMapParseOutcome parse_json_command(const char* line, const char* expected_command,
                                                          PhoenixBenchmarkChannelMapCommandArguments* arguments,
                                                          const char**                                error_message) {
  const char* cursor = skip_whitespace(line);
  if ((cursor == nullptr) || (*cursor != '{')) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
    }
    return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
  }
  ++cursor;

  bool saw_command = false;
  bool saw_sweeps  = false;
  bool saw_wiper   = false;

  while (true) {
    cursor = skip_whitespace(cursor);
    if (cursor == nullptr) {
      if (error_message != nullptr) {
        *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
      }
      return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
    }

    if (*cursor == '}') {
      ++cursor;
      break;
    }

    if (*cursor != '"') {
      if (error_message != nullptr) {
        *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
      }
      return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
    }
    ++cursor;

    const char* key_start = cursor;
    while ((*cursor != '\0') && (*cursor != '"')) {
      ++cursor;
    }
    if (*cursor != '"') {
      if (error_message != nullptr) {
        *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
      }
      return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
    }
    const std::size_t key_length = static_cast<std::size_t>(cursor - key_start);
    if ((key_length == 0u) || (key_length >= 32u)) {
      if (error_message != nullptr) {
        *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
      }
      return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
    }

    char key_buffer[32] = {};
    std::memcpy(key_buffer, key_start, key_length);
    key_buffer[key_length] = '\0';

    ++cursor;
    cursor = skip_whitespace(cursor);
    if (*cursor != ':') {
      if (error_message != nullptr) {
        *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
      }
      return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
    }
    ++cursor;
    cursor = skip_whitespace(cursor);

    if (std::strcmp(key_buffer, "command") == 0) {
      if (*cursor != '"') {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
        }
        return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
      }
      ++cursor;
      const char* value_start = cursor;
      while ((*cursor != '\0') && (*cursor != '"')) {
        ++cursor;
      }
      if (*cursor != '"') {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
        }
        return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
      }
      const std::size_t value_length = static_cast<std::size_t>(cursor - value_start);
      if ((value_length != std::strlen(expected_command)) ||
          (std::strncmp(value_start, expected_command, value_length) != 0)) {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
        }
        return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
      }
      saw_command = true;
      ++cursor;
      cursor = skip_whitespace(cursor);
      if (*cursor == ',') {
        ++cursor;
      }
      continue;
    }

    if ((std::strcmp(key_buffer, "parameters") == 0) || (std::strcmp(key_buffer, "arguments") == 0)) {
      if (*cursor != '{') {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
        }
        return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
      }
      ++cursor;
      while (true) {
        cursor = skip_whitespace(cursor);
        if (cursor == nullptr) {
          if (error_message != nullptr) {
            *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
          }
          return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
        }
        if (*cursor == '}') {
          ++cursor;
          break;
        }
        if (*cursor != '"') {
          if (error_message != nullptr) {
            *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
          }
          return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
        }
        ++cursor;
        const char* param_key_start = cursor;
        while ((*cursor != '\0') && (*cursor != '"')) {
          ++cursor;
        }
        if (*cursor != '"') {
          if (error_message != nullptr) {
            *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
          }
          return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
        }
        const std::size_t param_key_length = static_cast<std::size_t>(cursor - param_key_start);
        if ((param_key_length == 0u) || (param_key_length >= 32u)) {
          if (error_message != nullptr) {
            *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
          }
          return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
        }

        char param_key_buffer[32] = {};
        std::memcpy(param_key_buffer, param_key_start, param_key_length);
        param_key_buffer[param_key_length] = '\0';

        ++cursor;
        cursor = skip_whitespace(cursor);
        if (*cursor != ':') {
          if (error_message != nullptr) {
            *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
          }
          return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
        }
        ++cursor;
        cursor = skip_whitespace(cursor);

        uint32_t parsed_value = 0u;
        if (!parse_unsigned_value(cursor, (std::strcmp(param_key_buffer, "wiper_code") == 0) ? 0 : 10, &parsed_value,
                                  &cursor)) {
          if (error_message != nullptr) {
            *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
          }
          return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_value};
        }

        if (std::strcmp(param_key_buffer, "sweeps") == 0) {
          if (parsed_value == 0u) {
            if (error_message != nullptr) {
              *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
            }
            return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_value};
          }
          arguments->sweep_count        = parsed_value;
          arguments->has_sweep_override = true;
          saw_sweeps                    = true;
        }
        else if (std::strcmp(param_key_buffer, "dwell_us") == 0) {
          arguments->dwell_us           = parsed_value;
          arguments->has_dwell_override = true;
        }
        else if (std::strcmp(param_key_buffer, "wiper_code") == 0) {
          if (parsed_value > 0xFFu) {
            if (error_message != nullptr) {
              *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
            }
            return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_value};
          }
          arguments->wiper_code         = static_cast<uint8_t>(parsed_value & 0xFFu);
          arguments->has_wiper_override = true;
          saw_wiper                     = true;
        }
        else {
          if (error_message != nullptr) {
            *error_message = k_phoenix_benchmark_channel_map_error_unknown_argument;
          }
          return {false, {}, k_phoenix_benchmark_channel_map_error_unknown_argument};
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
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_channel_map_error_invalid_command;
        }
        return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_command};
      }
      cursor = skip_whitespace(cursor);
      if (*cursor == ',') {
        ++cursor;
      }
      continue;
    }

    if ((std::strcmp(key_buffer, "sweeps") == 0) || (std::strcmp(key_buffer, "dwell_us") == 0) ||
        (std::strcmp(key_buffer, "wiper_code") == 0)) {
      const bool is_wiper_key = (std::strcmp(key_buffer, "wiper_code") == 0);
      uint32_t   parsed_value = 0u;
      if (!parse_unsigned_value(cursor, is_wiper_key ? 0 : 10, &parsed_value, &cursor)) {
        if (error_message != nullptr) {
          *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
        }
        return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_value};
      }

      if (std::strcmp(key_buffer, "sweeps") == 0) {
        if (parsed_value == 0u) {
          if (error_message != nullptr) {
            *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
          }
          return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_value};
        }
        arguments->sweep_count        = parsed_value;
        arguments->has_sweep_override = true;
        saw_sweeps                    = true;
      }
      else if (std::strcmp(key_buffer, "dwell_us") == 0) {
        arguments->dwell_us           = parsed_value;
        arguments->has_dwell_override = true;
      }
      else {
        if (parsed_value > 0xFFu) {
          if (error_message != nullptr) {
            *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
          }
          return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_value};
        }
        arguments->wiper_code         = static_cast<uint8_t>(parsed_value & 0xFFu);
        arguments->has_wiper_override = true;
        saw_wiper                     = true;
      }

      cursor = skip_whitespace(cursor);
      if (*cursor == ',') {
        ++cursor;
      }
      continue;
    }

    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_unknown_argument;
    }
    return {false, {}, k_phoenix_benchmark_channel_map_error_unknown_argument};
  }

  if (!saw_command) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_missing_argument;
    }
    return {false, {}, k_phoenix_benchmark_channel_map_error_missing_argument};
  }

  if (!saw_sweeps) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_missing_argument;
    }
    return {false, {}, k_phoenix_benchmark_channel_map_error_missing_argument};
  }

  if (!saw_wiper) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_missing_argument;
    }
    return {false, {}, k_phoenix_benchmark_channel_map_error_missing_argument};
  }

  if (saw_wiper && (arguments->wiper_code > 0xFFu)) {
    if (error_message != nullptr) {
      *error_message = k_phoenix_benchmark_channel_map_error_invalid_value;
    }
    return {false, {}, k_phoenix_benchmark_channel_map_error_invalid_value};
  }

  return {true, *arguments, nullptr};
}

}  // namespace

PhoenixBenchmarkChannelMapParseOutcome phoenix_benchmark_channel_map_parse_command_line(const char* line,
                                                                                        const char* expected_command) {
  PhoenixBenchmarkChannelMapCommandArguments arguments     = {};
  const char*                                error_message = nullptr;

  if (line == nullptr) {
    return {false, arguments, k_phoenix_benchmark_channel_map_error_invalid_command};
  }

  bool        parsing_json = false;
  const char* cursor       = skip_whitespace(line);
  if (cursor != nullptr) {
    parsing_json = (*cursor == '{');
  }

  if (parsing_json) {
    return parse_json_command(line, expected_command, &arguments, &error_message);
  }

  if (!parse_key_value_arguments(line, expected_command, &arguments, &error_message)) {
    return {false, arguments, error_message};
  }

  if (arguments.has_sweep_override && (arguments.sweep_count == 0u)) {
    return {false, arguments, k_phoenix_benchmark_channel_map_error_invalid_value};
  }

  return {true, arguments, nullptr};
}
