#include "command_parser.hpp"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace phoenix_benchmark {
namespace parser {
namespace {

constexpr size_t     k_command_buffer_length = 192u;
constexpr const char k_whitespace_tokens[]   = " \t\r\n";

const char* skip_whitespace(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }
  while ((*text != '\0') && isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  return text;
}

bool parse_unsigned_value(const char* cursor, int base, uint32_t* value_out, const char** end_out) {
  if (cursor == nullptr || value_out == nullptr) {
    return false;
  }
  char*      end_ptr = nullptr;
  const long parsed  = strtol(cursor, &end_ptr, base);
  if ((end_ptr == cursor) || (parsed < 0L)) {
    return false;
  }
  *value_out = static_cast<uint32_t>(parsed);
  if (end_out != nullptr) {
    *end_out = end_ptr;
  }
  return true;
}

bool parse_key_value_arguments(const char* line, const char* expected_command, CommandArguments* arguments,
                               const char** error_message) {
  if (line == nullptr || expected_command == nullptr || arguments == nullptr) {
    if (error_message != nullptr) {
      *error_message = k_error_invalid_command;
    }
    return false;
  }

  char         buffer[k_command_buffer_length] = {};
  const size_t length                          = strnlen(line, sizeof(buffer));
  if ((length == 0u) || (length >= sizeof(buffer))) {
    if (error_message != nullptr) {
      *error_message = k_error_invalid_command;
    }
    return false;
  }

  memcpy(buffer, line, length);
  buffer[length] = '\0';

  char* token = strtok(buffer, k_whitespace_tokens);
  if (token == nullptr || strcmp(token, expected_command) != 0) {
    if (error_message != nullptr) {
      *error_message = k_error_invalid_command;
    }
    return false;
  }

  bool saw_token = false;
  while (true) {
    token = strtok(nullptr, k_whitespace_tokens);
    if (token == nullptr) {
      break;
    }

    saw_token    = true;
    char* equals = strchr(token, '=');
    if (equals == nullptr) {
      if (error_message != nullptr) {
        *error_message = k_error_invalid_value;
      }
      return false;
    }

    *equals           = '\0';
    const char* key   = token;
    const char* value = equals + 1;
    if ((key == nullptr) || (value == nullptr) || (key[0] == '\0') || (value[0] == '\0')) {
      if (error_message != nullptr) {
        *error_message = k_error_invalid_value;
      }
      return false;
    }

    if (strcmp(key, "sweeps") == 0) {
      uint32_t sweeps = 0u;
      if (!parse_unsigned_value(value, 10, &sweeps, nullptr) || sweeps == 0u) {
        if (error_message != nullptr) {
          *error_message = k_error_invalid_value;
        }
        return false;
      }
      arguments->sweep_count        = sweeps;
      arguments->has_sweep_override = true;
      continue;
    }

    if (strcmp(key, "dwell_us") == 0) {
      uint32_t dwell = 0u;
      if (!parse_unsigned_value(value, 10, &dwell, nullptr)) {
        if (error_message != nullptr) {
          *error_message = k_error_invalid_value;
        }
        return false;
      }
      arguments->dwell_us           = dwell;
      arguments->has_dwell_override = true;
      continue;
    }

    if ((strcmp(key, "wiper") == 0) || (strcmp(key, "wiper_code") == 0)) {
      uint32_t wiper_value = 0u;
      if (!parse_unsigned_value(value, 0, &wiper_value, nullptr) || wiper_value > 0xFFu) {
        if (error_message != nullptr) {
          *error_message = k_error_invalid_value;
        }
        return false;
      }
      arguments->wiper_code         = static_cast<uint8_t>(wiper_value & 0xFFu);
      arguments->has_wiper_override = true;
      continue;
    }

    if (error_message != nullptr) {
      *error_message = k_error_unknown_argument;
    }
    return false;
  }

  if (!saw_token) {
    // Bare command is valid; defaults remain in the caller-provided output structure.
    return true;
  }

  return true;
}

bool parse_json_command(const char* line, const char* expected_command, CommandArguments* arguments,
                        const char** error_message) {
  const char* cursor = skip_whitespace(line);
  if (cursor == nullptr || *cursor != '{') {
    if (error_message != nullptr) {
      *error_message = k_error_invalid_command;
    }
    return false;
  }
  ++cursor;

  bool saw_command = false;
  bool saw_sweeps  = false;
  bool saw_dwell   = false;
  bool saw_wiper   = false;

  while (true) {
    cursor = skip_whitespace(cursor);
    if (cursor == nullptr) {
      if (error_message != nullptr) {
        *error_message = k_error_invalid_command;
      }
      return false;
    }

    if (*cursor == '}') {
      ++cursor;
      break;
    }

    if (*cursor != '"') {
      if (error_message != nullptr) {
        *error_message = k_error_invalid_command;
      }
      return false;
    }
    ++cursor;

    const char* key_start = cursor;
    while ((*cursor != '\0') && (*cursor != '"')) {
      ++cursor;
    }
    if (*cursor != '"') {
      if (error_message != nullptr) {
        *error_message = k_error_invalid_command;
      }
      return false;
    }
    const size_t key_length = static_cast<size_t>(cursor - key_start);
    if (key_length == 0u || key_length >= 32u) {
      if (error_message != nullptr) {
        *error_message = k_error_invalid_command;
      }
      return false;
    }

    char key_buffer[32] = {};
    memcpy(key_buffer, key_start, key_length);
    key_buffer[key_length] = '\0';

    ++cursor;
    cursor = skip_whitespace(cursor);
    if (*cursor != ':') {
      if (error_message != nullptr) {
        *error_message = k_error_invalid_command;
      }
      return false;
    }
    ++cursor;
    cursor = skip_whitespace(cursor);

    if (strcmp(key_buffer, "command") == 0) {
      if (*cursor != '"') {
        if (error_message != nullptr) {
          *error_message = k_error_invalid_command;
        }
        return false;
      }
      ++cursor;
      const char* value_start = cursor;
      while ((*cursor != '\0') && (*cursor != '"')) {
        ++cursor;
      }
      if (*cursor != '"') {
        if (error_message != nullptr) {
          *error_message = k_error_invalid_command;
        }
        return false;
      }
      const size_t value_length = static_cast<size_t>(cursor - value_start);
      if ((value_length != strlen(expected_command)) || (strncmp(value_start, expected_command, value_length) != 0)) {
        if (error_message != nullptr) {
          *error_message = k_error_invalid_command;
        }
        return false;
      }
      ++cursor;
      saw_command = true;
    }
    else if (strcmp(key_buffer, "sweeps") == 0) {
      uint32_t    sweep_value = 0u;
      const char* end_ptr     = nullptr;
      if (!parse_unsigned_value(cursor, 10, &sweep_value, &end_ptr) || sweep_value == 0u) {
        if (error_message != nullptr) {
          *error_message = k_error_invalid_value;
        }
        return false;
      }
      arguments->sweep_count        = sweep_value;
      arguments->has_sweep_override = true;
      cursor                        = end_ptr;
      saw_sweeps                    = true;
    }
    else if (strcmp(key_buffer, "dwell_us") == 0) {
      uint32_t    dwell_value = 0u;
      const char* end_ptr     = nullptr;
      if (!parse_unsigned_value(cursor, 10, &dwell_value, &end_ptr)) {
        if (error_message != nullptr) {
          *error_message = k_error_invalid_value;
        }
        return false;
      }
      arguments->dwell_us           = dwell_value;
      arguments->has_dwell_override = true;
      cursor                        = end_ptr;
      saw_dwell                     = true;
    }
    else if (strcmp(key_buffer, "wiper_code") == 0) {
      uint32_t    wiper_value = 0u;
      const char* end_ptr     = nullptr;
      if (!parse_unsigned_value(cursor, 0, &wiper_value, &end_ptr) || wiper_value > 0xFFu) {
        if (error_message != nullptr) {
          *error_message = k_error_invalid_value;
        }
        return false;
      }
      arguments->wiper_code         = static_cast<uint8_t>(wiper_value & 0xFFu);
      arguments->has_wiper_override = true;
      cursor                        = end_ptr;
      saw_wiper                     = true;
    }
    else {
      if (error_message != nullptr) {
        *error_message = k_error_unknown_argument;
      }
      return false;
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
  }

  if (!saw_command || !saw_sweeps || !saw_dwell || !saw_wiper) {
    if (error_message != nullptr) {
      *error_message = k_error_missing_argument;
    }
    return false;
  }

  cursor = skip_whitespace(cursor);
  if (cursor == nullptr || *cursor != '\0') {
    if (error_message != nullptr) {
      *error_message = k_error_invalid_command;
    }
    return false;
  }

  return true;
}

}  // namespace

ParseOutcome parse_command(const char* line, const char* expected_command) {
  CommandArguments arguments = {};

  if (line == nullptr) {
    return ParseOutcome{false, arguments, k_error_invalid_command};
  }

  const char* trimmed = skip_whitespace(line);
  if (trimmed == nullptr || *trimmed == '\0') {
    return ParseOutcome{false, arguments, k_error_invalid_command};
  }

  const char* error_message = nullptr;
  bool        parsed        = false;
  if (*trimmed == '{') {
    parsed = parse_json_command(trimmed, expected_command, &arguments, &error_message);
  }
  else {
    parsed = parse_key_value_arguments(trimmed, expected_command, &arguments, &error_message);
  }

  if (!parsed) {
    return ParseOutcome{false, arguments, error_message != nullptr ? error_message : k_error_invalid_command};
  }

  return ParseOutcome{true, arguments, nullptr};
}

}  // namespace parser
}  // namespace phoenix_benchmark