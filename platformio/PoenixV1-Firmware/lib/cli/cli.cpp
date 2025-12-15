#include "cli.hpp"

#include <Arduino.h>
#include <string.h>

namespace {
constexpr size_t k_cli_max_command_length = 32u;

struct CliCommandEntry {
  const char* name;
  void (*handler)(void);
  const char* help;
};

static void handle_help(void);
static void handle_baseline(void);
static void handle_sample(void);

constexpr CliCommandEntry k_cli_commands[] = {
    {"b", handle_baseline, "Capture baseline (placeholder)"},
    {"s", handle_sample, "Capture sample + pH (placeholder)"},
    {"help", handle_help, "List commands"},
    {NULL, NULL, NULL},
};

static bool g_cli_ready = false;

// Step 1: Emit the available commands for quick discovery.
static void handle_help(void) {
  for (size_t index = 0u; k_cli_commands[index].name != NULL; ++index) {
    Serial.print(k_cli_commands[index].name);
    Serial.print("\t");
    Serial.println(k_cli_commands[index].help);
  }
}

// Step 1: Placeholder for baseline capture; will be wired to measurement flow.
static void handle_baseline(void) {
  Serial.println("baseline: TODO hook sweep");
}

// Step 1: Placeholder for sample capture; will be wired to measurement flow.
static void handle_sample(void) {
  Serial.println("sample: TODO hook sweep + pH");
}
}  // namespace

CliDispatchResult cli_dispatch_command(const char* command_token) {
  if ((command_token == NULL) || (command_token[0] == '\0')) {
    return CliDispatchResult::empty_command;
  }

  for (size_t index = 0u; k_cli_commands[index].name != NULL; ++index) {
    if (strcmp(command_token, k_cli_commands[index].name) == 0) {
      k_cli_commands[index].handler();
      return CliDispatchResult::ok;
    }
  }

  Serial.print("error\tunknown_command\t");
  Serial.println(command_token);
  return CliDispatchResult::unknown_command;
}

void cli_initialize(void) {
  g_cli_ready = true;
  Serial.println("phoenix-cli ready (commands: b, s, help)");
}

void cli_poll(void) {
  if (!g_cli_ready) {
    return;
  }

  static char   command_buffer[k_cli_max_command_length] = {0};
  static size_t command_length                           = 0u;

  while (Serial.available() > 0) {
    const int incoming_byte = Serial.read();
    if (incoming_byte < 0) {
      break;
    }

    const char incoming_char = static_cast<char>(incoming_byte);

    if ((incoming_char == '\n') || (incoming_char == '\r')) {
      command_buffer[command_length] = '\0';
      if (command_length > 0u) {
        (void) cli_dispatch_command(command_buffer);
      }
      command_length = 0u;
      continue;
    }

    if (command_length < (k_cli_max_command_length - 1u)) {
      command_buffer[command_length] = incoming_char;
      ++command_length;
    }
  }
}
